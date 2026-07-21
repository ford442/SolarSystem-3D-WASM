import './style.css';
import * as THREE from 'three';
import { WebGPURenderer } from 'three/webgpu';
import { OrbitControls } from 'three/addons/controls/OrbitControls.js';
import { KTX2Loader } from 'three/addons/loaders/KTX2Loader.js';
import { VRButton } from 'three/addons/webxr/VRButton.js';
import orbitalParametersJson from './data/orbital-parameters.json';
import {
  PlanetTextureLodManager,
  type PlanetTextureLodTarget,
  type TextureTier,
} from './textureLod';

interface OrbitalBody {
  id: string;
  name: string;
  position: [number, number, number];
  earthRadiusScale: number;
  axialTiltDegrees: number;
  rotationRate: number;
  texture: string;
  fallbackColor: string;
}

interface OrbitalParameters {
  source: string;
  bodies: OrbitalBody[];
}

interface PlanetView {
  parameters: OrbitalBody;
  mesh: THREE.Mesh<THREE.SphereGeometry, THREE.MeshPhongMaterial>;
  label: HTMLElement;
  visualRadius: number;
  textureTarget: PlanetTextureLodTarget;
}

interface CameraTransition {
  startedAt: number;
  duration: number;
  fromPosition: THREE.Vector3;
  toPosition: THREE.Vector3;
  fromTarget: THREE.Vector3;
  toTarget: THREE.Vector3;
}

type CompanionRenderer = WebGPURenderer | THREE.WebGLRenderer;

const orbitalParameters = orbitalParametersJson as OrbitalParameters;
const infoElement = requiredElement('info');
const focusElement = requiredElement('focus-presets');
const textureStatusElement = requiredElement('texture-status');
const canvas = requiredCanvas('scene');

const lowTextureBase = withTrailingSlash(`${import.meta.env.BASE_URL}textures/ktx2/`);
const highTextureBase = withTrailingSlash(
  import.meta.env.VITE_KTX2_BASE?.trim() || `${import.meta.env.BASE_URL}textures/ktx2/high/`,
);
const transcoderBase = withTrailingSlash(`${import.meta.env.BASE_URL}basis/`);
const scene = new THREE.Scene();
scene.background = new THREE.Color(0x01030a);
scene.fog = new THREE.FogExp2(0x01030a, 0.00008);

const camera = new THREE.PerspectiveCamera(45, window.innerWidth / window.innerHeight, 0.1, 12000);
camera.position.set(0, 1250, 3600);

let renderer: CompanionRenderer;
let controls: OrbitControls;
let ktx2Loader: KTX2Loader;
let textureLodManager: PlanetTextureLodManager;
let cameraTransition: CameraTransition | null = null;
const planets: PlanetView[] = [];
const pressedKeys = new Set<string>();
const cameraVelocity = new THREE.Vector3();
const timer = new THREE.Timer();
timer.connect(document);

const tierCounts: Record<TextureTier, number> = { low: 0, high: 0 };

async function init(): Promise<void> {
  const rendererResult = await createRenderer(canvas);
  renderer = rendererResult.renderer;
  renderer.setPixelRatio(Math.min(window.devicePixelRatio, 2));
  renderer.setSize(window.innerWidth, window.innerHeight);
  renderer.outputColorSpace = THREE.SRGBColorSpace;
  renderer.toneMapping = THREE.ACESFilmicToneMapping;
  renderer.toneMappingExposure = 1.15;

  controls = new OrbitControls(camera, renderer.domElement);
  controls.enableDamping = true;
  controls.dampingFactor = 0.075;
  controls.zoomSpeed = 0.8;
  controls.rotateSpeed = 0.55;
  controls.panSpeed = 0.65;
  controls.zoomToCursor = true;
  controls.minDistance = 8;
  controls.maxDistance = 6500;
  controls.target.set(0, 0, 0);

  ktx2Loader = new KTX2Loader()
    .setTranscoderPath(transcoderBase)
    .setWorkerLimit(2)
    .detectSupport(renderer);

  textureLodManager = new PlanetTextureLodManager(ktx2Loader, () => {
    tierCounts.low = 0;
    tierCounts.high = 0;
    for (const planet of planets) {
      const tier = textureLodManager.getActiveTier(planet.parameters.id);
      tierCounts[tier]++;
    }
    updateTextureStatus();
  });

  addLighting();
  addStarfield();
  createSolarSystem();
  createFocusPresets();
  bindInput();
  onResize();

  infoElement.textContent = `Phase 1 · ${rendererResult.backend} · 4 inner planets · texture LOD`;
  updateTextureStatus();

  if (renderer instanceof THREE.WebGLRenderer) {
    renderer.xr.enabled = true;
    const vrButton = VRButton.createButton(renderer);
    vrButton.style.position = 'fixed';
    document.body.appendChild(vrButton);
  }

  renderer.setAnimationLoop(animate);
  console.info('[threejs-companion] Phase 1 ready', {
    backend: rendererResult.backend,
    orbitalSource: orbitalParameters.source,
    lowTextureBase,
    highTextureBase,
  });
}

