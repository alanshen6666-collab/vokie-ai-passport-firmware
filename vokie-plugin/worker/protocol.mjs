export const SERVICE_UUID = '7f0e0001-6a7b-4b6f-9d1a-564f4b494500';
export const CONTROL_UUID = '7f0e0002-6a7b-4b6f-9d1a-564f4b494500';
export const AUDIO_UUID = '7f0e0003-6a7b-4b6f-9d1a-564f4b494500';
export const INFO_UUID = '7f0e0004-6a7b-4b6f-9d1a-564f4b494500';
export const AUDIO_MAGIC = 0x5041;
export const AUDIO_VERSION = 1;
export const AUDIO_HEADER_BYTES = 16;
export const EMPTY_FINAL_SEQUENCE = 0xffffffff;
export const ADPCM_FRAME_BYTES = 166;
export const ATT_MTU_OVERHEAD_BYTES = 3;
// V1 requires enough ATT payload for the complete 166-byte ADPCM frame plus
// the 16-byte fragment envelope.  This also keeps the common path to one
// notification while retaining the fragment envelope for future revisions.
export const MIN_ATT_MTU = 185;
export const MIN_ATT_PAYLOAD_BYTES = MIN_ATT_MTU - ATT_MTU_OVERHEAD_BYTES;
export const MAX_AUDIO_FRAGMENTS = 64;
export const MAX_AUDIO_SEQUENCES_PER_SESSION = 15_000;
// A dropped BLE frame can be concealed with silence, but an unbounded sequence
// gap would turn a single malformed release into minutes of synthetic audio.
export const MAX_MISSING_AUDIO_FRAMES = 100;
// The V1 peripheral requires ATT_MTU >= 185, leaving 182 bytes for a control
// characteristic value after the three-byte ATT notification/write overhead.
export const MAX_CONTROL_PAYLOAD_BYTES = MIN_ATT_PAYLOAD_BYTES;
export const MAX_DIAGNOSTIC_MESSAGE_BYTES = 128;
export const MAX_DEVICE_FIELD_BYTES = 256;
export const MAX_HELPER_LINE_BYTES = 16 * 1024;
export const MAX_DEVICE_ERROR_MESSAGE_BYTES = 128;
// 166 bytes encoded as canonical base64 occupy 224 characters. The helper
// emits one complete ADPCM frame per event, so larger payloads are never valid.
export const MAX_AUDIO_PAYLOAD_BASE64_CHARS = Math.ceil(ADPCM_FRAME_BYTES / 3) * 4;
export const MAX_CONTROL_BASE64_CHARS = Math.ceil(MAX_CONTROL_PAYLOAD_BYTES / 3) * 4;

const fatalUtf8Decoder = new TextDecoder('utf-8', { fatal: true });

/**
 * Convert an IPC value to bytes without allowing Buffer's permissive coercion
 * rules (for example, Buffer.from(123) allocates a new zero-filled buffer).
 * The helper boundary is binary, so accepting those coercions would make an
 * invalid event indistinguishable from an empty/valid one.
 */
function toBytes(input) {
  if (typeof input === 'string') return Buffer.from(input, 'utf8');
  if (input instanceof Uint8Array) {
    return Buffer.from(input.buffer, input.byteOffset, input.byteLength);
  }
  if (input instanceof ArrayBuffer) return Buffer.from(input);
  throw new TypeError('Expected a string or byte buffer');
}

function decodeUtf8(input) {
  const bytes = toBytes(input);
  try {
    return fatalUtf8Decoder.decode(bytes);
  } catch {
    throw new Error('Invalid UTF-8');
  }
}

function isBase64(
  value,
  maxLength = MAX_HELPER_LINE_BYTES,
  maxDecodedBytes = Infinity
) {
  if (
    typeof value !== 'string' ||
    value.length === 0 ||
    value.length > maxLength ||
    value.length % 4 !== 0
  )
    return false;
  if (
    !/^(?:[A-Za-z0-9+/]{4})*(?:[A-Za-z0-9+/]{2}==|[A-Za-z0-9+/]{3}=)?$/.test(value)
  )
    return false;
  try {
    const decoded = Buffer.from(value, 'base64');
    return (
      decoded.byteLength <= maxDecodedBytes && decoded.toString('base64') === value
    );
  } catch {
    return false;
  }
}

