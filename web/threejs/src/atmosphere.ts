import * as THREE from 'three';
import { MeshBasicNodeMaterial, WebGPURenderer } from 'three/webgpu';
import { color, normalView, positionViewDirection } from 'three/tsl';

export interface AtmosphereConfig {
  bodyId: string;
  color: string;
  /** Shell radius as a multiple of the body radius. */
  shellScale: number;
  intensity: number;
  /** Rim exponent — higher keeps the glow tighter to the limb. */
  falloff: number;
}

/**
 * Deliberately *not* a port of the C++ Mie/Rayleigh scattering shader: an
 * inverted-hull shell with an additive Fresnel rim, which is enough to read as
 * an atmosphere in the companion. Two material flavours exist because raw GLSL
 * `ShaderMaterial` does not run on the WebGPU backend, and TSL node materials do
 * not run on the plain `THREE.WebGLRenderer` rescue path.
 */
export function createAtmosphereShell(
  renderer: WebGPURenderer | THREE.WebGLRenderer,
  bodyRadius: number,
  config: AtmosphereConfig,
): THREE.Mesh {
  const geometry = new THREE.SphereGeometry(bodyRadius * config.shellScale, 48, 32);
  const material =
    renderer instanceof WebGPURenderer
      ? createNodeMaterial(config)
      : createGlslMaterial(config);

  const shell = new THREE.Mesh(geometry, material);
  shell.name = `${config.bodyId}Atmosphere`;
  shell.renderOrder = 2;
  return shell;
}

function createNodeMaterial(config: AtmosphereConfig): THREE.Material {
  const material = new MeshBasicNodeMaterial();
  // abs() so the rim survives the flipped normals of back-face rendering.
  const rim = positionViewDirection
    .dot(normalView)
    .abs()
    .oneMinus()
    .saturate()
    .pow(config.falloff);

  material.colorNode = color(config.color);
  material.opacityNode = rim.mul(config.intensity);
  applyShellSettings(material);
  return material;
}

function createGlslMaterial(config: AtmosphereConfig): THREE.Material {
  const material = new THREE.ShaderMaterial({
    uniforms: {
      uColor: { value: new THREE.Color(config.color) },
      uIntensity: { value: config.intensity },
      uFalloff: { value: config.falloff },
    },
    vertexShader: /* glsl */ `
      varying vec3 vViewNormal;
      varying vec3 vViewPosition;

      void main() {
        vec4 viewPosition = modelViewMatrix * vec4(position, 1.0);
        vViewNormal = normalize(normalMatrix * normal);
        vViewPosition = viewPosition.xyz;
        gl_Position = projectionMatrix * viewPosition;
      }
    `,
    fragmentShader: /* glsl */ `
      uniform vec3 uColor;
      uniform float uIntensity;
      uniform float uFalloff;
      varying vec3 vViewNormal;
      varying vec3 vViewPosition;

      void main() {
        vec3 viewDirection = normalize(-vViewPosition);
        float rim = 1.0 - abs(dot(normalize(vViewNormal), viewDirection));
        gl_FragColor = vec4(uColor, pow(clamp(rim, 0.0, 1.0), uFalloff) * uIntensity);
      }
    `,
  });
  applyShellSettings(material);
  return material;
}

function applyShellSettings(material: THREE.Material): void {
  material.transparent = true;
  material.side = THREE.BackSide;
  material.blending = THREE.AdditiveBlending;
  material.depthWrite = false;
}
