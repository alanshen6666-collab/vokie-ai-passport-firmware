export const ADPCM_FRAME_BYTES: number;
export function decodeImaAdpcmFrame(input: string | Uint8Array | ArrayBuffer): Int16Array;
export function silenceFrame(): Int16Array;
