export const SERVICE_UUID: string;
export const CONTROL_UUID: string;
export const AUDIO_UUID: string;
export const INFO_UUID: string;
export const AUDIO_MAGIC: number;
export const AUDIO_VERSION: number;
export const AUDIO_HEADER_BYTES: number;
export const EMPTY_FINAL_SEQUENCE: number;
export const ADPCM_FRAME_BYTES: number;
export const ATT_MTU_OVERHEAD_BYTES: number;
export const MIN_ATT_MTU: number;
export const MIN_ATT_PAYLOAD_BYTES: number;
export const MAX_AUDIO_FRAGMENTS: number;
export const MAX_AUDIO_SEQUENCES_PER_SESSION: number;
export const MAX_MISSING_AUDIO_FRAMES: number;
export const MAX_CONTROL_PAYLOAD_BYTES: number;
export const MAX_DIAGNOSTIC_MESSAGE_BYTES: number;
export const MAX_DEVICE_FIELD_BYTES: number;
export const MAX_DEVICE_ERROR_MESSAGE_BYTES: number;
export const MAX_HELPER_LINE_BYTES: number;
export const MAX_AUDIO_PAYLOAD_BASE64_CHARS: number;
export const MAX_CONTROL_BASE64_CHARS: number;

export type HelperEvent =
  | { type: 'control'; payloadBase64: string }
  | {
      type: 'audio';
      sessionId: number;
      sequence: number;
      payloadBase64: string;
    }
  | {
      type: 'connected' | 'ready' | 'subscribed' | 'disconnected';
      deviceId?: string;
      deviceName?: string;
      preferredDeviceId?: string | null;
    }
  | { type: 'error' | 'diagnostic'; message: string; level?: string };

export function utf8ByteLength(value: unknown): number;
export function truncateUtf8(value: unknown, maxBytes?: number): string;
export function isCanonicalBase64(
  value: unknown,
  maxLength?: number,
  maxDecodedBytes?: number
): boolean;
export function parseHelperLine(
  line: string | Uint8Array | ArrayBuffer
): HelperEvent | null;
export function parseControlMessage(input: string | Uint8Array | ArrayBuffer): {
  v: 1;
  type: 'hello' | 'ptt_down' | 'ptt_up' | 'device_error';
  device?: string;
  fw?: string;
  codec?: string;
  sampleRate?: number;
  channels?: number;
  frameMs?: number;
  sessionId?: number;
  seq?: number;
  finalSequence?: number;
  message?: string;
};
export function parseAudioFragment(input: string | Uint8Array | ArrayBuffer): {
  sessionId: number;
  sequence: number;
  fragmentIndex: number;
  fragmentCount: number;
  payload: Uint8Array;
};

export class AudioFrameAssembler {
  constructor(options?: {
    maxFrames?: number;
    onDiagnostic?: (message: string) => void;
  });
  reset(): void;
  push(fragment: {
    sessionId: number;
    sequence: number;
    fragmentIndex: number;
    fragmentCount: number;
    payload: Uint8Array;
  }): { sessionId: number; sequence: number; payload: Uint8Array } | null;
}