/**
 * Validate canonical RFC 4648 base64 at call sites that receive a helper
 * event directly. `Buffer.from(value, 'base64')` is intentionally permissive
 * (it silently ignores invalid characters), so using it without this check
 * could turn a malformed event into a valid control or audio payload.
 */
export function isCanonicalBase64(
  value,
  maxLength = MAX_HELPER_LINE_BYTES,
  maxDecodedBytes = Infinity
) {
  return isBase64(value, maxLength, maxDecodedBytes);
}

export function utf8ByteLength(value) {
  return Buffer.byteLength(String(value), 'utf8');
}

/** Return a valid UTF-8 prefix without splitting a multi-byte scalar. */
export function truncateUtf8(value, maxBytes = MAX_DIAGNOSTIC_MESSAGE_BYTES) {
  const text = String(value);
  if (maxBytes <= 0) return '';
  const bytes = Buffer.from(text, 'utf8');
  if (bytes.byteLength <= maxBytes) return text;
  let end = maxBytes;
  // Back up until the prefix is accepted by a fatal UTF-8 decoder. This keeps
  // a diagnostic from gaining a replacement character at the truncation edge.
  const decoder = new TextDecoder('utf-8', { fatal: true });
  while (end > 0) {
    try {
      return decoder.decode(bytes.subarray(0, end));
    } catch {
      end -= 1;
    }
  }
  return '';
}

function isBoundedString(value, maxBytes = MAX_DEVICE_FIELD_BYTES) {
  return typeof value === 'string' && utf8ByteLength(value) <= maxBytes;
}

export function parseHelperLine(line) {
  let text;
  try {
    const bytes = toBytes(line);
    if (bytes.byteLength > MAX_HELPER_LINE_BYTES) return null;
    text = decodeUtf8(bytes);
  } catch {
    return null;
  }
  let value;
  try {
    value = JSON.parse(text);
  } catch {
    return null;
  }
  if (!value || typeof value !== 'object' || Array.isArray(value)) return null;
  if (
    value.type === 'control' &&
    isBase64(
      value.payloadBase64,
      MAX_CONTROL_BASE64_CHARS,
      MAX_CONTROL_PAYLOAD_BYTES
    )
  ) {
    return value;
  }
  if (
    value.type === 'audio' &&
    Number.isSafeInteger(value.sessionId) &&
    value.sessionId >= 0 &&
    value.sessionId <= 0xffffffff &&
    Number.isSafeInteger(value.sequence) &&
    value.sequence >= 0 &&
    value.sequence <= 0xffffffff &&
    isBase64(
      value.payloadBase64,
      MAX_AUDIO_PAYLOAD_BASE64_CHARS,
      ADPCM_FRAME_BYTES
    ) &&
    Buffer.from(value.payloadBase64, 'base64').byteLength === ADPCM_FRAME_BYTES
  ) {
    return value;
  }
  if (['connected', 'ready', 'subscribed', 'disconnected', 'discovered'].includes(value.type)) {
    if (
      (value.deviceId !== undefined && !isBoundedString(value.deviceId)) ||
      (value.deviceName !== undefined && !isBoundedString(value.deviceName)) ||
      (value.preferredDeviceId !== undefined &&
        value.preferredDeviceId !== null &&
        !isBoundedString(value.preferredDeviceId))
    )
      return null;
    return value;
  }
  if (
    (value.type === 'error' || value.type === 'diagnostic') &&
    isBoundedString(value.message, MAX_DIAGNOSTIC_MESSAGE_BYTES)
  )
    return value;
  return null;
}

