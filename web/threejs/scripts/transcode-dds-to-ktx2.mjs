import { mkdir, readFile, writeFile } from 'node:fs/promises';
import { basename, dirname, extname, resolve } from 'node:path';
import { fileURLToPath } from 'node:url';
import {
  KHR_DF_MODEL_RGBSDA,
  KHR_DF_PRIMARIES_BT709,
  KHR_DF_TRANSFER_SRGB,
  VK_FORMAT_BC1_RGBA_SRGB_BLOCK,
  VK_FORMAT_BC2_SRGB_BLOCK,
  VK_FORMAT_BC3_SRGB_BLOCK,
  VK_FORMAT_R8G8B8A8_SRGB,
  createDefaultContainer,
  write,
} from '../node_modules/three/examples/jsm/libs/ktx-parse.module.js';

const scriptDirectory = dirname(fileURLToPath(import.meta.url));
const projectRoot = resolve(scriptDirectory, '..');
const repositoryRoot = resolve(projectRoot, '../..');
const defaultInputs = [
  'Mercury_Diffuse_Low.dds',
  'Venus_Diffuse_Low.dds',
  'Earth_Day_Diffuse_Low.dds',
  'Mars_Diffuse_Low.dds',
].map((name) => resolve(repositoryRoot, 'resource/textures_low', name));

function readOption(name) {
  const index = process.argv.indexOf(name);
  return index >= 0 ? process.argv[index + 1] : undefined;
}

const outputDirectory = resolve(projectRoot, readOption('--output') ?? 'public/textures/ktx2');
const cliInputs = process.argv.slice(2).filter((value, index, args) =>
  !value.startsWith('--') && args[index - 1] !== '--output');
const inputs = cliInputs.length > 0 ? cliInputs.map((path) => resolve(path)) : defaultInputs;

function fourCC(view, offset) {
  return String.fromCharCode(view.getUint8(offset), view.getUint8(offset + 1), view.getUint8(offset + 2), view.getUint8(offset + 3));
}

function channelFromMask(value, mask) {
  if (mask === 0) return 255;
  let shift = 0;
  while (((mask >>> shift) & 1) === 0) shift++;
  const maximum = mask >>> shift;
  return Math.round((((value & mask) >>> shift) / maximum) * 255);
}

function parseDDS(bytes) {
  const view = new DataView(bytes.buffer, bytes.byteOffset, bytes.byteLength);
  if (view.getUint32(0, true) !== 0x20534444 || view.getUint32(4, true) !== 124) {
    throw new Error('Invalid DDS header');
  }

  const height = view.getUint32(12, true);
  const width = view.getUint32(16, true);
  const mipCount = Math.max(1, view.getUint32(28, true));
  const pixelFlags = view.getUint32(80, true);
  const format = (pixelFlags & 0x4) !== 0 ? fourCC(view, 84) : 'RGBA';
  const bitsPerPixel = view.getUint32(88, true);
  const masks = [92, 96, 100, 104].map((offset) => view.getUint32(offset, true));

  if (format === 'DX10') throw new Error('DX10 DDS headers require toktx; this script supports legacy DXT1/3/5 and 32-bit RGBA DDS');

  const formatInfo = {
    DXT1: { blockBytes: 8, vkFormat: VK_FORMAT_BC1_RGBA_SRGB_BLOCK },
    DXT3: { blockBytes: 16, vkFormat: VK_FORMAT_BC2_SRGB_BLOCK },
    DXT5: { blockBytes: 16, vkFormat: VK_FORMAT_BC3_SRGB_BLOCK },
  }[format];

  if (!formatInfo && (format !== 'RGBA' || bitsPerPixel !== 32)) {
    throw new Error(`Unsupported DDS pixel format: ${format}, ${bitsPerPixel} bpp`);
  }

  const levels = [];
  let offset = 128;
  for (let level = 0; level < mipCount; level++) {
    const levelWidth = Math.max(1, width >> level);
    const levelHeight = Math.max(1, height >> level);
    const byteLength = formatInfo
      ? Math.max(1, Math.ceil(levelWidth / 4)) * Math.max(1, Math.ceil(levelHeight / 4)) * formatInfo.blockBytes
      : levelWidth * levelHeight * 4;
    if (offset + byteLength > bytes.byteLength) throw new Error(`DDS mip ${level} exceeds file length`);

    let levelData = bytes.slice(offset, offset + byteLength);
    if (!formatInfo) {
      const converted = new Uint8Array(byteLength);
      const pixels = new DataView(levelData.buffer, levelData.byteOffset, levelData.byteLength);
      for (let pixel = 0; pixel < levelWidth * levelHeight; pixel++) {
        const value = pixels.getUint32(pixel * 4, true);
        converted[pixel * 4] = channelFromMask(value, masks[0]);
        converted[pixel * 4 + 1] = channelFromMask(value, masks[1]);
        converted[pixel * 4 + 2] = channelFromMask(value, masks[2]);
        converted[pixel * 4 + 3] = channelFromMask(value, masks[3]);
      }
      levelData = converted;
    }

    levels.push({ levelData, uncompressedByteLength: byteLength });
    offset += byteLength;
  }

  return { width, height, levels, formatInfo };
}

function createKTX2(dds) {
  const container = createDefaultContainer();
  container.vkFormat = dds.formatInfo?.vkFormat ?? VK_FORMAT_R8G8B8A8_SRGB;
  container.typeSize = 1;
  container.pixelWidth = dds.width;
  container.pixelHeight = dds.height;
  container.pixelDepth = 0;
  container.layerCount = 0;
  container.faceCount = 1;
  container.levelCount = dds.levels.length;
  container.levels = dds.levels;

  const descriptor = container.dataFormatDescriptor[0];
  descriptor.colorModel = KHR_DF_MODEL_RGBSDA;
  descriptor.colorPrimaries = KHR_DF_PRIMARIES_BT709;
  descriptor.transferFunction = KHR_DF_TRANSFER_SRGB;
  descriptor.texelBlockDimension = dds.formatInfo ? [3, 3, 0, 0] : [0, 0, 0, 0];
  descriptor.bytesPlane[0] = dds.formatInfo?.blockBytes ?? 4;
  return write(container);
}

await mkdir(outputDirectory, { recursive: true });
for (const input of inputs) {
  if (extname(input).toLowerCase() !== '.dds') throw new Error(`Expected a .dds input: ${input}`);
  const dds = parseDDS(new Uint8Array(await readFile(input)));
  const outputName = basename(input, extname(input)).replace(/_Low$/, '') + '.ktx2';
  const output = resolve(outputDirectory, outputName);
  await writeFile(output, createKTX2(dds));
  console.log(`${input} -> ${output} (${dds.width}x${dds.height}, ${dds.levels.length} mip level(s))`);
}
