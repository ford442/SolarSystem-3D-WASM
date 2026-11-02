#!/usr/bin/env node
/**
 * Scan WasmExports.cpp for EMSCRIPTEN_KEEPALIVE exports and emit TypeScript
 * fragments for SolarSystem.d.ts (SolarSystemCwrap overloads + SolarSystemModule
 * `_Export` members) and wasmBridge.exports.ts.
 *
 * Arity, parameter names and argument/return kinds come from the C declaration, so a new
 * export needs no parallel TypeScript edit: `bool`/`int`/`float`/`double` and other pointers
 * are `number`, a `char*` is a `string` through cwrap. Raw module / cwrap types stay
 * unbranded; QualityPreset / PlanetIndex brands live on the wasmBridge façade only.
 *
 * Usage: node scripts/generate-wasm-exports.mjs [--check]
 *   --check  Exit 1 if generated output would differ from committed files.
 */
import { readFileSync, writeFileSync } from 'node:fs';
import { dirname, join } from 'node:path';
import { fileURLToPath, pathToFileURL } from 'node:url';

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

// `EMSCRIPTEN_KEEPALIVE <return type> <name>(<params>) {` — the body brace keeps a prototype or a
// mention in a comment from counting as an export. Lazy return-type match, so `const char* Foo()`
// and `float *Foo()` both split correctly.
const EXPORT_RE = /EMSCRIPTEN_KEEPALIVE\s+([\w\s*]+?)\s*\b(\w+)\s*\(([^)]*)\)\s*\{/g;

/** C scalar types that cross the wasm boundary as a JS number (wasm32: `long` and `size_t` are 32-bit). */
const NUMBER_TYPES = new Set([
    'bool', 'char', 'signed char', 'unsigned char', 'short', 'unsigned short', 'int', 'unsigned',
    'unsigned int', 'long', 'unsigned long', 'float', 'double', 'size_t',
    'int8_t', 'uint8_t', 'int16_t', 'uint16_t', 'int32_t', 'uint32_t',
]);

const TS_RESERVED = new Set([
    'await', 'break', 'case', 'catch', 'class', 'const', 'continue', 'debugger', 'default', 'delete', 'do',
    'else', 'enum', 'export', 'extends', 'false', 'finally', 'for', 'function', 'if', 'implements', 'import',
    'in', 'instanceof', 'interface', 'let', 'new', 'null', 'package', 'private', 'protected', 'public',
    'return', 'static', 'super', 'switch', 'this', 'throw', 'true', 'try', 'typeof', 'var', 'void', 'while',
    'with', 'yield',
]);

/** `const char *` → `char*`: drop qualifiers and glue the stars to the type. */
function normalizeCType(cType) {
    return cType
        .replace(/\b(const|volatile|struct|enum)\b/g, ' ')
        .replace(/\s*\*\s*/g, '*')
        .replace(/\s+/g, ' ')
        .trim();
}

/**
 * Classify a C type for cwrap. `kind` is what cwrap marshals: 'number', or 'string' for a
 * `char*` (cwrap copies a JS string in / a returned C string out with UTF8 helpers). Any other
 * pointer is a heap address, so a number. 64-bit integers and by-value structs cannot cross
 * the boundary as numbers, so they fail the generator instead of silently becoming `number`.
 */
function classifyCType(cType, where) {
    const t = normalizeCType(cType);
    if (t === 'void') return { cType: t, kind: 'void' };
    if (t === 'char*') return { cType: t, kind: 'string' };
    if (t.endsWith('*')) return { cType: t, kind: 'number' };
    if (NUMBER_TYPES.has(t)) return { cType: t, kind: 'number' };
    throw new Error(`${where}: C type '${cType.trim()}' has no cwrap mapping (add it to NUMBER_TYPES if it is a 32-bit scalar)`);
}

function parseParams(paramStr, exportName) {
    const trimmed = paramStr.trim();
    if (!trimmed || trimmed === 'void') return [];
    const used = new Set();
    return trimmed.split(',').map((raw, i) => {
        const p = raw.replace(/=.*$/s, '').trim();
        // The parameter name is the trailing identifier, unless the whole thing is just a type.
        const m = /^(.*?[\s*&])([A-Za-z_]\w*)$/s.exec(p);
        const rawName = m && !NUMBER_TYPES.has(p) ? m[2] : '';
        const cType = m && rawName ? m[1] : p;
        let name = rawName;
        if (!name || TS_RESERVED.has(name) || used.has(name)) name = `arg${i}`;
        used.add(name);
        return { name, ...classifyCType(cType, `${exportName} parameter ${i + 1}`) };
    });
}

function parseExports(source) {
    const exports = [];
    const seen = new Set();
    for (const match of source.matchAll(EXPORT_RE)) {
        const [, returnType, name, params] = match;
        if (seen.has(name)) throw new Error(`${name} is exported twice`);
        seen.add(name);
        const ret = classifyCType(returnType, `${name} return type`);
        exports.push({ name, ret, args: parseParams(params, name) });
    }
    return exports;
}

function toCamelCase(exportName) {
    return exportName[0].toLowerCase() + exportName.slice(1);
}

/** The cwrap argTypes / returnType literal for one side of the boundary. */
function cwrapKind(kind) {
    return kind === 'void' ? 'null' : `'${kind}'`;
}

/** TS type on the cwrap façade: a `char*` is a JS string there. */
function tsKind(kind) {
    return kind === 'string' ? 'string' : kind === 'void' ? 'void' : 'number';
}

/** TS type on the raw `Module._Export`: a `char*` is a heap pointer there. */
function rawTsKind(kind) {
    return kind === 'void' ? 'void' : 'number';
}

function tsParams({ args }, kindOf) {
    return args.map((a) => `${a.name}: ${kindOf(a.kind)}`).join(', ');
}

function generateCwrapOverload(exp) {
    const { name, ret, args } = exp;
    const argTypes = `[${args.map((a) => cwrapKind(a.kind)).join(', ')}]`;
    return `  (
    ident: '${name}',
    returnType: ${cwrapKind(ret.kind)},
    argTypes: ${argTypes},
  ): (${tsParams(exp, tsKind)}) => ${tsKind(ret.kind)};`;
}

function generateCachedBinding({ name, ret, args }) {
    const argTypes = args.map((a) => cwrapKind(a.kind)).join(', ');
    return `    ${toCamelCase(name)}: cwrap('${name}', ${cwrapKind(ret.kind)}, [${argTypes}]),`;
}

function generateCachedExportType(exp) {
    return `    ${toCamelCase(exp.name)}: (${tsParams(exp, tsKind)}) => ${tsKind(exp.ret.kind)};`;
}

/** Raw `_Export` members on SolarSystemModule — numbers (and pointers) only, no façade brands. */
function generateModuleExport(exp) {
    return `  _${exp.name}: (${tsParams(exp, rawTsKind)}) => ${rawTsKind(exp.ret.kind)};`;
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
function findUnwrappedExports(exports, bridgeSource = readBridgeSource()) {
    if (bridgeSource === null) {
        return exports.map(({ name }) => name);
    }
    return exports
        .filter(({ name }) => {
            // Word-boundary match: a plain .includes() would count `exports.getCameraPosition`
            // as "wrapped" merely because `exports.getCameraPositionX` appears in the file.
            // `\s*` around the dot: Prettier wraps `exports.getX().split(..)` chains as
            // `exports⏎  .getX()⏎  .split(..)`, which is still a call site.
            const re = new RegExp(`exports\\s*\\.\\s*${escapeRegExp(toCamelCase(name))}\\b`);
            return !re.test(bridgeSource);
        })
        .map(({ name }) => name);
}

function readBridgeSource() {
    try {
        return readFileSync(bridgePath, 'utf8');
    } catch {
        return null;
    }
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

export { parseExports, generateCwrapOverload, generateModuleExport, findUnwrappedExports };

if (process.argv[1] && import.meta.url === pathToFileURL(process.argv[1]).href) {
    main();
}
