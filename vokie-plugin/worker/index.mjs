#!/usr/bin/env node
import { spawn } from 'node:child_process';
import { randomUUID } from 'node:crypto';
import { createRequire } from 'node:module';
import fs from 'node:fs';
import path from 'node:path';
import { fileURLToPath, pathToFileURL } from 'node:url';
import {
  EMPTY_FINAL_SEQUENCE,
  MAX_AUDIO_SEQUENCES_PER_SESSION,
  ADPCM_FRAME_BYTES,
  MAX_AUDIO_PAYLOAD_BASE64_CHARS,
  MAX_CONTROL_BASE64_CHARS,
  MAX_MISSING_AUDIO_FRAMES,
  MAX_CONTROL_PAYLOAD_BYTES,
  MAX_DIAGNOSTIC_MESSAGE_BYTES,
  MAX_DEVICE_ERROR_MESSAGE_BYTES,
  MAX_HELPER_LINE_BYTES,
  isCanonicalBase64,
  parseControlMessage,
  parseHelperLine,
  truncateUtf8
} from './protocol.mjs';
import { decodeImaAdpcmFrame, silenceFrame } from './adpcm.mjs';

export const manifest = {
  id:
    process.env.VOKIE_PLUGIN_ID ||
    '0b7a1e8d-12f1-4a4d-ae4d-5a1f6d0b8e21',
  name: 'AI Passport',
  version: '1.0.4',
  apiVersion: '1',
  platforms: ['darwin'],
  architectures: ['arm64'],
  transports: ['ble'],
  capabilities: {
    ptt: true,
    streamOnly: true,
    sendEnter: true,
    deleteChar: true,
    clearInput: true
  },
  permissions: ['bluetooth', 'native-helper', 'accessibility'],
  icon: 'assets/icon.svg',
  ui: { entrypoint: 'ui/index.html' }
};

const root = path.resolve(path.dirname(fileURLToPath(import.meta.url)), '..');

function isAsarPath(candidate) {
  return /(^|[\\/])[^\\/]+\.asar([\\/]|$)/i.test(candidate);
}

function inferResourcesPathFromAsar(candidate) {
  let cursor = path.resolve(candidate);
  while (cursor !== path.dirname(cursor)) {
    if (path.basename(cursor).toLowerCase().endsWith('.asar')) {
      return path.dirname(cursor);
    }
    cursor = path.dirname(cursor);
  }
  return null;
}

/**
 * Resolve the executable outside app.asar in both source and packaged layouts.
 * The injected options keep path discovery deterministic in build-contract
 * tests without starting CoreBluetooth or touching a physical device.
 */
export function resolveAiPassportHelperPath(options = {}) {
  const workingDirectory = path.resolve(
    typeof options.workingDirectory === 'string' && options.workingDirectory.trim()
      ? options.workingDirectory
      : process.cwd()
  );
  const packageRoot = path.resolve(
    workingDirectory,
    typeof options.packageRoot === 'string' && options.packageRoot.trim()
      ? options.packageRoot
      : root
  );
  const overridePath =
    options.overridePath === undefined
      ? process.env.VOKIE_AI_PASSPORT_HELPER_PATH
      : options.overridePath;
  const resourcesPath =
    options.resourcesPath === undefined
      ? process.resourcesPath
      : options.resourcesPath;
  const existsSync = options.existsSync || fs.existsSync;
  const name = 'ai-passport-helper';
  const candidates = [overridePath];

  // Electron can read files from app.asar, but the OS cannot execute a native
  // helper from that virtual path. Packaged builds place the package under
  // Resources/plugins through extraResources, which is considered below.
  if (!isAsarPath(packageRoot)) {
    candidates.push(
      path.join(packageRoot, 'assets', 'bin', name),
      path.join(packageRoot, 'assets', name)
    );
  }
  const resourceRoots = [
    typeof resourcesPath === 'string' && resourcesPath.trim()
      ? path.resolve(resourcesPath)
      : null,
    inferResourcesPathFromAsar(packageRoot)
  ];
  for (const resourceRoot of new Set(resourceRoots.filter(Boolean))) {
    candidates.push(
      path.join(resourceRoot, 'plugins', 'ai-passport', 'assets', 'bin', name),
      path.join(resourceRoot, 'plugins', 'ai-passport', 'assets', name),
      path.join(resourceRoot, 'bin', name)
    );
  }
  candidates.push(
    path.join(workingDirectory, 'plugins', 'ai-passport', 'assets', 'bin', name),
    path.join(workingDirectory, 'assets', 'bin', name),
    path.join(
      workingDirectory,
      'resources',
      'plugins',
      'ai-passport',
      'assets',
      'bin',
      name
    ),
    path.join(workingDirectory, 'resources', 'bin', name)
  );

  const resolvedCandidates = [
    ...new Set(
      candidates
        .filter(
          (candidate) => typeof candidate === 'string' && candidate.trim().length > 0
        )
        .map((candidate) =>
          path.isAbsolute(candidate)
            ? path.normalize(candidate)
            : path.resolve(workingDirectory, candidate)
        )
        .filter((candidate) => !isAsarPath(candidate))
    )
  ];
  for (const candidate of resolvedCandidates) {
    try {
      if (existsSync(candidate)) return candidate;
    } catch {
      // Continue through the bounded fallback list when a candidate cannot be
      // inspected (for example an unavailable mounted checkout).
    }
  }
  // Preserve a deterministic path in diagnostics when nothing exists. With no
  // explicit override, prefer the canonical package-local development path.
  return resolvedCandidates[0] || path.join(packageRoot, 'assets', 'bin', name);
}

const helperPath = resolveAiPassportHelperPath();
const MAX_PLUGIN_BUFFERED_BYTES = 4 * 1024 * 1024;
const MAX_REQUEST_ID_BYTES = 256;
const MAX_PCM_BYTES = 1024 * 1024;
const ACCEPT_TIMEOUT_MS = 1_200;
const ACTIVE_PTT_MAX_DURATION_MS = 5 * 60_000;
const AUDIO_STALL_TIMEOUT_MS = 3_000;
const HELPER_DIAGNOSTIC_WINDOW_MS = 10_000;
const HELPER_DIAGNOSTIC_LIMIT = 8;
const PERIPHERAL_UUID_PATTERN =
  /^[0-9a-f]{8}-[0-9a-f]{4}-[0-9a-f]{4}-[0-9a-f]{4}-[0-9a-f]{12}$/i;
// An unscoped peripheral error cannot identify a Host request and therefore
// must not cancel a live PTT immediately. Do not wait indefinitely either: if
// the device/Host lifecycles do not converge through a normal release, retire
// that exact request and explicitly unlock the peripheral.
const UNSCOPED_ERROR_RECOVERY_TIMEOUT_MS = 5_000;
// The Host has a 30 s result-processing watchdog. Keep the device-side
// retention window longer than that so a delayed terminal state is not lost,
// while still providing a bounded recovery path if the Host stops responding.
const RESULT_TIMEOUT_MS = 180_000;