async function createRenderer(targetCanvas: HTMLCanvasElement): Promise<{ renderer: CompanionRenderer; backend: string }> {
  try {
    const webgpuRenderer = new WebGPURenderer({ canvas: targetCanvas, antialias: true });
    await webgpuRenderer.init();
    const backend = webgpuRenderer.backend as { isWebGLBackend?: boolean };
    return { renderer: webgpuRenderer, backend: backend.isWebGLBackend ? 'WebGL fallback' : 'WebGPU' };
  } catch (error) {
    console.warn('[threejs-companion] WebGPU unavailable; using WebGL fallback.', error);
    return {
      renderer: new THREE.WebGLRenderer({ canvas: targetCanvas, antialias: true }),
      backend: 'WebGL fallback',
    };
  }
}

function addLighting(): void {
  const sun = new THREE.Mesh(
    new THREE.SphereGeometry(45, 48, 32),
    new THREE.MeshBasicMaterial({ color: 0xffd46a }),
  );
  sun.name = 'Sun';
  scene.add(sun);

  const sunlight = new THREE.PointLight(0xfff1d0, 8_000_000, 0, 2);
  sunlight.position.set(0, 0, 0);
  scene.add(sunlight, new THREE.AmbientLight(0x33405c, 0.32));
}

function addStarfield(): void {
  const count = 1800;
  const positions = new Float32Array(count * 3);
  for (let index = 0; index < count; index++) {
    const radius = 4500 + Math.random() * 3500;
    const theta = Math.random() * Math.PI * 2;
    const phi = Math.acos(2 * Math.random() - 1);
    positions[index * 3] = radius * Math.sin(phi) * Math.cos(theta);
    positions[index * 3 + 1] = radius * Math.cos(phi);
    positions[index * 3 + 2] = radius * Math.sin(phi) * Math.sin(theta);
  }
  const geometry = new THREE.BufferGeometry();
  geometry.setAttribute('position', new THREE.BufferAttribute(positions, 3));
  scene.add(new THREE.Points(geometry, new THREE.PointsMaterial({ color: 0xb8c9ff, size: 4, sizeAttenuation: true })));
}

function createSolarSystem(): void {
  for (const parameters of orbitalParameters.bodies) {
    const visualRadius = Math.max(12, parameters.earthRadiusScale * 30);
    const material = new THREE.MeshPhongMaterial({
      color: parameters.fallbackColor,
      shininess: parameters.id === 'earth' ? 18 : 5,
      specular: parameters.id === 'earth' ? 0x38506d : 0x171717,
    });
    const mesh = new THREE.Mesh(new THREE.SphereGeometry(visualRadius, 64, 48), material);
    mesh.name = parameters.name;
    mesh.position.fromArray(parameters.position);
    mesh.rotation.z = THREE.MathUtils.degToRad(parameters.axialTiltDegrees);
    scene.add(mesh);
    addOrbitGuide(mesh.position.length());

    const label = document.createElement('button');
    label.className = 'planet-label';
    label.textContent = parameters.name;
    label.addEventListener('click', () => focusPlanet(parameters.id));
    document.body.appendChild(label);

    const textureTarget: PlanetTextureLodTarget = {
      id: parameters.id,
      name: parameters.name,
      mesh,
      worldPosition: mesh.position,
      lowUrl: textureUrl(parameters.texture, lowTextureBase),
      highUrl: textureUrl(parameters.texture, highTextureBase),
    };

    const planet: PlanetView = { parameters, mesh, label, visualRadius, textureTarget };
    planets.push(planet);
    textureLodManager.register(textureTarget);
  }
}

function textureUrl(fileName: string, base: string): string {
  return new URL(fileName, new URL(base, window.location.href)).toString();
}

function addOrbitGuide(radius: number): void {
  const points: THREE.Vector3[] = [];
  for (let index = 0; index <= 128; index++) {
    const angle = (index / 128) * Math.PI * 2;
    points.push(new THREE.Vector3(Math.cos(angle) * radius, 0, Math.sin(angle) * radius));
  }
  const geometry = new THREE.BufferGeometry().setFromPoints(points);
  scene.add(new THREE.Line(geometry, new THREE.LineBasicMaterial({ color: 0x25314a, transparent: true, opacity: 0.45 })));
}

function updateTextureStatus(): void {
  const highBaseLabel = import.meta.env.VITE_KTX2_BASE?.trim() ? 'CDN high' : 'local high';
  textureStatusElement.textContent =
    `LOD low ${tierCounts.low} · high ${tierCounts.high} · ${highBaseLabel}: ${highTextureBase}`;
}

function createFocusPresets(): void {
  const overviewButton = createFocusButton('Overview', () => focusOverview());
  focusElement.appendChild(overviewButton);
  for (const body of orbitalParameters.bodies) {
    focusElement.appendChild(createFocusButton(body.name, () => focusPlanet(body.id)));
  }
}

function createFocusButton(label: string, action: () => void): HTMLButtonElement {
  const button = document.createElement('button');
  button.type = 'button';
  button.textContent = label;
  button.addEventListener('click', action);
  return button;
}