export function parseControlMessage(input) {
  let bytes;
  try {
    bytes = toBytes(input);
  } catch {
    throw new Error('Invalid control payload');
  }
  if (bytes.length === 0 || bytes.length > MAX_CONTROL_PAYLOAD_BYTES)
    throw new Error('Control payload exceeds size limit');
  let value;
  try {
    value = JSON.parse(decodeUtf8(bytes));
  } catch {
    // Keep malformed UTF-8 and malformed JSON on the same protocol error path;
    // callers must never receive a replacement-character control message.
    throw new Error('Invalid control JSON');
  }
  if (!value || typeof value !== 'object' || Array.isArray(value))
    throw new Error('Invalid control object');
  if (value.v !== 1)
    throw new Error(`Unsupported control version: ${String(value.v)}`);
  if (value.type === 'hello') {
    if (value.device !== 'ai-passport') throw new Error('Unsupported device');
    if (
      !isBoundedString(value.fw, MAX_DEVICE_FIELD_BYTES) ||
      value.fw.trim().length === 0
    )
      throw new Error('Invalid firmware version');
    if (value.codec !== 'ima-adpcm') throw new Error('Unsupported audio codec');
    if (value.sampleRate !== 16000 || value.channels !== 1 || value.frameMs !== 20)
      throw new Error('Unsupported audio configuration');
    return value;
  }
  if (value.type === 'device_error') {
    const sessionId = readUInt32(value.sessionId, 'sessionId');
    if (
      value.message !== undefined &&
      !isBoundedString(value.message, MAX_DEVICE_ERROR_MESSAGE_BYTES)
    ) {
      throw new Error('Invalid device error message');
    }
    return { ...value, sessionId };
  }
  if (value.type === 'button_event') {
    if (!['down', 'ok'].includes(value.button))
      throw new Error('Unsupported button');
    if (!['click', 'long'].includes(value.event))
      throw new Error('Unsupported button event');
    if (
      value.durationMs !== undefined &&
      (!Number.isSafeInteger(value.durationMs) ||
        value.durationMs < 0 ||
        value.durationMs > 60000)
    )
      throw new Error('Invalid button duration');
    if (
      value.seq !== undefined &&
      (!Number.isSafeInteger(value.seq) || value.seq < 0 || value.seq > 0xffffffff)
    )
      throw new Error('Invalid button sequence');
    return value;
  }
  if (value.type !== 'ptt_down' && value.type !== 'ptt_up')
    throw new Error(`Unsupported control type: ${String(value.type)}`);
  const sessionId = readUInt32(value.sessionId, 'sessionId');
  const seq = readUInt32(value.seq, 'seq');
  if (value.type === 'ptt_down') return { ...value, sessionId, seq };
  return {
    ...value,
    sessionId,
    seq,
    finalSequence: readUInt32(value.finalSequence, 'finalSequence')
  };
}

export function parseAudioFragment(input) {
  let bytes;
  try {
    bytes = toBytes(input);
  } catch {
    throw new Error('Invalid audio fragment payload');
  }
  if (bytes.length < AUDIO_HEADER_BYTES) throw new Error('Audio fragment too short');
  if (bytes.readUInt16LE(0) !== AUDIO_MAGIC) throw new Error('Invalid audio magic');
  if (bytes[2] !== AUDIO_VERSION) throw new Error('Unsupported audio version');
  if (bytes[3] !== 0) throw new Error('Invalid audio reserved byte');
  const fragmentIndex = bytes[12];
  const fragmentCount = bytes[13];
  const payloadLength = bytes.readUInt16LE(14);
  const sequence = bytes.readUInt32LE(8);
  if (
    !fragmentCount ||
    fragmentCount > MAX_AUDIO_FRAGMENTS ||
    fragmentIndex >= fragmentCount
  )
    throw new Error('Invalid audio fragment index');
  if (sequence >= MAX_AUDIO_SEQUENCES_PER_SESSION)
    throw new Error('Invalid audio sequence');
  if (payloadLength === 0 || payloadLength > ADPCM_FRAME_BYTES)
    throw new Error('Invalid audio fragment payload length');
  if (bytes.length !== AUDIO_HEADER_BYTES + payloadLength)
    throw new Error('Audio fragment length mismatch');
  return {
    sessionId: bytes.readUInt32LE(4),
    sequence,
    fragmentIndex,
    fragmentCount,
    payload: Buffer.from(bytes.subarray(AUDIO_HEADER_BYTES))
  };
}

