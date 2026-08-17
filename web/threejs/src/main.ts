import './style.css';
import * as THREE from 'three';
import { WebGPURenderer } from 'three/webgpu';
import { OrbitControls } from 'three/addons/controls/OrbitControls.js';
import { KTX2Loader } from 'three/addons/loaders/KTX2Loader.js';
import { VRButton } from 'three/addons/webxr/VRButton.js';
import orbitalParametersJson from './data/orbital-parameters.json';
import companionConfigJson from './data/companion-config.json';
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

interface MoonConfig {
  id: string;
  name: string;
  parent: string;
  orbitRadius: number;
  earthRadiusScale: number;
  periodDays: number;
  texture: string;
  fallbackColor: string;
  renderMode: 'full' | 'proxy';
}

interface CompanionConfig {
  phase: number;
  title: string;
  fullBodyIds: string[];
  proxyBodyIds: string[];
  skybox: {
    enabled: boolean;
    directory: string;
    faces: string[];
  };
  moons: MoonConfig[];
}

type BodyRenderMode = 'full' | 'proxy';

interface BodyView {
  id: string;
  name: string;
  mode: BodyRenderMode;
  kind: 'sun' | 'planet' | 'moon' | 'proxy';
  mesh: THREE.Object3D;
  label: HTMLElement;
  visualRadius: number;
  worldPosition: THREE.Vector3;
  rotationRate: number;
  textureTarget?: PlanetTextureLodTarget;
  /** Parent mesh for moons (local orbit). */
  parentId?: string;
  orbitRadius?: number;
  periodDays?: number;
  anomaly?: number;
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

const orbitalParameters = orbitalParametersJson as unknown as OrbitalParameters;
const companionConfig = companionConfigJson as unknown as CompanionConfig;

const infoElement = requiredElement('info');
const focusElement = requiredElement('focus-presets');
const textureStatusElement = requiredElement('texture-status');
const titleElement = requiredElement('companion-title');
const canvas = requiredCanvas('scene');

const lowTextureBase = withTrailingSlash(`${import.meta.env.BASE_URL}textures/ktx2/`);
const highTextureBase = withTrailingSlash(
  import.meta.env.VITE_KTX2_BASE?.trim() || `${import.meta.env.BASE_URL}textures/ktx2/high/`,
);
const transcoderBase = withTrailingSlash(`${import.meta.env.BASE_URL}basis/`);
const skyboxBase = withTrailingSlash(
  `${import.meta.env.BASE_URL}${companionConfig.skybox.directory}`,
);

const scene = new THREE.Scene();
scene.background = new THREE.Color(0x01030a);
scene.fog = new THREE.FogExp2(0x01030a, 0.00004);

const camera = new THREE.PerspectiveCamera(45, window.innerWidth / window.innerHeight, 0.1, 20_000);
camera.position.set(0, 1250, 3600);

let renderer: CompanionRenderer;
let controls: OrbitControls;
let ktx2Loader: KTX2Loader;
let textureLodManager: PlanetTextureLodManager;
let cameraTransition: CameraTransition | null = null;
const bodies: BodyView[] = [];
const bodyById = new Map<string, BodyView>();
const pressedKeys = new Set<string>();
const cameraVelocity = new THREE.Vector3();
const timer = new THREE.Timer();
timer.connect(document);

const tierCounts: Record<TextureTier, number> = { low: 0, high: 0 };
let skyboxStatus = 'procedural starfield';
let pendingTextureLoads = 0;

const EARTH_ORBIT_SECONDS_AT_1X = 120;
const EARTH_YEAR_DAYS = 365.25;

async function init(): Promise<void> {
  titleElement.textContent = companionConfig.title;

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
  controls.maxDistance = 9000;
  controls.target.set(0, 0, 0);

  ktx2Loader = new KTX2Loader()
    .setTranscoderPath(transcoderBase)
    .setWorkerLimit(2)
    .detectSupport(renderer);

  textureLodManager = new PlanetTextureLodManager(ktx2Loader, () => {
    tierCounts.low = 0;
    tierCounts.high = 0;
    for (const body of bodies) {
      if (!body.textureTarget) continue;
      const tier = textureLodManager.getActiveTier(body.id);
      tierCounts[tier]++;
    }
    updateTextureStatus();
  });

  addLightingAndSun();
  addProceduralStarfield();
  await tryLoadSkybox();
  createSolarSystem();
  createFocusPresets();
  bindInput();
  onResize();

  const fullCount = companionConfig.fullBodyIds.length;
  const proxyCount = companionConfig.proxyBodyIds.length;
  infoElement.textContent =
    `Phase ${companionConfig.phase} · ${rendererResult.backend} · ` +
    `${fullCount} full bodies · ${proxyCount} proxies · ${companionConfig.moons.length} moons · ${skyboxStatus}`;
  updateTextureStatus();

  if (renderer instanceof THREE.WebGLRenderer) {
    renderer.xr.enabled = true;
    const vrButton = VRButton.createButton(renderer);
    vrButton.style.position = 'fixed';
    document.body.appendChild(vrButton);
  }

  renderer.setAnimationLoop(animate);
  console.info('[threejs-companion] Phase 2 ready', {
    backend: rendererResult.backend,
    orbitalSource: orbitalParameters.source,
    fullBodyIds: companionConfig.fullBodyIds,
    proxyBodyIds: companionConfig.proxyBodyIds,
    lowTextureBase,
    highTextureBase,
    skyboxStatus,
  });
}

async function createRenderer(
  targetCanvas: HTMLCanvasElement,
): Promise<{ renderer: CompanionRenderer; backend: string }> {
  try {
    const webgpuRenderer = new WebGPURenderer({ canvas: targetCanvas, antialias: true });
    await webgpuRenderer.init();
    const backend = webgpuRenderer.backend as { isWebGLBackend?: boolean };
    return {
      renderer: webgpuRenderer,
      backend: backend.isWebGLBackend ? 'WebGL fallback' : 'WebGPU',
    };
  } catch (error) {
    console.warn('[threejs-companion] WebGPU unavailable; using WebGL fallback.', error);
    return {
      renderer: new THREE.WebGLRenderer({ canvas: targetCanvas, antialias: true }),
      backend: 'WebGL fallback',
    };
  }
}

function addLightingAndSun(): void {
  const sunRadius = 45;
  const sunMesh = new THREE.Mesh(
    new THREE.SphereGeometry(sunRadius, 48, 32),
    new THREE.MeshBasicMaterial({ color: 0xffd46a }),
  );
  sunMesh.name = 'Sun';
  scene.add(sunMesh);

  const sunlight = new THREE.PointLight(0xfff1d0, 8_000_000, 0, 2);
  sunlight.position.set(0, 0, 0);
  scene.add(sunlight, new THREE.AmbientLight(0x33405c, 0.32));

  const sunLabel = createBodyLabel('Sun', false);
  sunLabel.addEventListener('click', () => focusBody('sun'));
  document.body.appendChild(sunLabel);

  const sunView: BodyView = {
    id: 'sun',
    name: 'Sun',
    mode: 'full',
    kind: 'sun',
    mesh: sunMesh,
    label: sunLabel,
    visualRadius: sunRadius,
    worldPosition: sunMesh.position,
    rotationRate: 0.002,
  };
  bodies.push(sunView);
  bodyById.set('sun', sunView);
}

function addProceduralStarfield(): void {
  const count = 4200;
  const positions = new Float32Array(count * 3);
  for (let index = 0; index < count; index++) {
    const radius = 5000 + Math.random() * 7000;
    const theta = Math.random() * Math.PI * 2;
    const phi = Math.acos(2 * Math.random() - 1);
    positions[index * 3] = radius * Math.sin(phi) * Math.cos(theta);
    positions[index * 3 + 1] = radius * Math.cos(phi);
    positions[index * 3 + 2] = radius * Math.sin(phi) * Math.sin(theta);
  }
  const geometry = new THREE.BufferGeometry();
  geometry.setAttribute('position', new THREE.BufferAttribute(positions, 3));
  scene.add(
    new THREE.Points(
      geometry,
      new THREE.PointsMaterial({
        color: 0xb8c9ff,
        size: 3.5,
        sizeAttenuation: true,
        transparent: true,
        opacity: 0.9,
        depthWrite: false,
      }),
    ),
  );
}

async function tryLoadSkybox(): Promise<void> {
  if (!companionConfig.skybox.enabled) {
    skyboxStatus = 'starfield only';
    return;
  }

  try {
    const materials: THREE.MeshBasicMaterial[] = [];
    for (const face of companionConfig.skybox.faces) {
      const url = textureUrl(face, skyboxBase);
      pendingTextureLoads++;
      updateTextureStatus();
      try {
        const texture = await ktx2Loader.loadAsync(url);
        texture.colorSpace = THREE.SRGBColorSpace;
        materials.push(
          new THREE.MeshBasicMaterial({
            map: texture,
            side: THREE.BackSide,
            depthWrite: false,
          }),
        );
      } finally {
        pendingTextureLoads = Math.max(0, pendingTextureLoads - 1);
        updateTextureStatus();
      }
    }

    if (materials.length === 6) {
      // BoxGeometry material order: +x, -x, +y, -y, +z, -z
      const sky = new THREE.Mesh(new THREE.BoxGeometry(14_000, 14_000, 14_000), materials);
      sky.name = 'Skybox';
      sky.frustumCulled = false;
      scene.add(sky);
      skyboxStatus = 'KTX2 cube + starfield';
      console.info('[threejs-companion] Skybox cube loaded from', skyboxBase);
    } else {
      skyboxStatus = 'procedural starfield (skybox incomplete)';
    }
  } catch (error) {
    skyboxStatus = 'procedural starfield (skybox unavailable)';
    console.warn('[threejs-companion] Skybox KTX2 unavailable; using starfield only.', error);
  }
}

function createSolarSystem(): void {
  const fullSet = new Set(companionConfig.fullBodyIds);
  const proxySet = new Set(companionConfig.proxyBodyIds);

  for (const parameters of orbitalParameters.bodies) {
    if (fullSet.has(parameters.id)) {
      createFullPlanet(parameters);
    } else if (proxySet.has(parameters.id)) {
      createProxyMarker(parameters);
    }
    // Bodies not listed in phase config are skipped (future outer moons, etc.)
  }

  for (const moon of companionConfig.moons) {
    createMoon(moon);
  }
}

function createFullPlanet(parameters: OrbitalBody): void {
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

  const label = createBodyLabel(parameters.name, false);
  label.addEventListener('click', () => focusBody(parameters.id));
  document.body.appendChild(label);

  const textureTarget: PlanetTextureLodTarget = {
    id: parameters.id,
    name: parameters.name,
    mesh,
    worldPosition: mesh.position,
    lowUrl: textureUrl(parameters.texture, lowTextureBase),
    highUrl: textureUrl(parameters.texture, highTextureBase),
  };

  const view: BodyView = {
    id: parameters.id,
    name: parameters.name,
    mode: 'full',
    kind: 'planet',
    mesh,
    label,
    visualRadius,
    worldPosition: mesh.position,
    rotationRate: parameters.rotationRate,
    textureTarget,
  };
  bodies.push(view);
  bodyById.set(parameters.id, view);
  textureLodManager.register(textureTarget);
}

function createProxyMarker(parameters: OrbitalBody): void {
  const visualRadius = Math.max(18, parameters.earthRadiusScale * 8);
  const group = new THREE.Group();
  group.name = `${parameters.name}Proxy`;
  group.position.fromArray(parameters.position);

  const diamond = new THREE.Mesh(
    new THREE.OctahedronGeometry(visualRadius * 0.55, 0),
    new THREE.MeshBasicMaterial({
      color: parameters.fallbackColor,
      wireframe: true,
      transparent: true,
      opacity: 0.85,
    }),
  );
  const core = new THREE.Mesh(
    new THREE.SphereGeometry(visualRadius * 0.22, 12, 10),
    new THREE.MeshBasicMaterial({ color: parameters.fallbackColor }),
  );
  group.add(diamond, core);
  scene.add(group);
  addOrbitGuide(group.position.length(), 0x3a4a66);

  const label = createBodyLabel(`${parameters.name} · proxy`, true);
  label.addEventListener('click', () => focusBody(parameters.id));
  document.body.appendChild(label);

  const view: BodyView = {
    id: parameters.id,
    name: parameters.name,
    mode: 'proxy',
    kind: 'proxy',
    mesh: group,
    label,
    visualRadius,
    worldPosition: group.position,
    rotationRate: 0.004,
  };
  bodies.push(view);
  bodyById.set(parameters.id, view);
}

function createMoon(moon: MoonConfig): void {
  const parent = bodyById.get(moon.parent);
  if (!parent) {
    console.warn(`[threejs-companion] Moon ${moon.id} parent ${moon.parent} missing; skipped`);
    return;
  }

  const visualRadius = Math.max(4, moon.earthRadiusScale * 30);
  const isProxy = moon.renderMode === 'proxy';

  let mesh: THREE.Object3D;
  let textureTarget: PlanetTextureLodTarget | undefined;

  if (isProxy) {
    const group = new THREE.Group();
    group.add(
      new THREE.Mesh(
        new THREE.OctahedronGeometry(visualRadius * 0.7, 0),
        new THREE.MeshBasicMaterial({
          color: moon.fallbackColor,
          wireframe: true,
          transparent: true,
          opacity: 0.8,
        }),
      ),
    );
    mesh = group;
  } else {
    const material = new THREE.MeshPhongMaterial({
      color: moon.fallbackColor,
      shininess: 4,
      specular: 0x111111,
    });
    const sphere = new THREE.Mesh(new THREE.SphereGeometry(visualRadius, 32, 24), material);
    mesh = sphere;
    textureTarget = {
      id: moon.id,
      name: moon.name,
      mesh: sphere,
      worldPosition: new THREE.Vector3(),
      lowUrl: textureUrl(moon.texture, lowTextureBase),
      highUrl: textureUrl(moon.texture, highTextureBase),
    };
  }

  mesh.name = moon.name;
  // Parent-relative: moons live in world space but track parent each frame.
  scene.add(mesh);

  const label = createBodyLabel(isProxy ? `${moon.name} · proxy` : moon.name, isProxy);
  label.addEventListener('click', () => focusBody(moon.id));
  document.body.appendChild(label);

  const anomaly = Math.random() * Math.PI * 2;
  const view: BodyView = {
    id: moon.id,
    name: moon.name,
    mode: isProxy ? 'proxy' : 'full',
    kind: 'moon',
    mesh,
    label,
    visualRadius,
    worldPosition: mesh.position,
    rotationRate: 0.01,
    textureTarget,
    parentId: moon.parent,
    orbitRadius: moon.orbitRadius,
    periodDays: moon.periodDays,
    anomaly,
  };
  bodies.push(view);
  bodyById.set(moon.id, view);

  if (textureTarget) {
    textureLodManager.register(textureTarget);
  }

  // Local orbit guide around parent (once per parent)
  if (!parent.mesh.userData.moonOrbitGuides) {
    parent.mesh.userData.moonOrbitGuides = new Set<number>();
  }
  const guides = parent.mesh.userData.moonOrbitGuides as Set<number>;
  if (!guides.has(moon.orbitRadius)) {
    guides.add(moon.orbitRadius);
    addLocalOrbitGuide(parent.worldPosition, moon.orbitRadius);
  }
}

function createBodyLabel(text: string, isProxy: boolean): HTMLButtonElement {
  const label = document.createElement('button');
  label.type = 'button';
  label.className = isProxy ? 'planet-label proxy-label' : 'planet-label';
  label.textContent = text;
  return label;
}

function textureUrl(fileName: string, base: string): string {
  return new URL(fileName, new URL(base, window.location.href)).toString();
}

function addOrbitGuide(radius: number, color = 0x25314a): void {
  if (radius < 1) return;
  const points: THREE.Vector3[] = [];
  for (let index = 0; index <= 128; index++) {
    const angle = (index / 128) * Math.PI * 2;
    points.push(new THREE.Vector3(Math.cos(angle) * radius, 0, Math.sin(angle) * radius));
  }
  const geometry = new THREE.BufferGeometry().setFromPoints(points);
  scene.add(
    new THREE.Line(
      geometry,
      new THREE.LineBasicMaterial({ color, transparent: true, opacity: 0.45 }),
    ),
  );
}

function addLocalOrbitGuide(center: THREE.Vector3, radius: number): void {
  const points: THREE.Vector3[] = [];
  for (let index = 0; index <= 96; index++) {
    const angle = (index / 96) * Math.PI * 2;
    points.push(
      new THREE.Vector3(
        center.x + Math.cos(angle) * radius,
        center.y,
        center.z + Math.sin(angle) * radius,
      ),
    );
  }
  const geometry = new THREE.BufferGeometry().setFromPoints(points);
  const line = new THREE.Line(
    geometry,
    new THREE.LineBasicMaterial({ color: 0x3d5a80, transparent: true, opacity: 0.35 }),
  );
  line.userData.orbitCenterId = true;
  // Rebuilt each frame via moon positions; keep static initial ring as cue.
  scene.add(line);
}

function updateTextureStatus(): void {
  const highBaseLabel = import.meta.env.VITE_KTX2_BASE?.trim() ? 'CDN high' : 'local high';
  const pending = pendingTextureLoads > 0 ? ` · loading ${pendingTextureLoads}` : '';
  const proxyCount = bodies.filter((b) => b.mode === 'proxy').length;
  textureStatusElement.textContent =
    `LOD low ${tierCounts.low} · high ${tierCounts.high} · proxies ${proxyCount} · ` +
    `${highBaseLabel}: ${highTextureBase}${pending}`;
}

function createFocusPresets(): void {
  focusElement.replaceChildren();
  focusElement.appendChild(createFocusButton('Overview', () => focusOverview()));
  focusElement.appendChild(createFocusButton('Sun', () => focusBody('sun')));

  for (const id of companionConfig.fullBodyIds) {
    const body = bodyById.get(id);
    if (body) {
      focusElement.appendChild(createFocusButton(body.name, () => focusBody(id)));
    }
  }

  // Compact proxy group
  if (companionConfig.proxyBodyIds.length > 0) {
    const proxyGroup = document.createElement('div');
    proxyGroup.className = 'proxy-focus-group';
    proxyGroup.setAttribute('aria-label', 'Outer-body proxies');
    for (const id of companionConfig.proxyBodyIds) {
      const body = bodyById.get(id);
      if (body) {
        const button = createFocusButton(`${body.name} ·`, () => focusBody(id));
        button.classList.add('proxy-focus');
        button.title = `${body.name} (proxy marker — not fully implemented)`;
        proxyGroup.appendChild(button);
      }
    }
    focusElement.appendChild(proxyGroup);
  }
}

function createFocusButton(label: string, action: () => void): HTMLButtonElement {
  const button = document.createElement('button');
  button.type = 'button';
  button.textContent = label;
  button.addEventListener('click', action);
  return button;
}

function focusBody(id: string): void {
  const body = bodyById.get(id);
  if (!body) return;
  const target = body.worldPosition.clone();
  const direction = camera.position.clone().sub(controls.target).normalize();
  if (direction.lengthSq() < 0.5) direction.set(0.7, 0.35, 0.7).normalize();
  const distance = Math.max(body.visualRadius * 7, body.mode === 'proxy' ? 180 : 100);
  startCameraTransition(target.clone().add(direction.multiplyScalar(distance)), target);
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
  const maxSpeed = 520;
  if (input.lengthSq() > 0) cameraVelocity.addScaledVector(input.normalize(), acceleration * deltaSeconds);
  cameraVelocity.multiplyScalar(Math.exp(-4.2 * deltaSeconds));
  cameraVelocity.clampLength(0, maxSpeed);
  const movement = cameraVelocity.clone().multiplyScalar(deltaSeconds);
  camera.position.add(movement);
  controls.target.add(movement);
}

function updateMoons(deltaSeconds: number): void {
  const simDays =
    deltaSeconds * (EARTH_YEAR_DAYS / EARTH_ORBIT_SECONDS_AT_1X);

  for (const body of bodies) {
    if (body.kind !== 'moon' || body.parentId === undefined || body.orbitRadius === undefined) {
      continue;
    }
    const parent = bodyById.get(body.parentId);
    if (!parent) continue;

    const period = body.periodDays && body.periodDays > 0 ? body.periodDays : 10;
    body.anomaly = (body.anomaly ?? 0) + (simDays / period) * Math.PI * 2;
    const x = Math.cos(body.anomaly) * body.orbitRadius;
    const z = Math.sin(body.anomaly) * body.orbitRadius;
    body.mesh.position.set(parent.worldPosition.x + x, parent.worldPosition.y, parent.worldPosition.z + z);
    body.worldPosition.copy(body.mesh.position);
    if (body.textureTarget) {
      body.textureTarget.worldPosition = body.worldPosition;
    }
  }
}

function updateLabels(): void {
  const projected = new THREE.Vector3();
  for (const body of bodies) {
    projected.copy(body.worldPosition).project(camera);
    const visible = projected.z > -1 && projected.z < 1;
    body.label.hidden = !visible;
    if (!visible) continue;
    body.label.style.transform =
      `translate(-50%, -50%) translate(${(projected.x * 0.5 + 0.5) * window.innerWidth}px, ` +
      `${(-projected.y * 0.5 + 0.5) * window.innerHeight}px)`;
  }
}

function animate(now: number): void {
  timer.update(now);
  const deltaSeconds = Math.min(timer.getDelta(), 0.05);
  updateCameraTransition(now);
  if (!cameraTransition) updateFlyCamera(deltaSeconds);

  for (const body of bodies) {
    if (body.kind === 'planet' || body.kind === 'sun') {
      body.mesh.rotation.y += body.rotationRate * deltaSeconds * 60;
    } else if (body.kind === 'proxy') {
      body.mesh.rotation.y += body.rotationRate * deltaSeconds * 40;
      body.mesh.rotation.x += body.rotationRate * deltaSeconds * 15;
    }
  }

  updateMoons(deltaSeconds);
  controls.update();

  const lodTargets = bodies
    .map((body) => body.textureTarget)
    .filter((target): target is PlanetTextureLodTarget => target !== undefined);
  textureLodManager.update(camera, lodTargets);
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