function focusPlanet(id: string): void {
  const planet = planets.find((candidate) => candidate.parameters.id === id);
  if (!planet) return;
  const target = planet.mesh.position.clone();
  const direction = camera.position.clone().sub(controls.target).normalize();
  if (direction.lengthSq() < 0.5) direction.set(0.7, 0.35, 0.7).normalize();
  startCameraTransition(target.clone().add(direction.multiplyScalar(Math.max(planet.visualRadius * 7, 100))), target);
}

function focusOverview(): void {
  startCameraTransition(new THREE.Vector3(0, 1250, 3600), new THREE.Vector3(0, 0, 0));
}

function startCameraTransition(position: THREE.Vector3, target: THREE.Vector3): void {
  cameraTransition = {
    startedAt: performance.now(),
    duration: 1_350,
    fromPosition: camera.position.clone(),
    toPosition: position,
    fromTarget: controls.target.clone(),
    toTarget: target,
  };
  cameraVelocity.set(0, 0, 0);
}

function updateCameraTransition(now: number): void {
  if (!cameraTransition) return;
  const progress = Math.min(1, (now - cameraTransition.startedAt) / cameraTransition.duration);
  const eased = progress * progress * (3 - 2 * progress);
  camera.position.lerpVectors(cameraTransition.fromPosition, cameraTransition.toPosition, eased);
  controls.target.lerpVectors(cameraTransition.fromTarget, cameraTransition.toTarget, eased);
  if (progress === 1) cameraTransition = null;
}

function bindInput(): void {
  window.addEventListener('keydown', (event) => {
    if (['KeyW', 'KeyA', 'KeyS', 'KeyD', 'Space', 'KeyC'].includes(event.code)) {
      pressedKeys.add(event.code);
      cameraTransition = null;
      if (event.code === 'Space') event.preventDefault();
    }
  });
  window.addEventListener('keyup', (event) => pressedKeys.delete(event.code));
  window.addEventListener('blur', () => pressedKeys.clear());
  window.addEventListener('resize', onResize);
}

function updateFlyCamera(deltaSeconds: number): void {
  const input = new THREE.Vector3();
  const forward = controls.target.clone().sub(camera.position).normalize();
  const right = new THREE.Vector3().crossVectors(forward, camera.up).normalize();
  if (pressedKeys.has('KeyW')) input.add(forward);
  if (pressedKeys.has('KeyS')) input.sub(forward);
  if (pressedKeys.has('KeyD')) input.add(right);
  if (pressedKeys.has('KeyA')) input.sub(right);
  if (pressedKeys.has('Space')) input.y += 1;
  if (pressedKeys.has('KeyC')) input.y -= 1;

  const acceleration = 620;
  const maxSpeed = 420;
  if (input.lengthSq() > 0) cameraVelocity.addScaledVector(input.normalize(), acceleration * deltaSeconds);
  cameraVelocity.multiplyScalar(Math.exp(-4.2 * deltaSeconds));
  cameraVelocity.clampLength(0, maxSpeed);
  const movement = cameraVelocity.clone().multiplyScalar(deltaSeconds);
  camera.position.add(movement);
  controls.target.add(movement);
}

function updateLabels(): void {
  const projected = new THREE.Vector3();
  for (const planet of planets) {
    projected.copy(planet.mesh.position).project(camera);
    const visible = projected.z > -1 && projected.z < 1;
    planet.label.hidden = !visible;
    if (!visible) continue;
    planet.label.style.transform = `translate(-50%, -50%) translate(${(projected.x * 0.5 + 0.5) * window.innerWidth}px, ${(-projected.y * 0.5 + 0.5) * window.innerHeight}px)`;
  }
}

function animate(now: number): void {
  timer.update(now);
  const deltaSeconds = Math.min(timer.getDelta(), 0.05);
  updateCameraTransition(now);
  if (!cameraTransition) updateFlyCamera(deltaSeconds);
  for (const planet of planets) planet.mesh.rotation.y += planet.parameters.rotationRate * deltaSeconds * 60;
  controls.update();
  textureLodManager.update(camera, planets.map((planet) => planet.textureTarget));
  updateLabels();
  renderer.render(scene, camera);
}

function onResize(): void {
  camera.aspect = window.innerWidth / window.innerHeight;
  camera.updateProjectionMatrix();
  renderer?.setSize(window.innerWidth, window.innerHeight);
}

function withTrailingSlash(value: string): string {
  return value.endsWith('/') ? value : `${value}/`;
}

function requiredElement(id: string): HTMLElement {
  const element = document.getElementById(id);
  if (!element) throw new Error(`Missing #${id}`);
  return element;
}

function requiredCanvas(id: string): HTMLCanvasElement {
  const element = document.getElementById(id);
  if (!(element instanceof HTMLCanvasElement)) throw new Error(`Missing #${id} canvas`);
  return element;
}

void init().catch((error: unknown) => {
  console.error('[threejs-companion] Initialization failed', error);
  infoElement.textContent = `Initialization failed: ${error instanceof Error ? error.message : String(error)}`;
  infoElement.classList.add('error');
});
