import * as THREE from 'three';
import { RenderPipeline, WebGPURenderer } from 'three/webgpu';
import { pass } from 'three/tsl';
import { bloom } from 'three/addons/tsl/display/BloomNode.js';
import { EffectComposer } from 'three/addons/postprocessing/EffectComposer.js';
import { RenderPass } from 'three/addons/postprocessing/RenderPass.js';
import { UnrealBloomPass } from 'three/addons/postprocessing/UnrealBloomPass.js';
import { OutputPass } from 'three/addons/postprocessing/OutputPass.js';

export interface PostProcessingConfig {
  enabled: boolean;
  bloomStrength: number;
  bloomRadius: number;
  bloomThreshold: number;
  toneMappingExposure: number;
}

/**
 * Renders the scene either directly or through a bloom + tone-mapping chain.
 *
 * Two backends are supported because the node-based `RenderPipeline` only runs on
 * `WebGPURenderer` (WebGPU *and* its internal WebGL backend), while the plain
 * `THREE.WebGLRenderer` rescue path needs the classic `EffectComposer` stack.
 * Both apply threshold bloom (the Sun is the only bright emitter) plus ACES tone
 * mapping, so the two paths look close without sharing shader code with the
 * C++ HDR pass.
 */
export interface PostPipeline {
  readonly label: string;
  render(): void;
  setSize(width: number, height: number, pixelRatio: number): void;
  dispose(): void;
}

export function createPostPipeline(
  renderer: WebGPURenderer | THREE.WebGLRenderer,
  scene: THREE.Scene,
  camera: THREE.PerspectiveCamera,
  config: PostProcessingConfig,
): PostPipeline {
  if (!config.enabled) {
    return {
      label: 'tone map only',
      render: () => renderer.render(scene, camera),
      setSize: () => {},
      dispose: () => {},
    };
  }

  if (renderer instanceof WebGPURenderer) {
    return createNodePipeline(renderer, scene, camera, config);
  }

  return createComposerPipeline(renderer, scene, camera, config);
}

function createNodePipeline(
  renderer: WebGPURenderer,
  scene: THREE.Scene,
  camera: THREE.PerspectiveCamera,
  config: PostProcessingConfig,
): PostPipeline {
  const scenePass = pass(scene, camera);
  const bloomNode = bloom(
    scenePass.getTextureNode(),
    config.bloomStrength,
    config.bloomRadius,
    config.bloomThreshold,
  );

  const pipeline = new RenderPipeline(renderer);
  // Additive composite keeps the base image intact and only adds the glow.
  pipeline.outputNode = scenePass.add(bloomNode);

  return {
    label: 'TSL bloom + ACES',
    render: () => pipeline.render(),
    setSize: () => {
      // RenderPipeline follows the renderer's own size; nothing to do here.
    },
    dispose: () => pipeline.dispose(),
  };
}

function createComposerPipeline(
  renderer: THREE.WebGLRenderer,
  scene: THREE.Scene,
  camera: THREE.PerspectiveCamera,
  config: PostProcessingConfig,
): PostPipeline {
  const size = renderer.getSize(new THREE.Vector2());
  const composer = new EffectComposer(renderer);
  composer.addPass(new RenderPass(scene, camera));
  composer.addPass(
    new UnrealBloomPass(size, config.bloomStrength, config.bloomRadius, config.bloomThreshold),
  );
  // OutputPass applies renderer.toneMapping and the output color space last.
  composer.addPass(new OutputPass());

  return {
    label: 'UnrealBloom + ACES',
    render: () => composer.render(),
    setSize: (width, height, pixelRatio) => {
      composer.setPixelRatio(pixelRatio);
      composer.setSize(width, height);
    },
    dispose: () => composer.dispose(),
  };
}
