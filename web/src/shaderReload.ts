/**
 * Dev-only shader hot reload: `await window.__solarSystem.reloadShaders()` re-fetches every
 * file the live programs were built from (the dev server serves ../resource/shaders under
 * `__dev/shaders/`, see vite.config.ts), writes the changed ones into MEMFS and asks C++ to
 * rebuild the programs whose files moved (Shader::ReloadChanged). A program that fails to
 * compile keeps drawing with its previous build; the error is in the console.
 *
 * bootstrap.ts only imports this under `import.meta.env.DEV`, so production has neither the
 * hook nor the route.
 */
import type { SolarSystemModule } from './SolarSystem.js';
import type { SolarSystemRuntime } from './wasmBridge.js';

export const DEV_SHADER_ROUTE = '__dev/shaders/';
const SHADER_DIR = 'resource/shaders/';

export async function reloadShaders(
    instance: SolarSystemModule,
    runtime: SolarSystemRuntime,
    baseUrl: URL,
): Promise<number> {
    const fs = instance.FS;
    if (!fs) {
        throw new Error('reloadShaders: the module does not export FS');
    }
    const files = runtime.getShaderSourceFiles().filter((path) => path.startsWith(SHADER_DIR));
    await Promise.all(files.map(async (path) => {
        const url = new URL(DEV_SHADER_ROUTE + path.slice(SHADER_DIR.length), baseUrl);
        const response = await fetch(url, { cache: 'no-store' });
        if (!response.ok) {
            throw new Error(`reloadShaders: ${url} answered ${response.status}`);
        }
        const text = await response.text();
        let current: string | undefined;
        try {
            current = fs.readFile(path, { encoding: 'utf8' });
        } catch {
            // Not in MEMFS yet (a file added since startup): write it.
        }
        // A write moves the file's mtime, which is what marks its programs for a rebuild.
        if (current !== text) {
            fs.writeFile(path, text);
        }
    }));
    const reloaded = runtime.reloadChangedShaders();
    console.log(`[shaders] ${reloaded} program(s) rebuilt from ${files.length} file(s)`);
    return reloaded;
}

export function installShaderReload(instance: SolarSystemModule, runtime: SolarSystemRuntime, baseUrl: URL): void {
    window.__solarSystem = {
        ...window.__solarSystem,
        reloadShaders: () => reloadShaders(instance, runtime, baseUrl),
    };
}
