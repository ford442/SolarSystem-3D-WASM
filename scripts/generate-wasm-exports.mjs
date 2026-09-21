#!/usr/bin/env node
/**
 * Scan WasmExports.cpp for EMSCRIPTEN_KEEPALIVE exports and emit TypeScript
 * fragments for SolarSystem.d.ts (SolarSystemCwrap overloads + SolarSystemModule
 * `_Export` members) and wasmBridge.exports.ts.
 *
 * Raw module / cwrap types stay unbranded (`number`). QualityPreset / PlanetIndex
 * brands live on the wasmBridge façade only.
 *
 * Usage: node scripts/generate-wasm-exports.mjs [--check]
 *   --check  Exit 1 if generated output would differ from committed files.
 */
import { readFileSync, writeFileSync } from 'node:fs';
import { dirname, join } from 'node:path';
import { fileURLToPath } from 'node:url';

const root = join(dirname(fileURLToPath(import.meta.url)), '..');
const wasmExportsCpp = join(root, 'src/WasmExports.cpp');
const dtsPath = join(root, 'web/src/SolarSystem.d.ts');
const bridgeExportsPath = join(root, 'web/src/wasmBridge.exports.ts');
const exportedFunctionsPath = join(root, 'scripts/wasm-exports.json');
const bridgePath = join(root, 'web/src/wasmBridge.ts');

const CWRAP_BEGIN = '// BEGIN GENERATED CWARP OVERLOADS';
const CWRAP_END = '// END GENERATED CWARP OVERLOADS';
const MODULE_BEGIN = '// BEGIN GENERATED MODULE EXPORTS';
const MODULE_END = '// END GENERATED MODULE EXPORTS';

const EXPORT_RE = /EMSCRIPTEN_KEEPALIVE\s+([\w\s*]+?)\s+(\w+)\s*\(([^)]*)\)/g;

/** Map C types to cwrap arg/return types. */
function mapCType(cType) {
    const t = cType.replace(/\s+/g, ' ').trim();
    if (t === 'void') return { tsReturn: 'void', cwrapReturn: 'null' };
    if (t === 'int' || t === 'float' || t === 'double' || t === 'bool') {
        return { tsReturn: 'number', cwrapReturn: 'number' };
    }
    // A returned C string is handed to JS through cwrap's 'string' marshalling, which copies
    // it out with UTF8ToString. The raw Module._Export member is still a pointer (number) —
    // only the cwrap façade sees a string.
    if (t === 'const char*' || t === 'char*') {
        return { tsReturn: 'string', cwrapReturn: 'string', moduleReturn: 'number' };
    }
    if (t.endsWith('*')) return { tsReturn: 'number', cwrapReturn: 'number' };
    return { tsReturn: 'number', cwrapReturn: 'number' };
}

function parseParams(paramStr) {
    if (!paramStr.trim()) return [];
    return paramStr.split(',').map((p) => {
        const parts = p.trim().split(/\s+/);
        const cType = parts.slice(0, -1).join(' ') || parts[0];
        return mapCType(cType);
    });
}

function parseExports(source) {
    const exports = [];
    let match;
    while ((match = EXPORT_RE.exec(source)) !== null) {
        const [, returnType, name, params] = match;
        const ret = mapCType(returnType);
        const args = parseParams(params);
        exports.push({ name, ret, args });
    }
    return exports;
}

function toCamelCase(exportName) {
    return exportName[0].toLowerCase() + exportName.slice(1);
}

function generateCwrapOverload({ name, ret, args }) {
    const argTypes = args.map(() => "'number'").join(', ');
    const argTypesTuple = argTypes ? `[${argTypes}]` : '[]';
    return `  (
    ident: '${name}',
    returnType: ${ret.cwrapReturn === 'null' ? 'null' : `'${ret.cwrapReturn}'`},
    argTypes: ${argTypesTuple},
  ): (...args: number[]) => ${ret.tsReturn};`;
}

function generateCachedBinding({ name, ret, args }) {
    const key = toCamelCase(name);
    const argTypes = args.map(() => "'number'").join(', ');
    const returnType = ret.cwrapReturn === 'null' ? 'null' : `'${ret.cwrapReturn}'`;
    const tsReturn = ret.tsReturn;
    const callSig = args.length ? '(...args: number[])' : '()';
    return `    ${key}: cwrap('${name}', ${returnType}, [${argTypes}]) as ${callSig} => ${tsReturn},`;
}

function generateCachedExportType({ name, ret, args }) {
    const key = toCamelCase(name);
    const tsReturn = ret.tsReturn;
    if (args.length === 0) {
        return `    ${key}: () => ${tsReturn};`;
    }
    return `    ${key}: (...args: number[]) => ${tsReturn};`;
}

/** Raw `_Export` members on SolarSystemModule — numbers only, no façade brands. */
function generateModuleExport({ name, ret, args }) {
    const tsReturn = ret.moduleReturn ?? (ret.cwrapReturn === 'null' ? 'void' : 'number');
    if (args.length === 0) {
        return `  _${name}: () => ${tsReturn};`;
    }
    return `  _${name}: (...args: number[]) => ${tsReturn};`;
}

