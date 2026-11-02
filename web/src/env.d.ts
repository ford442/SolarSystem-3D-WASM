/// <reference types="vite/client" />

// Typed so a read is `string | undefined`, not the `any` of vite/client's index signature.
interface ImportMetaEnv {
    /** Where runtime assets (textures, manifests, music) are fetched from; default: same origin. */
    readonly VITE_ASSET_BASE?: string;
    /** Comma-separated KTX2 texture packs this deployment publishes, e.g. "bc3,astc,etc2". */
    readonly VITE_TEXTURE_PACKS?: string;
}
