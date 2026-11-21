// @ts-check
import eslint from '@eslint/js';
import { defineConfig } from 'eslint/config';
import tseslint from 'typescript-eslint';

// Lint scope is src/ (the files tsconfig.json covers, so type-aware rules have a program).
// Generated files are regenerated wholesale by scripts/generate-*.mjs and checked there.
export default defineConfig(
    {
        ignores: [
            'dist/**',
            'public/**',
            'threejs/**',
            'test-results/**',
            'playwright-report/**',
            'src/generated/**',
            'src/wasmBridge.exports.ts',
            'src/SolarSystem.d.ts',
        ],
    },
    eslint.configs.recommended,
    tseslint.configs.strictTypeChecked,
    {
        files: ['src/**/*.ts'],
        languageOptions: {
            parserOptions: {
                projectService: true,
                tsconfigRootDir: import.meta.dirname,
            },
        },
        rules: {
            // `as any` and friends are how an unchecked value leaks past tsc --strict.
            '@typescript-eslint/no-explicit-any': 'error',
            '@typescript-eslint/no-unsafe-assignment': 'error',
            '@typescript-eslint/no-unsafe-member-access': 'error',
            '@typescript-eslint/no-unsafe-call': 'error',
            '@typescript-eslint/no-unsafe-return': 'error',
            // verbatimModuleSyntax is on in tsconfig; keep imports honest about what is a type.
            '@typescript-eslint/consistent-type-imports': [
                'error',
                { prefer: 'type-imports', fixStyle: 'inline-type-imports' },
            ],
            '@typescript-eslint/consistent-type-exports': 'error',
            // `_x` marks an intentionally unused binding (omit-by-rest, ignored callback args).
            '@typescript-eslint/no-unused-vars': [
                'error',
                { argsIgnorePattern: '^_', varsIgnorePattern: '^_', ignoreRestSiblings: true },
            ],
            // The ambient WebXR stubs are single-extends marker interfaces (XRSpace extends EventTarget).
            '@typescript-eslint/no-empty-object-type': ['error', { allowInterfaces: 'with-single-extends' }],
            // `${n}` for a number is fine; null/undefined/boolean/objects in a template are bugs.
            '@typescript-eslint/restrict-template-expressions': ['error', { allowNumber: true }],
            // `(x) => bridge.setX(x)` is how the wasmBridge façade is written.
            '@typescript-eslint/no-confusing-void-expression': ['error', { ignoreArrowShorthand: true }],
        },
    },
    {
        files: ['**/*.test.ts'],
        rules: {
            // Tests assert on loosely-typed fixtures on purpose.
            '@typescript-eslint/no-non-null-assertion': 'off',
            // Mocks implement async interfaces (Cache, fetch) without awaiting anything.
            '@typescript-eslint/require-await': 'off',
        },
    },
);
