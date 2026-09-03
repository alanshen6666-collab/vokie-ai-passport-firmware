// IMA/DVI ADPCM step-size table (89 entries, indices 0..88).
const STEP_TABLE = [
  7, 8, 9, 10, 11, 12, 13, 14, 16, 17, 19, 21, 23, 25, 28, 31, 34, 37, 41, 45,
  50, 55, 60, 66, 73, 80, 88, 97, 107, 118, 130, 143, 157, 173, 190, 209,
  230, 253, 279, 307, 337, 371, 408, 449, 494, 544, 598, 658, 724, 796,
  876, 963, 1060, 1166, 1282, 1411, 1552, 1707, 1878, 2066, 2272, 2499,
  2749, 3024, 3327, 3660, 4026, 4428, 4871, 5358, 5894, 6484, 7132, 7845,
  8630, 9493, 10442, 11487, 12635, 13899, 15289, 16818, 18500, 20350,
  22385, 24623, 27086, 29794, 32767
];
const INDEX_TABLE = [-1, -1, -1, -1, 2, 4, 6, 8, -1, -1, -1, -1, 2, 4, 6, 8];

export const ADPCM_FRAME_BYTES = 166;

export function decodeImaAdpcmFrame(input) {
  if (!(input instanceof Uint8Array))
    throw new Error('Invalid ADPCM frame input');
  const payload = Buffer.from(input.buffer, input.byteOffset, input.byteLength);
  if (payload.length !== ADPCM_FRAME_BYTES)
    throw new Error(`Invalid ADPCM frame length: ${payload.length}`);
  const sampleCount = payload.readUInt16LE(3);
  if (sampleCount !== 320)
    throw new Error(`Invalid ADPCM sample count: ${sampleCount}; expected 320`);
  if (payload[5] !== 0) throw new Error('Invalid ADPCM reserved byte');
  if ((payload[ADPCM_FRAME_BYTES - 1] & 0xf0) !== 0)
    throw new Error('Invalid ADPCM padding nibble');
  const requiredNibbles = sampleCount - 1;
  if (Math.ceil(requiredNibbles / 2) > 160)
    throw new Error('ADPCM payload is too short');
  let predictor = payload.readInt16LE(0);
  if (payload[2] > 88) throw new Error(`Invalid ADPCM step index: ${payload[2]}`);
  let stepIndex = payload[2];
  const output = new Int16Array(sampleCount);
  output[0] = predictor;
  for (let sample = 1; sample < sampleCount; sample += 1) {
    const nibbleIndex = sample - 1;
    const byte = payload[6 + (nibbleIndex >> 1)];
    const nibble = nibbleIndex % 2 === 0 ? byte & 0x0f : byte >> 4;
    const step = STEP_TABLE[stepIndex];
    let delta = step >> 3;
    if (nibble & 4) delta += step;
    if (nibble & 2) delta += step >> 1;
    if (nibble & 1) delta += step >> 2;
    predictor += nibble & 8 ? -delta : delta;
    predictor = Math.max(-32768, Math.min(32767, predictor));
    stepIndex = Math.max(0, Math.min(88, stepIndex + INDEX_TABLE[nibble]));
    output[sample] = predictor;
  }
  return output;
}

export function silenceFrame() {
  return new Int16Array(320);
}