function escapeRegExp(value) {
    return value.replace(/[.*+?^${}()|[\]\\]/g, '\\$&');
}

function replaceMarkedSection(source, beginComment, endComment, inner) {
    const re = new RegExp(
        `[ \\t]*${escapeRegExp(beginComment)}[\\s\\S]*?[ \\t]*${escapeRegExp(endComment)}`,
    );
    if (!re.test(source)) {
        throw new Error(`Missing markers in SolarSystem.d.ts: ${beginComment} / ${endComment}`);
    }
    return source.replace(re, `  ${beginComment}\n${inner}\n  ${endComment}`);
}

/**
 * SolarSystem.d.ts / wasmBridge.exports.ts are regenerated wholesale, so they can never
 * drift from WasmExports.cpp. wasmBridge.ts's hand-written SolarSystemRuntime façade is
 * not regenerated — nothing stops a new export from landing in exports.ts without anyone
 * wiring it into the façade real code ever calls. Fail --check if that happens.
 */
function findUnwrappedExports(exports) {
    let bridgeSource;
    try {
        bridgeSource = readFileSync(bridgePath, 'utf8');
    } catch {
        return exports.map(({ name }) => name);
    }
    return exports
        .filter(({ name }) => !bridgeSource.includes(`exports.${toCamelCase(name)}`))
        .map(({ name }) => name);
}

function main() {
    const source = readFileSync(wasmExportsCpp, 'utf8');
    const exports = parseExports(source);
    if (exports.length === 0) {
        console.error('No EMSCRIPTEN_KEEPALIVE exports found in', wasmExportsCpp);
        process.exit(1);
    }

    const cwrapBlock = exports.map(generateCwrapOverload).join('\n');
    const moduleBlock = exports.map(generateModuleExport).join('\n');
    const cachedBindings = exports.map(generateCachedBinding).join('\n');
    const cachedTypes = exports.map(generateCachedExportType).join('\n');

    // -sEXPORTED_FUNCTIONS=@scripts/wasm-exports.json (CMakeLists.txt). '_main' is the
    // C entry point; the rest are EMSCRIPTEN_KEEPALIVE exports from WasmExports.cpp.
    const exportedFunctions = ['_main', ...exports.map(({ name }) => `_${name}`)];
    const generatedExportedFunctions = `${JSON.stringify(exportedFunctions, null, 2)}\n`;

    const generatedBridgeExports = `// AUTO-GENERATED by scripts/generate-wasm-exports.mjs — do not edit.
import type { SolarSystemCwrap } from './SolarSystem.js';

export interface CachedCwrapExports {
${cachedTypes}
}

export function createCachedCwrapExports(cwrap: SolarSystemCwrap): CachedCwrapExports {
    return {
${cachedBindings}
    };
}

export const EXPORT_COUNT = ${exports.length};
`;

    let dts = readFileSync(dtsPath, 'utf8');
    dts = replaceMarkedSection(dts, CWRAP_BEGIN, CWRAP_END, cwrapBlock);
    dts = replaceMarkedSection(dts, MODULE_BEGIN, MODULE_END, moduleBlock);

    const check = process.argv.includes('--check');

    if (check) {
        let existingBridgeExports = '';
        try {
            existingBridgeExports = readFileSync(bridgeExportsPath, 'utf8');
        } catch {
            existingBridgeExports = '';
        }
        let existingExportedFunctions = '';
        try {
            existingExportedFunctions = readFileSync(exportedFunctionsPath, 'utf8');
        } catch {
            existingExportedFunctions = '';
        }
        const dtsChanged = readFileSync(dtsPath, 'utf8') !== dts;
        const bridgeChanged = existingBridgeExports !== generatedBridgeExports;
        const exportedFunctionsChanged = existingExportedFunctions !== generatedExportedFunctions;
        if (dtsChanged || bridgeChanged || exportedFunctionsChanged) {
            console.error('Generated wasm export bindings are out of date. Run: npm run generate:wasm-exports');
            process.exit(1);
        }

        const unwrapped = findUnwrappedExports(exports);
        if (unwrapped.length > 0) {
            console.error(
                `SolarSystemRuntime (web/src/wasmBridge.ts) does not wrap ${unwrapped.length} generated export(s): ${unwrapped.join(', ')}\n` +
                'Every EMSCRIPTEN_KEEPALIVE export needs a call site in createSolarSystemRuntime(), or it is dead from JS.',
            );
            process.exit(1);
        }

        console.log(`OK: ${exports.length} exports in sync (cwrap + SolarSystemModule _Export members) and wrapped in SolarSystemRuntime`);
        return;
    }

    writeFileSync(dtsPath, dts);
    writeFileSync(bridgeExportsPath, generatedBridgeExports);
    writeFileSync(exportedFunctionsPath, generatedExportedFunctions);
    console.log(`Generated ${exports.length} export bindings → ${dtsPath}, ${bridgeExportsPath}, ${exportedFunctionsPath}`);
}

main();
