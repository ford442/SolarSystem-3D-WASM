import * as THREE from 'three';

export interface RingConfig {
  bodyId: string;
  /** Inner/outer radius as a multiple of the body radius. */
  innerScale: number;
  outerScale: number;
  texture: string;
  opacity: number;
  /** [base, gap, highlight] colours for the procedural fallback strip. */
  palette: [string, string, string] | string[];
}

const RADIAL_SEGMENTS = 220;
const PROCEDURAL_WIDTH = 512;

/**
 * Ring plane for Saturn/Uranus. UVs are remapped so `u` runs from the inner to
 * the outer edge, which is how ring strips are authored; a KTX2 strip can be
 * dropped in later with {@link applyRingTexture}. Until then a procedural strip
 * (banding plus a Cassini-style gap) keeps the ring readable with the 4×4
 * placeholder DDS assets committed to the repo.
 */
export function createRingMesh(bodyRadius: number, config: RingConfig): THREE.Mesh {
  const inner = bodyRadius * config.innerScale;
  const outer = bodyRadius * config.outerScale;
  const geometry = createRadialUvRingGeometry(inner, outer);

  const material = new THREE.MeshLambertMaterial({
    map: createProceduralRingTexture(config),
    // Rings are thin and unshadowed here, so a touch of emissive keeps the
    // night side from going fully black.
    emissive: new THREE.Color(config.palette[0]).multiplyScalar(0.18),
    side: THREE.DoubleSide,
    transparent: true,
    opacity: config.opacity,
    depthWrite: false,
  });

  const mesh = new THREE.Mesh(geometry, material);
  mesh.name = `${config.bodyId}Ring`;
  mesh.rotation.x = -Math.PI / 2;
  mesh.renderOrder = 1;
  return mesh;
}

/** Swap in a real ring strip once one is published; ignores placeholder stubs. */
export function applyRingTexture(mesh: THREE.Mesh, texture: THREE.Texture): boolean {
  const width = (texture.image as { width?: number } | undefined)?.width ?? 0;
  if (width < 32) {
    texture.dispose();
    return false;
  }

  const material = mesh.material as THREE.MeshLambertMaterial;
  texture.colorSpace = THREE.SRGBColorSpace;
  texture.wrapS = THREE.ClampToEdgeWrapping;
  texture.wrapT = THREE.ClampToEdgeWrapping;
  texture.anisotropy = 4;
  material.map?.dispose();
  material.map = texture;
  material.needsUpdate = true;
  return true;
}

function createRadialUvRingGeometry(inner: number, outer: number): THREE.RingGeometry {
  const geometry = new THREE.RingGeometry(inner, outer, RADIAL_SEGMENTS, 1);
  const position = geometry.attributes.position;
  const uv = geometry.attributes.uv;
  const vertex = new THREE.Vector3();

  for (let index = 0; index < position.count; index++) {
    vertex.fromBufferAttribute(position, index);
    const radius = vertex.length();
    uv.setXY(index, THREE.MathUtils.clamp((radius - inner) / (outer - inner), 0, 1), 0.5);
  }
  uv.needsUpdate = true;
  return geometry;
}

function createProceduralRingTexture(config: RingConfig): THREE.CanvasTexture {
  const canvas = document.createElement('canvas');
  canvas.width = PROCEDURAL_WIDTH;
  canvas.height = 1;
  const context = canvas.getContext('2d');
  if (!context) {
    throw new Error(`Unable to build procedural ring texture for ${config.bodyId}`);
  }

  const base = new THREE.Color(config.palette[0]);
  const gap = new THREE.Color(config.palette[1] ?? config.palette[0]);
  const highlight = new THREE.Color(config.palette[2] ?? config.palette[0]);
  const image = context.createImageData(PROCEDURAL_WIDTH, 1);
  const swatch = new THREE.Color();

  for (let x = 0; x < PROCEDURAL_WIDTH; x++) {
    const t = x / (PROCEDURAL_WIDTH - 1);
    const banding = 0.5 + 0.5 * Math.sin(t * 68) * Math.sin(t * 17 + 1.3);
    swatch.copy(base).lerp(highlight, banding * 0.55);

    // Soft inner/outer edges plus one wide division around 62% of the span.
    const innerEdge = THREE.MathUtils.smoothstep(t, 0, 0.06);
    const outerEdge = 1 - THREE.MathUtils.smoothstep(t, 0.94, 1);
    const division = 1 - 0.85 * gaussian(t, 0.62, 0.022);
    const alpha = THREE.MathUtils.clamp(
      innerEdge * outerEdge * division * (0.55 + 0.45 * banding),
      0,
      1,
    );
    swatch.lerp(gap, (1 - division) * 0.6);

    const offset = x * 4;
    image.data[offset] = Math.round(swatch.r * 255);
    image.data[offset + 1] = Math.round(swatch.g * 255);
    image.data[offset + 2] = Math.round(swatch.b * 255);
    image.data[offset + 3] = Math.round(alpha * 255);
  }

  context.putImageData(image, 0, 0);
  const texture = new THREE.CanvasTexture(canvas);
  texture.colorSpace = THREE.SRGBColorSpace;
  texture.wrapS = THREE.ClampToEdgeWrapping;
  texture.wrapT = THREE.ClampToEdgeWrapping;
  return texture;
}

function gaussian(value: number, center: number, width: number): number {
  const offset = (value - center) / width;
  return Math.exp(-0.5 * offset * offset);
}