export class AudioFrameAssembler {
  constructor({ maxFrames = 64, onDiagnostic = () => {} } = {}) {
    this.maxFrames = Number.isSafeInteger(maxFrames)
      ? Math.max(1, Math.min(MAX_AUDIO_FRAGMENTS, maxFrames))
      : 64;
    this.onDiagnostic = onDiagnostic;
    this.frames = new Map();
    this.completed = new Set();
  }

  reset() {
    this.frames.clear();
    this.completed.clear();
  }

  push(fragment) {
    if (
      !fragment ||
      !Number.isSafeInteger(fragment.sessionId) ||
      fragment.sessionId < 0 ||
      fragment.sessionId > 0xffffffff ||
      !Number.isSafeInteger(fragment.sequence) ||
      fragment.sequence < 0 ||
      fragment.sequence > 0xffffffff ||
      !Number.isInteger(fragment.fragmentCount) ||
      fragment.fragmentCount < 1 ||
      fragment.fragmentCount > MAX_AUDIO_FRAGMENTS ||
      !Number.isInteger(fragment.fragmentIndex) ||
      fragment.fragmentIndex < 0 ||
      fragment.fragmentIndex >= fragment.fragmentCount ||
      !(fragment.payload instanceof Uint8Array) ||
      fragment.payload.byteLength < 1 ||
      fragment.payload.byteLength > ADPCM_FRAME_BYTES
    ) {
      this.onDiagnostic('invalid audio fragment object');
      return null;
    }
    const key = `${fragment.sessionId}:${fragment.sequence}`;
    if (this.completed.has(key)) return null;
    let frame = this.frames.get(key);
    if (!frame) {
      if (this.frames.size >= this.maxFrames) {
        const oldest = this.frames.keys().next().value;
        if (oldest !== undefined) {
          this.frames.delete(oldest);
          this.onDiagnostic(`audio frame evicted before completion: ${oldest}`);
        }
      }
      frame = {
        sessionId: fragment.sessionId,
        sequence: fragment.sequence,
        fragmentCount: fragment.fragmentCount,
        parts: new Array(fragment.fragmentCount),
        received: 0,
        totalBytes: 0
      };
      this.frames.set(key, frame);
    }
    if (frame.fragmentCount !== fragment.fragmentCount) {
      this.onDiagnostic(`audio fragment count changed: ${key}`);
      return null;
    }
    if (!frame.parts[fragment.fragmentIndex]) {
      if (frame.totalBytes + fragment.payload.byteLength > ADPCM_FRAME_BYTES) {
        this.frames.delete(key);
        this.onDiagnostic(`audio frame exceeds ADPCM payload limit: ${key}`);
        return null;
      }
      // Keep ownership inside the assembler. Callers often reuse a BLE read
      // buffer immediately after dispatching a fragment.
      frame.parts[fragment.fragmentIndex] = Buffer.from(
        fragment.payload.buffer,
        fragment.payload.byteOffset,
        fragment.payload.byteLength
      );
      frame.totalBytes += fragment.payload.byteLength;
      frame.received += 1;
    }
    if (frame.received !== frame.fragmentCount) return null;
    this.frames.delete(key);
    if (frame.totalBytes !== ADPCM_FRAME_BYTES) {
      this.onDiagnostic(`incomplete ADPCM frame payload: ${key}`);
      return null;
    }
    this.completed.add(key);
    while (this.completed.size > this.maxFrames * 2) {
      const oldestCompleted = this.completed.values().next().value;
      if (oldestCompleted !== undefined) this.completed.delete(oldestCompleted);
    }
    return {
      sessionId: frame.sessionId,
      sequence: frame.sequence,
      payload: Buffer.concat(frame.parts)
    };
  }
}

function readUInt32(value, name) {
  if (!Number.isInteger(value) || value < 0 || value > 0xffffffff)
    throw new Error(`Invalid ${name}`);
  return value;
}