export function createPluginRuntime({ socket, spawnHelper = spawn } = {}) {
  let child = null;
  // Keep helper stdout as bytes until a complete newline-delimited record is
  // available. Decoding each chunk as UTF-8 first would silently replace an
  // invalid sequence and could make a malformed control record pass parsing.
  let helperStdout = Buffer.alloc(0);
  let active = null;
  let stopping = false;
  const helperStopRequested = new WeakSet();
  let stopTimer = null;
  let gapTimer = null;
  let acceptTimer = null;
  let activeDurationTimer = null;
  let audioStallTimer = null;
  let unscopedErrorRecovery = null;
  let helperDiagnosticTimer = null;
  let helperDiagnosticWindowStartedAt = null;
  let helperDiagnosticsEmitted = 0;
  let helperDiagnosticsSuppressed = 0;
  let hasPreferredDeviceConfiguration = false;
  let preferredDeviceId = null;
  // Keep the peripheral session identity alongside each host request. A
  // device_error can arrive after session_stop, when `active` is already
  // cleared; without this association a late error could cancel a newer
  // request or clear every in-flight result indiscriminately.
  const awaitingResults = new Map();
  const resultTimers = new Map();
  let lastDevice = null;
  let forgettingDevice = false;
  const discoveredDevices = new Map();
  let deviceHello = false;
  const seenControl = new Set();
  const seenDeviceSessions = new Set();
  const seenButtonEvents = new Set();
  let latestResultRequestId = null;

  const send = (message) => {
    if (socket?.readyState !== 1) return false;
    try {
      const outbound = { ...message };
      if (typeof outbound.message === 'string') {
        outbound.message = truncateUtf8(
          outbound.message,
          MAX_DIAGNOSTIC_MESSAGE_BYTES
        );
      }
      if (typeof outbound.reason === 'string') {
        outbound.reason = truncateUtf8(
          outbound.reason,
          MAX_DIAGNOSTIC_MESSAGE_BYTES
        );
      }
      const encoded = JSON.stringify(outbound);
      // Keep text events well below the WebSocket server's payload cap. Audio
      // uses its own binary path and is intentionally not subject to this
      // check.
      if (Buffer.byteLength(encoded, 'utf8') > MAX_HELPER_LINE_BYTES) return false;
      socket.send(encoded);
      return true;
    } catch {
      return false;
    }
  };
  const diagnostic = (message, level = 'warn') =>
    send({
      type: 'diagnostic',
      level,
      message: truncateUtf8(String(message), MAX_DIAGNOSTIC_MESSAGE_BYTES)
    });
  const redactHelperDiagnostic = (message) => {
    let redacted = String(message);
    redacted = redacted.replace(
      /\b(?:https?|wss?|ftp|file):\/\/[^\s"'<>]+/gi,
      '[redacted-url]'
    );
    redacted = redacted.replace(
      /\b[0-9a-f]{8}-[0-9a-f]{4}-[0-9a-f]{4}-[0-9a-f]{4}-[0-9a-f]{12}\b/gi,
      '[redacted-uuid]'
    );
    redacted = redacted.replace(/(["'])\/(?:[^"']+)\1/g, '$1[redacted-path]$1');
    redacted = redacted.replace(
      /(["'])[A-Za-z]:\\(?:[^"']+)\1/g,
      '$1[redacted-path]$1'
    );
    redacted = redacted.replace(
      /\b[A-Za-z]:\\(?:[^\\\s"'<>]+\\?)+/g,
      '[redacted-path]'
    );
    redacted = redacted.replace(
      /(^|[\s("'=])\/(?:[^/\s"'<>]+\/?)+/g,
      '$1[redacted-path]'
    );
    redacted = redacted.replace(
      /(^|[^A-Za-z0-9+/_=-])([A-Za-z0-9+/_=-]{24,})(?=$|[^A-Za-z0-9+/_=-])/g,
      '$1[redacted-token]'
    );
    return truncateUtf8(redacted, MAX_DIAGNOSTIC_MESSAGE_BYTES);
  };
  const clearHelperDiagnosticWindow = () => {
    if (helperDiagnosticTimer) clearTimeout(helperDiagnosticTimer);
    helperDiagnosticTimer = null;
    helperDiagnosticWindowStartedAt = null;
    helperDiagnosticsEmitted = 0;
    helperDiagnosticsSuppressed = 0;
  };
  const closeHelperDiagnosticWindow = () => {
    if (helperDiagnosticTimer) clearTimeout(helperDiagnosticTimer);
    helperDiagnosticTimer = null;
    const suppressed = helperDiagnosticsSuppressed;
    helperDiagnosticWindowStartedAt = null;
    helperDiagnosticsEmitted = 0;
    helperDiagnosticsSuppressed = 0;
    if (suppressed > 0) {
      diagnostic(`Suppressed ${suppressed} helper diagnostics`, 'warn');
    }
  };
  const startHelperDiagnosticWindow = (now) => {
    helperDiagnosticWindowStartedAt = now;
    helperDiagnosticsEmitted = 0;
    helperDiagnosticsSuppressed = 0;
    helperDiagnosticTimer = setTimeout(
      closeHelperDiagnosticWindow,
      HELPER_DIAGNOSTIC_WINDOW_MS
    );
    helperDiagnosticTimer.unref?.();
  };
  const forwardHelperDiagnostic = (event) => {
    const now = Date.now();
    if (
      helperDiagnosticWindowStartedAt === null ||
      now - helperDiagnosticWindowStartedAt >= HELPER_DIAGNOSTIC_WINDOW_MS
    ) {
      if (helperDiagnosticWindowStartedAt !== null) closeHelperDiagnosticWindow();
      startHelperDiagnosticWindow(now);
    }
    if (helperDiagnosticsEmitted >= HELPER_DIAGNOSTIC_LIMIT) {
      helperDiagnosticsSuppressed += 1;
      return false;
    }
    helperDiagnosticsEmitted += 1;
    const level = ['info', 'warn', 'error'].includes(event?.level)
      ? event.level
      : 'warn';
    const message = redactHelperDiagnostic(
      typeof event?.message === 'string' && event.message.length > 0
        ? event.message
        : 'AI Passport helper diagnostic'
    );
    return diagnostic(message, level);
  };
  const state = (value, details = {}) => {
    const publicState = value === 'processing' ? 'connected' : value;
    send({
      type: 'state',
      state: publicState,
      transport: 'ble',
      ...(details.message
        ? {
            message: truncateUtf8(details.message, MAX_DIAGNOSTIC_MESSAGE_BYTES)
          }
        : {}),
      ...(details.deviceId ? { deviceId: details.deviceId } : {}),
      ...(details.deviceName ? { deviceName: details.deviceName } : {}),
      ...(Object.prototype.hasOwnProperty.call(details, 'preferredDeviceId')
        ? { preferredDeviceId: details.preferredDeviceId }
        : {}),
      ...(details.discoveredDevices || value === 'connected'
        ? {
            discoveredDevices: details.discoveredDevices || [
              ...discoveredDevices.values()
            ]
          }
        : {})
    });
    const hostState = {
      starting: 'ready',
      connected: 'ready',
      stopped: 'ready',
      processing: 'processing',
      error: 'error',
      ready: 'ready'
    }[value];
    // A state refresh during an active PTT must not reset the device to ready.
    // The peripheral owns the recording state after ptt_down; an explicit
    // ready command here can race the first audio frame and make it leave the
    // RECORDING state before ptt_up arrives. Error/processing states still need
    // to be forwarded while the request is active.
    if (hostState && deviceHello && !(active && hostState === 'ready'))
      sendHelper({ type: 'host_state', state: hostState });
  };
  const sendHelper = (message) => {
    const stdin = child?.stdin;
    if (!stdin || stdin.destroyed || stdin.writable === false) return false;
    try {
      const boundedMessage =
        message?.type === 'host_state' && typeof message.message === 'string'
          ? {
              ...message,
              message: truncateUtf8(message.message, MAX_DIAGNOSTIC_MESSAGE_BYTES)
            }
          : message;
      let encoded = JSON.stringify(boundedMessage);
      if (
        Buffer.byteLength(encoded, 'utf8') > MAX_CONTROL_PAYLOAD_BYTES &&
        boundedMessage?.type === 'host_state'
      ) {
        // State diagnostics are advisory. Keep the state command itself
        // deliverable even when a Host error contains an unexpectedly large
        // message (the native helper has the same input-line bound).
        const { message: _discarded, ...withoutMessage } = boundedMessage;
        encoded = JSON.stringify(withoutMessage);
      }
      if (Buffer.byteLength(encoded, 'utf8') > MAX_CONTROL_PAYLOAD_BYTES)
        return false;
      return stdin.write(`${encoded}\n`) !== false;
    } catch {
      return false;
    }
  };
  const sendDeviceReady = () => {
    if (!deviceHello) return false;
    return sendHelper({ type: 'host_ready' });
  };
  const sendHostCommand = (command) => {
    if (!['send_enter', 'delete_char', 'clear_input'].includes(command))
      return false;
    return send({
      type: 'command',
      command,
      requestId: randomUUID(),
      timestampMs: Date.now()
    });
  };
  const cancelVoiceSession = (reason) => {
    let requestId = active?.requestId ?? latestResultRequestId;
    if (!requestId || (!awaitingResults.has(requestId) && !active)) {
      if (!active && awaitingResults.size > 0) {
        requestId = [...awaitingResults.keys()][awaitingResults.size - 1];
      } else {
        return false;
      }
    }
    send({
      type: 'session_cancel',
      requestId,
      timestampMs: Date.now(),
      reason
    });
    clearAwaitingResult(requestId);
    if (active?.requestId === requestId) clearActive();
    diagnostic(reason);
    state('error', { message: reason });
    sendHelper({ type: 'host_state', state: 'ready' });
    return true;
  };
  const sendAudio = (requestId, sequence, pcm) => {
    try {
      if (
        typeof requestId !== 'string' ||
        requestId.length === 0 ||
        Buffer.byteLength(requestId, 'utf8') > MAX_REQUEST_ID_BYTES ||
        !Number.isSafeInteger(sequence) ||
        sequence < 0 ||
        sequence > 0xffffffff ||
        !(pcm instanceof Int16Array) ||
        pcm.byteLength === 0 ||
        pcm.byteLength % 2 !== 0 ||
        pcm.byteLength > MAX_PCM_BYTES
      ) {
        return false;
      }
      const header = new TextEncoder().encode(
        JSON.stringify({
          type: 'audio',
          requestId,
          sequence,
          sampleRate: 16000,
          channels: 1,
          format: 'pcm_s16le'
        })
      );
      const bytes = new Uint8Array(4 + header.byteLength + pcm.byteLength);
      new DataView(bytes.buffer).setUint32(0, header.byteLength);
      bytes.set(header, 4);
      bytes.set(
        new Uint8Array(pcm.buffer, pcm.byteOffset, pcm.byteLength),
        4 + header.byteLength
      );
      if (socket?.readyState !== 1) return false;
      if (
        Number.isFinite(socket.bufferedAmount) &&
        socket.bufferedAmount >= MAX_PLUGIN_BUFFERED_BYTES
      ) {
        diagnostic(
          `Plugin WebSocket backpressure; audio sequence=${sequence} dropped`
        );
        return false;
      }
      socket.send(bytes);
      return true;
    } catch {
      diagnostic('Plugin WebSocket audio send failed', 'error');
      return false;
    }
  };
  const clearActive = ({ recoverUnscopedError = false } = {}) => {
    const requestId = active?.requestId;
    if (stopTimer) clearTimeout(stopTimer);
    stopTimer = null;
    if (gapTimer) clearTimeout(gapTimer);
    gapTimer = null;
    if (acceptTimer) clearTimeout(acceptTimer);
    acceptTimer = null;
    if (activeDurationTimer) clearTimeout(activeDurationTimer);
    activeDurationTimer = null;
    if (audioStallTimer) clearTimeout(audioStallTimer);
    audioStallTimer = null;
    active = null;
    if (requestId && unscopedErrorRecovery?.requestId === requestId) {
      clearTimeout(unscopedErrorRecovery.timer);
      unscopedErrorRecovery = null;
      if (recoverUnscopedError && deviceHello)
        sendHelper({ type: 'host_state', state: 'ready' });
    }
  };
  const clearAwaitingResult = (requestId) => {
    const timer = resultTimers.get(requestId);
    if (timer) clearTimeout(timer);
    resultTimers.delete(requestId);
    awaitingResults.delete(requestId);
    if (latestResultRequestId === requestId) latestResultRequestId = null;
  };
  const clearAwaitingResults = () => {
    for (const timer of resultTimers.values()) clearTimeout(timer);
    resultTimers.clear();
    awaitingResults.clear();
    latestResultRequestId = null;
  };
  const cancelAwaitingResults = (reason) => {
    // A request stays in this table after `session_stop` while Vokie finishes
    // ASR/result processing. If the BLE link or helper dies during that window,
    // clearing the table alone would leave the Host request eligible to finish
    // and paste stale text. Send one identity-scoped cancellation before
    // dropping the bounded bookkeeping.
    for (const requestId of awaitingResults.keys()) {
      send({
        type: 'session_cancel',
        requestId,
        timestampMs: Date.now(),
        reason
      });
    }
    clearAwaitingResults();
  };
  const findAwaitingRequestForDeviceSession = (deviceSessionId) => {
    let match = null;
    const now = Date.now();
    for (const [requestId, result] of awaitingResults) {
      if (result.expiresAt <= now) continue;
      if (result.deviceSessionId === deviceSessionId) {
        match = { requestId, ...result };
      }
    }
    return match;
  };
  const clearDeviceIdentity = () => {
    lastDevice = null;
    deviceHello = false;
    seenControl.clear();
    seenDeviceSessions.clear();
    seenButtonEvents.clear();
  };
  // Collapse every native-helper failure into one terminal path. ChildProcess
  // can emit `error`, `stdin.error`, `stdout.error`, and `close` in different
  // orders; clearing the ownership first prevents a later event from leaving
  // a stale session or blocking a restart attempt.
  const failHelper = (message, processHandle = null) => {
    if (processHandle !== null && child !== processHandle) return false;
    let errorMessage;
    try {
      errorMessage = truncateUtf8(
        String(message || 'AI Passport helper failed'),
        MAX_DIAGNOSTIC_MESSAGE_BYTES
      );
    } catch {
      errorMessage = 'AI Passport helper failed';
    }
    if (active) {
      send({
        type: 'session_cancel',
        requestId: active.requestId,
        timestampMs: Date.now(),
        reason: 'helper error'
      });
    }
    clearActive();
    cancelAwaitingResults('helper error');
    clearDeviceIdentity();
    clearHelperDiagnosticWindow();
    helperStdout = Buffer.alloc(0);
    if (processHandle === null || child === processHandle) child = null;
    diagnostic(errorMessage, 'error');
    state('error', { message: errorMessage });
    // Supervisor treats this as a fatal worker lifecycle event and tears down
    // the authenticated socket, allowing a clean child to be started later.
    send({
      type: 'error',
      code: 'helper_failed',
      fatal: true,
      message: errorMessage
    });
    if (processHandle && !helperStopRequested.has(processHandle)) {
      try {
        processHandle.kill?.();
      } catch {
        // The process may already have exited after emitting the error.
      }
    }
    return true;
  };
  const abortActive = (message) => {
    if (!active) return;
    const requestId = active.requestId;
    send({
      type: 'session_cancel',
      requestId,
      timestampMs: Date.now(),
      reason: message
    });
    clearActive();
    diagnostic(message, 'error');
    state('error', { message });
    // The device remains locked while it is in an error state. Always return
    // it to ready after exposing the terminal error to preserve the lifecycle
    // ordering expected by the firmware.
    sendHelper({ type: 'host_state', state: 'ready' });
  };
  const resetAudioStallWatchdog = () => {
    if (audioStallTimer) clearTimeout(audioStallTimer);
    audioStallTimer = null;
    if (!active?.accepted || active.releaseRequested) return;
    const requestId = active.requestId;
    audioStallTimer = setTimeout(() => {
      audioStallTimer = null;
      if (
        active?.requestId !== requestId ||
        !active.accepted ||
        active.releaseRequested
      )
        return;
      abortActive('AI Passport audio stream stalled');
    }, AUDIO_STALL_TIMEOUT_MS);
    audioStallTimer.unref?.();
  };
  const armActivePttWatchdogs = () => {
    if (!active?.accepted || active.releaseRequested) return;
    const requestId = active.requestId;
    if (activeDurationTimer) clearTimeout(activeDurationTimer);
    activeDurationTimer = setTimeout(() => {
      activeDurationTimer = null;
      if (active?.requestId !== requestId || !active.accepted) return;
      abortActive('AI Passport recording exceeded the 5 minute limit');
    }, ACTIVE_PTT_MAX_DURATION_MS);
    activeDurationTimer.unref?.();
    resetAudioStallWatchdog();
  };
  const deferUnscopedErrorRecovery = (errorMessage) => {
    if (!active) return false;
    const requestId = active.requestId;
    // Repeated diagnostics for the same request must not extend the deadline;
    // otherwise a noisy peripheral could keep both sides wedged forever.
    if (unscopedErrorRecovery?.requestId === requestId) return true;
    if (unscopedErrorRecovery) {
      clearTimeout(unscopedErrorRecovery.timer);
      unscopedErrorRecovery = null;
    }
    const timer = setTimeout(() => {
      if (unscopedErrorRecovery?.requestId !== requestId) return;
      unscopedErrorRecovery = null;
      if (active?.requestId !== requestId) return;
      abortActive(`Unscoped AI Passport error recovery timed out: ${errorMessage}`);
    }, UNSCOPED_ERROR_RECOVERY_TIMEOUT_MS);
    timer.unref?.();
    unscopedErrorRecovery = { requestId, timer };
    return true;
  };
  const awaitResult = (requestId, deviceSessionId) => {
    clearAwaitingResult(requestId);
    const expiresAt = Date.now() + RESULT_TIMEOUT_MS;
    awaitingResults.set(requestId, { expiresAt, deviceSessionId });
    latestResultRequestId = requestId;
    const timer = setTimeout(() => {
      if (awaitingResults.get(requestId)?.expiresAt !== expiresAt) return;
      send({
        type: 'session_cancel',
        requestId,
        timestampMs: Date.now(),
        reason: 'result timeout'
      });
      diagnostic('Vokie result state timed out', 'error');
      // Move the hardware through error and back to ready. Firmware keeps the
      // device locked while it is in error until it receives a ready state.
      sendHelper({
        type: 'host_state',
        state: 'error',
        message: 'Vokie result state timed out'
      });
      sendHelper({ type: 'host_state', state: 'ready' });
      clearAwaitingResult(requestId);
    }, RESULT_TIMEOUT_MS + 100);
    timer.unref?.();
    resultTimers.set(requestId, timer);
  };
  const emitPcm = (requestId, sequence, frame) =>
    sendAudio(requestId, sequence, frame);
  const missingGapError = () =>
    `Audio gap exceeds recovery window (${MAX_MISSING_AUDIO_FRAMES} frames)`;
  const countMissingFrames = (target) => {
    if (!active) return 0;
    let missing = 0;
    let cursor = active.nextSequence;
    const sequences = [...active.frames.keys()]
      .filter(
        (sequence) => sequence >= cursor && (target === null || sequence <= target)
      )
      .sort((a, b) => a - b);
    for (const sequence of sequences) {
      if (sequence > cursor) {
        missing += sequence - cursor;
        if (missing > MAX_MISSING_AUDIO_FRAMES) return missing;
      }
      cursor = sequence + 1;
    }
    if (target !== null && cursor <= target) {
      missing += target - cursor + 1;
    }
    return missing;
  };
  const ensureMissingGapWithinLimit = (target) => {
    if (
      active &&
      active.missingFrames + countMissingFrames(target) <= MAX_MISSING_AUDIO_FRAMES
    )
      return true;
    abortActive(missingGapError());
    return false;
  };
  const emitMissingFrame = (sequence) => {
    if (!active) return false;
    if (active.missingFrames >= MAX_MISSING_AUDIO_FRAMES) {
      abortActive(missingGapError());
      return false;
    }
    diagnostic(`missing audio frame sequence=${sequence}`);
    if (!emitPcm(active.requestId, sequence, silenceFrame())) {
      abortActive('Plugin audio send failed; session cancelled');
      return false;
    }
    active.missingFrames += 1;
    return true;
  };
  const scheduleGap = () => {
    if (gapTimer || !active?.accepted || active.releaseRequested) return;
    if (!active.frames.size) return;
    const firstSequence = Math.min(...active.frames.keys());
    if (firstSequence <= active.nextSequence) return;
    if (firstSequence - active.nextSequence > MAX_MISSING_AUDIO_FRAMES) {
      abortActive(missingGapError());
      return;
    }
    gapTimer = setTimeout(() => {
      gapTimer = null;
      if (
        !active ||
        active.releaseRequested ||
        active.frames.has(active.nextSequence)
      )
        return drainFrames();
      if (!emitMissingFrame(active.nextSequence)) return;
      active.nextSequence += 1;
      drainFrames();
    }, 80);
    gapTimer.unref?.();
  };
  const stopAfterFrames = () => {
    if (!active || !active.accepted || active.stopping) return;
    const target = active.finalSequence;
    // Check the complete gap before emitting anything. This prevents a
    // malformed but numerically valid finalSequence from generating thousands
    // of synthetic silence frames and streaming them into ASR.
    if (!ensureMissingGapWithinLimit(target)) return;
    if (target !== null) {
      while (active.nextSequence <= target) {
        const frame = active.frames.get(active.nextSequence);
        if (frame) {
          if (!emitPcm(active.requestId, active.nextSequence, frame)) {
            abortActive('Plugin audio send failed; session cancelled');
            return;
          }
          active.frames.delete(active.nextSequence);
        } else {
          if (!emitMissingFrame(active.nextSequence)) return;
        }
        active.nextSequence += 1;
      }
    } else {
      for (const sequence of [...active.frames.keys()].sort((a, b) => a - b)) {
        const frame = active.frames.get(sequence);
        if (!frame || sequence < active.nextSequence) continue;
        while (active.nextSequence < sequence) {
          if (!emitMissingFrame(active.nextSequence)) return;
          active.nextSequence += 1;
        }
        if (!emitPcm(active.requestId, sequence, frame)) {
          abortActive('Plugin audio send failed; session cancelled');
          return;
        }
        active.frames.delete(sequence);
        active.nextSequence = sequence + 1;
      }
    }
    active.stopping = true;
    const stoppedRequestId = active.requestId;
    if (
      !send({
        type: 'session_stop',
        requestId: stoppedRequestId,
        timestampMs: Date.now(),
        reason: 'device'
      })
    ) {
      abortActive('Plugin WebSocket is unavailable; session cancelled');
      return;
    }
    awaitResult(stoppedRequestId, active.deviceSessionId);
    // If an unscoped device error arrived during capture, ptt_up is the first
    // trustworthy point at which the peripheral no longer owns a live stream.
    // Preserve the Host result route but release the peripheral immediately.
    clearActive({ recoverUnscopedError: true });
  };
  const scheduleReleaseStop = () => {
    if (!active || !active.accepted || !active.releaseRequested || active.stopping)
      return;
    if (!ensureMissingGapWithinLimit(active.finalSequence)) return;
    if (
      active.finalSequence !== null &&
      active.nextSequence <= active.finalSequence
    ) {
      if (stopTimer) return;
      stopTimer = setTimeout(stopAfterFrames, 500);
      stopTimer.unref?.();
      return;
    }
    stopAfterFrames();
  };
  const drainFrames = () => {
    if (!active?.accepted) return;
    while (active.frames.has(active.nextSequence)) {
      const frame = active.frames.get(active.nextSequence);
      if (!emitPcm(active.requestId, active.nextSequence, frame)) {
        abortActive('Plugin audio send failed; session cancelled');
        return;
      }
      active.frames.delete(active.nextSequence);
      active.nextSequence += 1;
    }
    if (active.frames.size) scheduleGap();
    if (!active) return;
    if (
      active.releaseRequested &&
      (active.finalSequence === null || active.nextSequence > active.finalSequence)
    )
      stopAfterFrames();
  };
  const startPtt = (message) => {
    if (!deviceHello) {
      diagnostic('PTT ignored until AI Passport hello is received');
      return;
    }
    if (active) {
      diagnostic('duplicate ptt_down ignored');
      return;
    }
    if (seenDeviceSessions.has(message.sessionId)) {
      diagnostic(`duplicate device session ignored: ${message.sessionId}`);
      return;
    }
    // A new PTT supersedes any result that is still being processed for a
    // previous request. Keep bounded result tombstones so a late,
    // session-scoped device_error can still cancel the old host request; the
    // session_state path below only accepts the newest request.
    seenDeviceSessions.add(message.sessionId);
    while (seenDeviceSessions.size > 128)
      seenDeviceSessions.delete(seenDeviceSessions.values().next().value);
    active = {
      requestId: randomUUID(),
      deviceSessionId: message.sessionId,
      accepted: false,
      releaseRequested: false,
      stopping: false,
      finalSequence: null,
      nextSequence: 0,
      missingFrames: 0,
      frames: new Map()
    };
    if (
      !send({
        type: 'session_start',
        requestId: active.requestId,
        mode: 'ptt',
        timestampMs: Date.now(),
        options: {
          audioSource: {
            type: 'stream',
            format: 'pcm_s16le',
            sampleRate: 16000,
            channels: 1
          }
        }
      })
    ) {
      clearActive();
      diagnostic('Vokie WebSocket is unavailable; PTT cancelled', 'error');
      state('error', { message: 'Vokie WebSocket is unavailable' });
      sendHelper({ type: 'host_state', state: 'ready' });
      return;
    }
    state('recording', {
      deviceId: lastDevice?.deviceId,
      deviceName: lastDevice?.deviceName,
      preferredDeviceId:
        lastDevice?.preferredDeviceId ??
        (hasPreferredDeviceConfiguration ? preferredDeviceId : undefined)
    });
    acceptTimer = setTimeout(() => {
      if (!active || active.accepted) return;
      diagnostic('session_start acceptance timed out', 'error');
      send({
        type: 'session_cancel',
        requestId: active.requestId,
        timestampMs: Date.now(),
        reason: 'timeout'
      });
      clearActive();
      state('error', { message: 'Vokie session acceptance timed out' });
      sendHelper({ type: 'host_state', state: 'ready' });
    }, ACCEPT_TIMEOUT_MS);
    acceptTimer.unref?.();
  };
  const releasePtt = (message) => {
    if (!active) {
      // The matching ptt_down may have been lost before it reached the Worker.
      // The firmware moves to BUSY after sending ptt_up, so acknowledge the
      // orphan release without inventing or cancelling a Host request.
      diagnostic(`unmatched ptt_up recovered: session=${message.sessionId}`);
      sendHelper({ type: 'host_state', state: 'ready' });
      return;
    }
    if (active.deviceSessionId !== message.sessionId) {
      // A stale release must not unlock the peripheral while another PTT owns
      // the link. That active request will drive its own terminal transition.
      diagnostic(
        `ptt_up session mismatch: active=${active.deviceSessionId}, received=${message.sessionId}`
      );
      return;
    }
    active.releaseRequested = true;
    if (audioStallTimer) clearTimeout(audioStallTimer);
    audioStallTimer = null;
    active.finalSequence =
      message.finalSequence === EMPTY_FINAL_SEQUENCE ? null : message.finalSequence;
    if (
      active.finalSequence !== null &&
      (active.finalSequence >= MAX_AUDIO_SEQUENCES_PER_SESSION ||
        active.finalSequence < active.nextSequence - 1)
    ) {
      diagnostic(`invalid finalSequence=${active.finalSequence}`, 'error');
      send({
        type: 'session_cancel',
        requestId: active.requestId,
        timestampMs: Date.now(),
        reason: 'invalid finalSequence'
      });
      clearActive();
      state('error', { message: 'Invalid audio sequence from AI Passport' });
      sendHelper({ type: 'host_state', state: 'ready' });
      return;
    }
    state('processing');
    if (active.accepted) {
      drainFrames();
      scheduleReleaseStop();
    }
  };
  const onAudio = (event) => {
    if (!active || event.sessionId !== active.deviceSessionId) return;
    if (
      !Number.isSafeInteger(event.sequence) ||
      event.sequence < 0 ||
      event.sequence >= MAX_AUDIO_SEQUENCES_PER_SESSION
    ) {
      diagnostic(`invalid audio sequence=${event.sequence}`, 'error');
      return;
    }
    if (
      typeof event.payloadBase64 !== 'string' ||
      !isCanonicalBase64(
        event.payloadBase64,
        MAX_AUDIO_PAYLOAD_BASE64_CHARS,
        ADPCM_FRAME_BYTES
      )
    ) {
      diagnostic(`invalid audio payload sequence=${event.sequence}`, 'error');
      return;
    }
    if (
      active.releaseRequested &&
      active.finalSequence !== null &&
      event.sequence > active.finalSequence
    ) {
      diagnostic(`audio after finalSequence ignored: ${event.sequence}`);
      return;
    }
    let frame;
    try {
      frame = decodeImaAdpcmFrame(Buffer.from(event.payloadBase64, 'base64'));
    } catch (error) {
      diagnostic(`ADPCM decode failed: ${error.message}`);
      return;
    }
    if (event.sequence < active.nextSequence || active.frames.has(event.sequence)) {
      diagnostic(`duplicate or old audio sequence=${event.sequence}`);
      return;
    }
    if (active.accepted && !active.releaseRequested) resetAudioStallWatchdog();
    if (active.frames.size >= 50) {
      diagnostic(`audio cache full; dropping sequence=${event.sequence}`);
      return;
    }
    active.frames.set(event.sequence, frame);
    drainFrames();
  };
  const handleDeviceEvent = (event) => {
    try {
      if (!event || typeof event !== 'object' || Array.isArray(event)) return;
      if (typeof event.type !== 'string') return;

      if (event.type === 'discovered') {
        discoveredDevices.set(event.deviceId, {
          deviceId: event.deviceId,
          deviceName: event.deviceName || 'Vokie Passport'
        });
        state('ready', { discoveredDevices: [...discoveredDevices.values()] });
        return;
      }

      if (
        event.type === 'connected' ||
        event.type === 'ready' ||
        event.type === 'subscribed'
      ) {
        if (event.type === 'connected') {
          if (forgettingDevice) return;
          if (
            lastDevice?.deviceId &&
            event.deviceId &&
            lastDevice.deviceId !== event.deviceId
          ) {
            if (active) abortActive('AI Passport device changed');
            cancelAwaitingResults('device changed');
            clearDeviceIdentity();
          }
          lastDevice = event;
          forgettingDevice = false;
          if (event.deviceId) {
            discoveredDevices.set(event.deviceId, {
              deviceId: event.deviceId,
              deviceName: event.deviceName || 'Vokie Passport'
            });
          }
          state('connected', event);
        }
        // The peripheral may receive a subscription callback before its hello
        // notification is delivered. Hold host_ready until the validated hello
        // has arrived; firmware uses the same hello-before-PTT gate.
        if ((event.type === 'ready' || event.type === 'subscribed') && deviceHello) {
          sendDeviceReady();
        }
        return;
      }

      if (event.type === 'control') {
        if (
          typeof event.payloadBase64 !== 'string' ||
          !isCanonicalBase64(
            event.payloadBase64,
            MAX_CONTROL_BASE64_CHARS,
            MAX_CONTROL_PAYLOAD_BYTES
          )
        ) {
          diagnostic('invalid control payload');
          return;
        }
        let message;
        try {
          message = parseControlMessage(Buffer.from(event.payloadBase64, 'base64'));
        } catch (error) {
          diagnostic(`invalid control message: ${error.message}`);
          return;
        }
        if (message.type === 'device_error') {
          const errorMessage =
            typeof message.message === 'string' && message.message.length > 0
              ? truncateUtf8(message.message, MAX_DEVICE_ERROR_MESSAGE_BYTES)
              : 'AI Passport device error';
          // sessionId=0 explicitly means that the peripheral had no active
          // PTT session when it produced the diagnostic. It can arrive late
          // after recovery, so it must not terminate a newer active session or
          // disturb a request that is still awaiting its terminal Host result.
          if (message.sessionId === 0) {
            diagnostic(`unscoped device error: ${errorMessage}`);
            // If no request owns the device, make the recovery handshake
            // explicit. A peripheral can emit an unscoped error while it is
            // already idle/locked in ERROR; host_state=ready lets it accept
            // the next PTT without requiring a reconnect. An awaiting Host
            // result does not own a live peripheral session, so it must not
            // block this recovery transition; the result remains routable and
            // is intentionally not cancelled. While an active request still
            // owns the lifecycle, defer ready until its normal release; a
            // request-scoped timeout prevents the mismatch from wedging the
            // peripheral indefinitely.
            if (active) deferUnscopedErrorRecovery(errorMessage);
            else sendHelper({ type: 'host_state', state: 'ready' });
            return;
          }
          const awaitingMatch = findAwaitingRequestForDeviceSession(
            message.sessionId
          );
          if (active && active.deviceSessionId !== message.sessionId) {
            diagnostic(`stale device error ignored: session=${message.sessionId}`);
            if (awaitingMatch) {
              send({
                type: 'session_cancel',
                requestId: awaitingMatch.requestId,
                timestampMs: Date.now(),
                reason: 'device error'
              });
              clearAwaitingResult(awaitingMatch.requestId);
              diagnostic(
                `cancelled awaiting Vokie request for stale device session=${message.sessionId}`,
                'error'
              );
            }
            return;
          }
          if (!active && !awaitingMatch) {
            diagnostic(`stale device error ignored: session=${message.sessionId}`);
            // A lost/missed PTT can leave the peripheral in ERROR even though
            // the Worker has no request to cancel. Recover the idle device so
            // the next physical press is accepted without a reconnect.
            sendHelper({ type: 'host_state', state: 'ready' });
            return;
          }
          const requestId = active?.requestId ?? awaitingMatch?.requestId;
          if (requestId) {
            send({
              type: 'session_cancel',
              requestId,
              timestampMs: Date.now(),
              reason: 'device error'
            });
            clearAwaitingResult(requestId);
          }
          if (active) clearActive();
          diagnostic(errorMessage, 'error');
          state('error', { message: errorMessage });
          // Firmware holds the device in ERROR until it observes an explicit
          // ready state. Preserve the BLE identity so recovery does not require
          // a reconnect.
          sendHelper({ type: 'host_state', state: 'ready' });
          return;
        }
        if (message.type !== 'hello' && !deviceHello) {
          diagnostic('Device event ignored until AI Passport hello is received');
          return;
        }
        if (message.type === 'button_event') {
          const key = `${message.seq ?? `${message.button}:${message.event}:${message.durationMs ?? 0}`}`;
          if (seenButtonEvents.has(key)) return;
          seenButtonEvents.add(key);
          while (seenButtonEvents.size > 64)
            seenButtonEvents.delete(seenButtonEvents.values().next().value);
          if (message.button === 'down' && message.event === 'click')
            sendHostCommand('send_enter');
          else if (
            message.button === 'ok' &&
            (message.event === 'click' || message.event === 'long')
          ) {
            if (active || awaitingResults.size > 0)
              cancelVoiceSession('User cancelled AI Passport voice session');
            else if (message.event === 'click') sendHostCommand('delete_char');
            else sendHostCommand('clear_input');
          }
          return;
        }
        if (message.type === 'ptt_down' || message.type === 'ptt_up') {
          const key = `${message.type}:${message.sessionId}:${message.seq}`;
          if (seenControl.has(key)) {
            diagnostic(`duplicate control ignored: ${key}`);
            return;
          }
          seenControl.add(key);
          while (seenControl.size > 128) {
            seenControl.delete(seenControl.values().next().value);
          }
        }
        if (message.type === 'hello') {
          if (deviceHello) {
            diagnostic('duplicate AI Passport hello; resending host_ready');
            // A peripheral can lose and restore its Control CCCD without a full
            // BLE reconnect. Its state machine clears host_ready and repeats
            // hello, so replay the idempotent handshake command.
            sendDeviceReady();
            if (!active) sendHelper({ type: 'host_state', state: 'ready' });
            return;
          }
          deviceHello = true;
          state('connected', lastDevice || {});
          sendDeviceReady();
        } else if (message.type === 'ptt_down') {
          startPtt(message);
        } else if (message.type === 'ptt_up') {
          releasePtt(message);
        }
        return;
      }

      if (event.type === 'audio') {
        onAudio(event);
        return;
      }

      if (event.type === 'diagnostic') {
        forwardHelperDiagnostic(event);
        return;
      }

      if (event.type === 'disconnected') {
        if (active) {
          send({
            type: 'session_cancel',
            requestId: active.requestId,
            timestampMs: Date.now(),
            reason: 'disconnect'
          });
        }
        clearActive();
        cancelAwaitingResults('disconnect');
        clearDeviceIdentity();
        state('ready', { discoveredDevices: [...discoveredDevices.values()] });
        return;
      }

      if (event.type === 'error') {
        const hadDeviceHello = deviceHello;
        const message =
          typeof event.message === 'string' && event.message.length > 0
            ? event.message
            : 'helper error';
        diagnostic(message, 'error');
        if (active) {
          send({
            type: 'session_cancel',
            requestId: active.requestId,
            timestampMs: Date.now(),
            reason: 'helper error'
          });
          clearActive();
        }
        cancelAwaitingResults('helper error');
        clearDeviceIdentity();
        const stateMessage =
          typeof event.message === 'string' && event.message.length > 0
            ? event.message
            : 'AI Passport helper error';
        state('error', { message: stateMessage });
        // Keep the device lifecycle recoverable after a helper-side failure.
        // The helper may be unavailable, but queueing ready is harmless and
        // ensures a still-connected device can accept the next PTT session.
        if (hadDeviceHello) sendHelper({ type: 'host_state', state: 'ready' });
      }
    } catch {
      // A direct/in-process caller may provide an object with throwing getters
      // or an otherwise malformed payload. Keep the helper event boundary
      // total so one bad record cannot terminate the Worker.
      diagnostic('invalid AI Passport device event');
    }
  };
  const consume = (chunk) => {
    let bytes;
    try {
      if (typeof chunk === 'string') bytes = Buffer.from(chunk, 'utf8');
      else if (chunk instanceof Uint8Array) {
        bytes = Buffer.from(chunk.buffer, chunk.byteOffset, chunk.byteLength);
      } else if (chunk instanceof ArrayBuffer) bytes = Buffer.from(chunk);
      else throw new TypeError('helper stdout must be text or bytes');
    } catch {
      diagnostic('invalid helper stdout chunk discarded');
      return;
    }
    let offset = 0;
    while (offset < bytes.byteLength) {
      const newline = bytes.indexOf(0x0a, offset);
      if (newline < 0) {
        const remainder = bytes.subarray(offset);
        if (helperStdout.byteLength + remainder.byteLength > MAX_HELPER_LINE_BYTES) {
          helperStdout = Buffer.alloc(0);
          diagnostic(
            'helper output exceeded the line size limit; discarded',
            'error'
          );
        } else if (remainder.byteLength > 0) {
          helperStdout = Buffer.concat([helperStdout, remainder]);
        }
        break;
      }
      const record = bytes.subarray(offset, newline);
      const line =
        helperStdout.byteLength > 0 ? Buffer.concat([helperStdout, record]) : record;
      helperStdout = Buffer.alloc(0);
      if (line.byteLength > MAX_HELPER_LINE_BYTES) {
        diagnostic('invalid or oversized helper event discarded');
      } else {
        const event = parseHelperLine(line);
        if (event) handleDeviceEvent(event);
        else if (line.byteLength > 0)
          diagnostic('invalid or oversized helper event discarded');
      }
      offset = newline + 1;
    }
    if (helperStdout.byteLength > MAX_HELPER_LINE_BYTES) {
      helperStdout = Buffer.alloc(0);
      diagnostic('helper output exceeded the line size limit; discarded', 'error');
    }
  };
  const startHelper = () => {
    if (stopping) return false;
    if (child) return true;
    let helperExists = false;
    try {
      helperExists = fs.existsSync(helperPath);
    } catch {
      helperExists = false;
    }
    if (!helperExists) {
      failHelper(`AI Passport helper not found: ${helperPath}`);
      return false;
    }
    try {
      child = spawnHelper(helperPath, [], {
        stdio: ['pipe', 'pipe', 'pipe'],
        windowsHide: true
      });
    } catch (error) {
      failHelper(
        `AI Passport helper failed to start: ${
          error instanceof Error ? error.message : String(error)
        }`
      );
      return false;
    }
    const processHandle = child;
    helperStdout = Buffer.alloc(0);
    const consumeForProcess = (chunk) => {
      // A process that was intentionally stopped can still flush stdout while
      // a replacement helper is starting. Its events belong to the old
      // connection and must not enter the current session.
      if (child !== processHandle) return;
      consume(chunk);
    };
    // Keep stdout binary so invalid UTF-8 can be rejected by parseHelperLine.
    // stderr is diagnostic text and may still use the stream decoder.
    child.stdout?.on?.('data', consumeForProcess);
    child.stderr?.setEncoding?.('utf8');
    child.stderr?.on?.('data', (chunk) => {
      if (child === processHandle)
        forwardHelperDiagnostic({
          level: 'warn',
          message: `helper: ${String(chunk).trim()}`
        });
    });
    child.stdin?.on?.('error', (error) => {
      if (child === processHandle) {
        const detail = error instanceof Error ? error.message : String(error);
        failHelper(`AI Passport helper stdin error: ${detail}`, processHandle);
      }
    });
    child.stdout?.on?.('error', (error) => {
      if (child === processHandle) {
        const detail = error instanceof Error ? error.message : String(error);
        failHelper(`AI Passport helper stdout error: ${detail}`, processHandle);
      }
    });
    child.stderr?.on?.('error', (error) => {
      if (child === processHandle) {
        const detail = error instanceof Error ? error.message : String(error);
        failHelper(`AI Passport helper stderr error: ${detail}`, processHandle);
      }
    });
    child.on?.('error', (error) => {
      if (child === processHandle) {
        const detail = error instanceof Error ? error.message : String(error);
        failHelper(`AI Passport helper process error: ${detail}`, processHandle);
      }
    });
    child.on?.('close', () => {
      const isCurrent = child === processHandle;
      const wasRequested = helperStopRequested.has(processHandle);
      if (!stopping && isCurrent && !wasRequested) {
        failHelper('AI Passport helper stopped', processHandle);
      }
    });
    state('starting');
    if (hasPreferredDeviceConfiguration) {
      sendHelper({ type: 'set-preferred-device', deviceId: preferredDeviceId });
    }
    return true;
  };
  const stopHelper = () => {
    if (active)
      send({
        type: 'session_cancel',
        requestId: active.requestId,
        timestampMs: Date.now(),
        reason: 'disconnect'
      });
    clearActive();
    cancelAwaitingResults('disconnect');
    clearDeviceIdentity();
    clearHelperDiagnosticWindow();
    helperStdout = Buffer.alloc(0);
    const current = child;
    child = null;
    if (!current) return;
    helperStopRequested.add(current);
    try {
      current.stdin.write('{"type":"shutdown"}\n');
    } catch {}
    setTimeout(() => current.kill?.(), 500).unref?.();
  };
  return {
    handleHostMessage(data) {
      try {
        if (typeof data !== 'string') return;
        let message;
        try {
          message = JSON.parse(data);
        } catch {
          return;
        }
        if (!message || typeof message !== 'object' || Array.isArray(message))
          return;
        if (message.type === 'initialize') send({ type: 'initialized' });
        else if (message.type === 'start') {
          if (startHelper()) send({ type: 'ready' });
        } else if (message.type === 'stop') {
          stopHelper();
          send({ type: 'stopped' });
        } else if (message.type === 'query_device_info' && lastDevice)
          state('connected', lastDevice);
        else if (
          message.type === 'configure' ||
          message.type === 'configuration_changed'
        ) {
          const config =
            message.config &&
            typeof message.config === 'object' &&
            !Array.isArray(message.config)
              ? message.config
              : {};
          const hasPreferredDeviceId = Object.prototype.hasOwnProperty.call(
            config,
            'preferredDeviceId'
          );
          const clearPreferredDevice = config.clearPreferredDevice === true;
          if (
            clearPreferredDevice ||
            (hasPreferredDeviceId && config.preferredDeviceId === null)
          ) {
            hasPreferredDeviceConfiguration = true;
            preferredDeviceId = null;
            forgettingDevice = true;
            lastDevice = null;
            discoveredDevices.clear();
            stopHelper();
            setTimeout(() => {
              if (!stopping && forgettingDevice) startHelper();
            }, 600).unref?.();
          } else if (hasPreferredDeviceId) {
            if (
              typeof config.preferredDeviceId !== 'string' ||
              !PERIPHERAL_UUID_PATTERN.test(config.preferredDeviceId.trim())
            ) {
              diagnostic('Invalid preferred AI Passport device id', 'error');
              send({ type: 'configured' });
              return;
            }
            hasPreferredDeviceConfiguration = true;
            forgettingDevice = false;
            preferredDeviceId = config.preferredDeviceId.trim().toUpperCase();
            sendHelper({
              type: 'set-preferred-device',
              deviceId: preferredDeviceId
            });
          }
          send({ type: 'configured' });
          if (clearPreferredDevice || (hasPreferredDeviceId && lastDevice)) {
            if (clearPreferredDevice) {
              state('ready', {
                discoveredDevices: [...discoveredDevices.values()]
              });
              return;
            }
            lastDevice = {
              ...lastDevice,
              preferredDeviceId: hasPreferredDeviceConfiguration
                ? preferredDeviceId
                : lastDevice.preferredDeviceId
            };
            if (!active) state('connected', lastDevice);
          }
        } else if (
          message.type === 'session_accepted' &&
          active?.requestId === message.requestId
        ) {
          if (
            message.mode !== 'ptt' ||
            typeof message.sessionId !== 'string' ||
            message.sessionId.length === 0
          ) {
            // Host responses cross a transport boundary even when they were
            // produced by the local Presenter. Do not let a malformed
            // acceptance unlock the release path or leave the peripheral in
            // RECORDING while waiting for a response that cannot be trusted.
            abortActive('Invalid Vokie session acceptance');
            return;
          }
          active.accepted = true;
          if (acceptTimer) clearTimeout(acceptTimer);
          acceptTimer = null;
          armActivePttWatchdogs();
          drainFrames();
          scheduleReleaseStop();
        } else if (
          message.type === 'session_rejected' &&
          active?.requestId === message.requestId
        ) {
          const reason =
            typeof message.reason === 'string' && message.reason.length > 0
              ? message.reason
              : 'unknown';
          const errorMessage = `Vokie session rejected: ${reason}`;
          diagnostic(`session rejected: ${reason}`, 'error');
          // A rejection can arrive while the peripheral is still in RECORDING.
          // Sending only ready is ignored by the firmware in that state; move it
          // through ERROR first, then back to CONNECTED/ready so a later PTT
          // cannot leave the device stuck in SENDING.
          clearActive();
          state('error', { message: errorMessage });
          state('connected', lastDevice || {});
        } else if (message.type === 'session_state') {
          const activeRequest = active?.requestId === message.requestId;
          const awaitingRequest = awaitingResults.get(message.requestId);
          const latestAwaitingRequest =
            !active &&
            latestResultRequestId === message.requestId &&
            awaitingRequest?.expiresAt > Date.now();
          if (activeRequest || latestAwaitingRequest) {
            if (['processing', 'success', 'error'].includes(message.state)) {
              const terminal =
                message.state === 'success' || message.state === 'error';
              if (activeRequest && terminal) {
                // A terminal Host result normally arrives only after the
                // device has sent ptt_up and the Worker has emitted
                // session_stop. If it overtakes that release, fail closed:
                // cancel the still-open Host route and force the peripheral
                // through ERROR -> READY. Sending success while the device is
                // still RECORDING/SENDING would leave it locked there because
                // the firmware intentionally ignores a ready transition in
                // those states.
                const recoveryMessage =
                  message.state === 'success'
                    ? 'Vokie completed before device release'
                    : String(message.message || 'Vokie session failed');
                send({
                  type: 'session_cancel',
                  requestId: message.requestId,
                  timestampMs: Date.now(),
                  reason:
                    message.state === 'success'
                      ? 'host terminal state before release'
                      : 'host error'
                });
                clearActive();
                diagnostic(recoveryMessage, 'error');
                // Publish the terminal error to the plugin boundary as well
                // as the hardware. The connected state is the recoverable
                // post-error state once the helper has received ready.
                state('error', { message: recoveryMessage });
                state('connected', lastDevice || {});
                clearAwaitingResult(message.requestId);
                return;
              }

              sendHelper({
                type: 'host_state',
                state: message.state,
                ...(message.message ? { message: String(message.message) } : {})
              });
              if (terminal) {
                clearAwaitingResult(message.requestId);
                if (message.state === 'error') {
                  // Error is terminal for the Vokie transaction, but the device
                  // must receive an explicit ready state before accepting PTT
                  // again. Keep the error notification observable first.
                  sendHelper({ type: 'host_state', state: 'ready' });
                }
              }
            }
          } else if (
            awaitingRequest &&
            (message.state === 'success' || message.state === 'error')
          ) {
            // A terminal state for an older request must not be forwarded while
            // a newer PTT owns the device. It still retires that request's
            // watchdog; otherwise the stale timeout can switch the hardware to
            // ERROR/ready in the middle of an unrelated future recording.
            clearAwaitingResult(message.requestId);
          }
        } else if (message.type === 'shutdown') {
          stopping = true;
          stopHelper();
          send({ type: 'destroyed' });
          socket?.close?.();
        }
      } catch {
        // Host responses are transport input. A throwing getter or malformed
        // shape must not terminate the Worker event loop.
        diagnostic('invalid Vokie host message');
      }
    },
    handleDeviceEvent,
    stopHelper
  };
}

export async function startWorker() {
  const url = process.env.VOKIE_PLUGIN_WS_URL;
  const token = process.env.VOKIE_PLUGIN_TOKEN;
  if (!url || !token)
    throw new Error('VOKIE_PLUGIN_WS_URL and VOKIE_PLUGIN_TOKEN are required');
  const WebSocketImpl = await resolveWebSocketImplementation();
  const socket = new WebSocketImpl(url);
  const runtime = createPluginRuntime({ socket });
  const hello = () =>
    socket.send(JSON.stringify({ type: 'plugin_hello', token, manifest }));
  const message = (data) =>
    runtime.handleHostMessage(
      typeof data === 'string' ? data : (data?.data ?? data?.toString?.() ?? '')
    );
  const close = () => runtime.stopHelper();
  const error = (value) => console.error('[AiPassport] WebSocket', value);
  if (typeof socket.on === 'function') {
    socket.on('open', hello);
    socket.on('message', message);
    socket.on('close', close);
    socket.on('error', error);
  } else {
    socket.addEventListener('open', hello);
    socket.addEventListener('message', (event) => message(event.data));
    socket.addEventListener('close', close);
    socket.addEventListener('error', error);
  }
}

function unwrapWebSocketImplementation(value) {
  const candidate = value?.WebSocket || value?.default || value;
  return typeof candidate === 'function' ? candidate : null;
}

/**
 * Resolve a WebSocket implementation even when the Worker itself is shipped
 * outside `app.asar`. Native WebSocket is preferred; older Electron runtimes
 * need `ws`, whose package lives under the sibling app.asar node_modules.
 */
export async function resolveWebSocketImplementation() {
  const nativeWebSocket = unwrapWebSocketImplementation(globalThis.WebSocket);
  if (nativeWebSocket) return nativeWebSocket;

  const loader = path.join(process.cwd(), '.vokie-ws-loader.cjs');
  const candidates = [];
  const explicitPath = process.env.VOKIE_PLUGIN_WS_MODULE_PATH;
  if (typeof explicitPath === 'string' && explicitPath.trim()) {
    candidates.push({ parent: loader, specifier: explicitPath.trim() });
  }

  const resourcesPath =
    typeof process.resourcesPath === 'string' && process.resourcesPath.trim()
      ? path.resolve(process.resourcesPath)
      : null;
  if (resourcesPath) {
    // `createRequire` gives CommonJS package resolution a concrete app.asar
    // parent; unlike ESM NODE_PATH, this also works in packaged Electron.
    candidates.push(
      {
        parent: path.join(resourcesPath, 'app.asar', 'package.json'),
        specifier: 'ws'
      },
      {
        parent: path.join(
          resourcesPath,
          'app.asar',
          'node_modules',
          'ws',
          'package.json'
        ),
        specifier: path.join(resourcesPath, 'app.asar', 'node_modules', 'ws')
      },
      { parent: path.join(resourcesPath, 'app', 'package.json'), specifier: 'ws' },
      {
        parent: path.join(
          resourcesPath,
          'app',
          'node_modules',
          'ws',
          'package.json'
        ),
        specifier: path.join(resourcesPath, 'app', 'node_modules', 'ws')
      }
    );
  }
  candidates.push(
    { parent: loader, specifier: path.join(process.cwd(), 'node_modules', 'ws') },
    { parent: loader, specifier: path.join(root, 'node_modules', 'ws') }
  );

  for (const candidate of candidates) {
    try {
      const implementation = unwrapWebSocketImplementation(
        createRequire(candidate.parent)(candidate.specifier)
      );
      if (implementation) return implementation;
    } catch {
      // Continue through the bounded layout list and finally try ESM import.
    }
  }

  try {
    const implementation = unwrapWebSocketImplementation(await import('ws'));
    if (implementation) return implementation;
  } catch {
    // Fall through to a stable, actionable startup error.
  }
  throw new Error(
    'No WebSocket implementation available; bundle ws or set VOKIE_PLUGIN_WS_MODULE_PATH'
  );
}

const invoked =
  process.argv[1] &&
  pathToFileURL(path.resolve(process.argv[1])).href === import.meta.url;
if (invoked)
  startWorker().catch((error) => {
    console.error('[AiPassport] Worker startup failed', error);
    process.exitCode = 1;
  });
