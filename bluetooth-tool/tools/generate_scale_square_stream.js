const fs = require('fs/promises');
const path = require('path');

const AUDIO_SAMPLE_RATE = 8000;
const AUDIO_SAMPLES_PER_PKT = 200;
const AUDIO_PKT_START_BYTE = 0xAA;
const AUDIO_PKT_TOTAL_SIZE = 109;
const AUDIO_PKT_PAYLOAD_LEN = 106;
const AUDIO_PKT_AUDIO_OFFSET = 8;
const DEFAULT_AUDIO_AMPLITUDE = 8000;
const DEFAULT_NOTES = [
  { name: 'C5', frequency: 523.2511, amplitude: 12000 },
  { name: 'D5', frequency: 587.3295 },
  { name: 'E5', frequency: 659.2551 },
  { name: 'F5', frequency: 698.4565 },
  { name: 'G5', frequency: 783.9909 },
  { name: 'A5', frequency: 880.0 },
  { name: 'B5', frequency: 987.7666 },
  { name: 'C6', frequency: 1046.5023 },
];

const NOTE_LOOKUP = new Map(
  DEFAULT_NOTES.map((note) => [note.name.toUpperCase(), note])
);

const STEP_TABLE = [
  7, 8, 9, 10, 11, 12, 13, 14, 16, 17,
  19, 21, 23, 25, 28, 31, 34, 37, 41, 45,
  50, 55, 60, 66, 73, 80, 88, 97, 107, 118,
  130, 143, 157, 173, 190, 209, 230, 253, 279, 307,
  337, 371, 408, 449, 494, 544, 598, 658, 724, 796,
  876, 963, 1060, 1166, 1282, 1411, 1552, 1707, 1878, 2066,
  2272, 2499, 2749, 3024, 3327, 3660, 4026, 4428, 4871, 5358,
  5894, 6484, 7132, 7845, 8630, 9493, 10442, 11487, 12635, 13899,
  15289, 16818, 18500, 20350, 22385, 24623, 27086, 29794, 32767,
];

const INDEX_TABLE = [-1, -1, -1, -1, 2, 4, 6, 8, -1, -1, -1, -1, 2, 4, 6, 8];

function clamp(value, min, max) {
  return Math.max(min, Math.min(max, value));
}

function adpcmDecodeNibble(nibble, state) {
  const step = STEP_TABLE[state.stepIndex];
  let diff = step >> 3;

  if (nibble & 4) diff += step;
  if (nibble & 2) diff += step >> 1;
  if (nibble & 1) diff += step >> 2;

  if (nibble & 8) {
    state.predictedSample -= diff;
  } else {
    state.predictedSample += diff;
  }

  state.predictedSample = clamp(state.predictedSample, -32768, 32767);
  state.stepIndex = clamp(state.stepIndex + INDEX_TABLE[nibble & 0x0F], 0, 88);
  return state.predictedSample;
}

function adpcmEncodeSample(sample, state) {
  const step = STEP_TABLE[state.stepIndex];
  let diff = sample - state.predictedSample;
  let nibble = 0;

  if (diff < 0) {
    nibble = 8;
    diff = -diff;
  }

  if (diff >= step) {
    nibble |= 4;
    diff -= step;
  }
  if (diff >= (step >> 1)) {
    nibble |= 2;
    diff -= step >> 1;
  }
  if (diff >= (step >> 2)) {
    nibble |= 1;
  }

  adpcmDecodeNibble(nibble, state);
  return nibble;
}

function checksum(bytes) {
  let value = 0;
  for (let i = 0; i < bytes.length - 1; i += 1) {
    value ^= bytes[i];
  }
  return value & 0xFF;
}

function buildAudioPacket(frequency, sequence, amplitude = DEFAULT_AUDIO_AMPLITUDE) {
  const packet = new Uint8Array(AUDIO_PKT_TOTAL_SIZE);
  const pcm = new Int16Array(AUDIO_SAMPLES_PER_PKT);
  const encoder = { predictedSample: 0, stepIndex: 0 };

  for (let i = 0; i < AUDIO_SAMPLES_PER_PKT; i += 1) {
    const t = i / AUDIO_SAMPLE_RATE;
    const envelope = Math.sin((Math.PI * i) / AUDIO_SAMPLES_PER_PKT);
    pcm[i] = Math.round(amplitude * envelope * Math.sin(2 * Math.PI * frequency * t));
  }

  packet[0] = AUDIO_PKT_START_BYTE;
  packet[1] = AUDIO_PKT_PAYLOAD_LEN & 0xFF;
  packet[2] = (AUDIO_PKT_PAYLOAD_LEN >> 8) & 0xFF;
  packet[3] = 0;
  packet[4] = sequence & 0xFF;
  packet[5] = 0;
  packet[6] = 0;
  packet[7] = 0;

  for (let i = 0; i < AUDIO_SAMPLES_PER_PKT; i += 2) {
    const nibbleLo = adpcmEncodeSample(pcm[i], encoder);
    const nibbleHi = adpcmEncodeSample(pcm[i + 1], encoder);
    packet[AUDIO_PKT_AUDIO_OFFSET + (i >> 1)] = ((nibbleHi & 0x0F) << 4) | (nibbleLo & 0x0F);
  }

  packet[AUDIO_PKT_TOTAL_SIZE - 1] = checksum(packet);
  return Array.from(packet);
}

function formatCsv(bytes, valuesPerLine = 32) {
  const tokens = bytes.map((value) => value.toString(16).padStart(2, '0').toUpperCase());
  const lines = [];

  for (let i = 0; i < tokens.length; i += valuesPerLine) {
    lines.push(tokens.slice(i, i + valuesPerLine).join(','));
  }

  return `${lines.join('\n')}\n`;
}

function sanitizeFilePart(value) {
  return value.toLowerCase().replace(/[^a-z0-9]+/g, '_');
}

function parseRequestedNotes(argv) {
  if (argv.length === 0) {
    return DEFAULT_NOTES;
  }

  return argv.map((noteName) => {
    const note = NOTE_LOOKUP.get(noteName.toUpperCase());

    if (!note) {
      throw new Error(
        `Unknown note '${noteName}'. Expected one of: ${DEFAULT_NOTES.map((item) => item.name).join(', ')}`
      );
    }

    return note;
  });
}

async function main() {
  const notes = parseRequestedNotes(process.argv.slice(2));
  const outputName = notes.length === 1
    ? `${sanitizeFilePart(notes[0].name)}_note_audio_stream.csv`
    : 'c_major_scale_audio_stream.csv';
  const outPath = path.resolve(
    __dirname,
    '..',
    'assets',
    outputName
  );

  const stream = [];
  let audioSequence = 0;

  for (const note of notes) {
    stream.push(...buildAudioPacket(note.frequency, audioSequence, note.amplitude || DEFAULT_AUDIO_AMPLITUDE));
    audioSequence = (audioSequence + 1) & 0xFF;
  }

  await fs.writeFile(outPath, formatCsv(stream), 'utf8');

  const summary = {
    output: outPath,
    totalBytes: stream.length,
    audioPackets: notes.length,
  };

  process.stdout.write(`${JSON.stringify(summary, null, 2)}\n`);
}

main().catch((error) => {
  console.error(error);
  process.exitCode = 1;
});