// Run with: node --test scripts/generate-wasm-exports.test.mjs  (npm run test:scripts in web/)
import assert from 'node:assert/strict';
import { readFileSync } from 'node:fs';
import { test } from 'node:test';
import { dirname, join } from 'node:path';
import { fileURLToPath } from 'node:url';
import {
    findUnwrappedExports,
    generateCwrapOverload,
    generateModuleExport,
    parseExports,
} from './generate-wasm-exports.mjs';

const root = join(dirname(fileURLToPath(import.meta.url)), '..');

function parseOne(decl) {
    const [exp, ...rest] = parseExports(`extern "C" {\n${decl}\n}`);
    assert.equal(rest.length, 0);
    return exp;
}

test('parses return kind, parameter names and arity from the C declaration', () => {
    const exp = parseOne('EMSCRIPTEN_KEEPALIVE void SetCameraPose(double x, double y, double z, float yaw, float pitch) {}');
    assert.equal(exp.name, 'SetCameraPose');
    assert.equal(exp.ret.kind, 'void');
    assert.deepEqual(exp.args.map((a) => a.name), ['x', 'y', 'z', 'yaw', 'pitch']);
    assert.ok(exp.args.every((a) => a.kind === 'number'));
});

test('a char* argument is a string through cwrap but a pointer on the raw module', () => {
    const exp = parseOne('EMSCRIPTEN_KEEPALIVE int SetLabel(const char* text, int length) { return 0; }');
    assert.match(generateCwrapOverload(exp), /argTypes: \['string', 'number'\]/);
    assert.match(generateCwrapOverload(exp), /\(text: string, length: number\) => number/);
    assert.match(generateModuleExport(exp), /_SetLabel: \(text: number, length: number\) => number;/);
});

test('a returned char* is a string through cwrap and a number on the raw module', () => {
    const exp = parseOne('EMSCRIPTEN_KEEPALIVE const char* GetJson() { return ""; }');
    assert.match(generateCwrapOverload(exp), /returnType: 'string'/);
    assert.match(generateCwrapOverload(exp), /\(\) => string;/);
    assert.match(generateModuleExport(exp), /_GetJson: \(\) => number;/);
});

test('other pointers are heap addresses (numbers), whichever way the star is written', () => {
    for (const decl of [
        'EMSCRIPTEN_KEEPALIVE float* GetScratch() { return 0; }',
        'EMSCRIPTEN_KEEPALIVE float *GetScratch() { return 0; }',
    ]) {
        assert.equal(parseOne(decl).ret.kind, 'number', decl);
    }
    const exp = parseOne('EMSCRIPTEN_KEEPALIVE void Fill(float *out, const float* src) {}');
    assert.deepEqual(exp.args.map((a) => [a.name, a.kind]), [['out', 'number'], ['src', 'number']]);
});

test('multi-line parameter lists, (void) and bool/int mix', () => {
    const exp = parseOne(`EMSCRIPTEN_KEEPALIVE void SetRay(int hand, float ox,
                                     float oy, bool visible) {}`);
    assert.deepEqual(exp.args.map((a) => a.name), ['hand', 'ox', 'oy', 'visible']);
    assert.deepEqual(parseOne('EMSCRIPTEN_KEEPALIVE int Count(void) { return 0; }').args, []);
});

test('reserved or missing parameter names fall back to argN', () => {
    const exp = parseOne('EMSCRIPTEN_KEEPALIVE void F(int delete, int) {}');
    assert.deepEqual(exp.args.map((a) => a.name), ['arg0', 'arg1']);
});

test('types that cannot cross as a number fail loudly', () => {
    assert.throws(() => parseOne('EMSCRIPTEN_KEEPALIVE void F(int64_t big) {}'), /no cwrap mapping/);
    assert.throws(() => parseOne('EMSCRIPTEN_KEEPALIVE Vec3 F() { return {}; }'), /no cwrap mapping/);
});

test('prototypes and comments that mention the macro are not exports', () => {
    assert.deepEqual(parseExports('// EMSCRIPTEN_KEEPALIVE\nEMSCRIPTEN_KEEPALIVE void Proto();\n'), []);
});

test('a name exported twice is an error', () => {
    const src = 'EMSCRIPTEN_KEEPALIVE void A() {}\nEMSCRIPTEN_KEEPALIVE void A() {}\n';
    assert.throws(() => parseExports(src), /exported twice/);
});

test('every EMSCRIPTEN_KEEPALIVE in WasmExports.cpp is picked up', () => {
    const source = readFileSync(join(root, 'src/WasmExports.cpp'), 'utf8');
    const declared = source.match(/^\s*EMSCRIPTEN_KEEPALIVE\b/gm)?.length ?? 0;
    assert.ok(declared > 0);
    assert.equal(parseExports(source).length, declared);
});

test('an export counts as wrapped when the façade calls it, however the call is formatted', () => {
    const exports = parseExports(
        'EMSCRIPTEN_KEEPALIVE int GetCameraPosition() { return 0; }\n' +
            'EMSCRIPTEN_KEEPALIVE const char* GetShaderSourceFiles() { return ""; }\n',
    );
    const single = 'x: () => exports.getCameraPosition(), y: () => exports.getShaderSourceFiles().split("\\n")';
    assert.deepEqual(findUnwrappedExports(exports, single), []);
    // Prettier breaks long chains onto separate lines.
    const wrapped = 'x: exports.getCameraPosition,\n y: () =>\n exports\n .getShaderSourceFiles()\n .split("\\n")';
    assert.deepEqual(findUnwrappedExports(exports, wrapped), []);
});

test('a longer export name does not make a shorter one look wrapped', () => {
    const exports = parseExports('EMSCRIPTEN_KEEPALIVE float GetCameraPosition() { return 0; }\n');
    assert.deepEqual(findUnwrappedExports(exports, 'exports.getCameraPositionX()'), ['GetCameraPosition']);
});

test('every real export is wrapped by wasmBridge.ts as it is formatted today', () => {
    const source = readFileSync(join(root, 'src/WasmExports.cpp'), 'utf8');
    assert.deepEqual(findUnwrappedExports(parseExports(source)), []);
});
