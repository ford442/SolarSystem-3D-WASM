import { copyFile, mkdir } from 'node:fs/promises';
import { dirname, resolve } from 'node:path';
import { fileURLToPath } from 'node:url';

const projectRoot = resolve(dirname(fileURLToPath(import.meta.url)), '..');
const basisSource = resolve(projectRoot, 'node_modules/three/examples/jsm/libs/basis');
const basisOutput = resolve(projectRoot, 'public/basis');

await mkdir(basisOutput, { recursive: true });
await Promise.all([
  copyFile(resolve(basisSource, 'basis_transcoder.js'), resolve(basisOutput, 'basis_transcoder.js')),
  copyFile(resolve(basisSource, 'basis_transcoder.wasm'), resolve(basisOutput, 'basis_transcoder.wasm')),
]);

console.log(`Prepared KTX2 transcoder assets in ${basisOutput}`);
