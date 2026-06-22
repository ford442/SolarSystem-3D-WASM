// @ts-nocheck  -- WebGPURenderer import is experimental; runtime works, types incomplete in current @types
import * as THREE from 'three';
import { WebGPURenderer as WebGPURendererCtor } from 'three/webgpu';
const WebGPURenderer: any = WebGPURendererCtor;
import { OrbitControls } from 'three/addons/controls/OrbitControls.js';

// Phase 0: minimal Three.js + WebGPU companion for solar system exploration.
// Mirrors spirit of main demo but for rapid UI/effects iteration. No C++ involved.

let renderer: THREE.WebGPURenderer | THREE.WebGLRenderer;
let scene: THREE.Scene;
let camera: THREE.PerspectiveCamera;
let controls: OrbitControls;
let earth: THREE.Mesh;
let sunLight: THREE.PointLight;

const infoEl = document.getElementById('info')!;

async function init() {
  const canvas = document.createElement('canvas');
  document.body.appendChild(canvas);

  // Prefer WebGPU, fallback to WebGLRenderer
  let useWebGPU = true;
  try {
    renderer = new WebGPURenderer({ canvas, antialias: true, forceWebGL: false });
    // @ts-ignore - init may be needed for WebGPU backend
    if (typeof (renderer as any).init === 'function') {
      await (renderer as any).init();
    }
    infoEl.textContent += ' | WebGPU';
  } catch (err) {
    console.warn('WebGPU unavailable, falling back to WebGL:', err);
    useWebGPU = false;
    renderer = new THREE.WebGLRenderer({ canvas, antialias: true });
    infoEl.textContent += ' | WebGL fallback';
  }

  renderer.setPixelRatio(Math.min(window.devicePixelRatio, 2));
  renderer.setSize(window.innerWidth, window.innerHeight);

  scene = new THREE.Scene();

  camera = new THREE.PerspectiveCamera(60, window.innerWidth / window.innerHeight, 0.1, 1000);
  camera.position.set(0, 0, 5);

  // Sun light (point)
  sunLight = new THREE.PointLight(0xffffff, 2, 0, 1);
  sunLight.position.set(0, 0, 0);
  scene.add(sunLight);

  // Ambient for basic visibility
  scene.add(new THREE.AmbientLight(0x202020));

  // Earth textured sphere (procedural "texture" for Phase 0)
  const earthGeometry = new THREE.SphereGeometry(1, 64, 64);
  const earthMaterial = createEarthMaterial();
  earth = new THREE.Mesh(earthGeometry, earthMaterial);
  scene.add(earth);

  // Simple "sun" sphere for reference
  const sunGeo = new THREE.SphereGeometry(0.4, 32, 32);
  const sunMat = new THREE.MeshBasicMaterial({ color: 0xffdd44 });
  const sun = new THREE.Mesh(sunGeo, sunMat);
  scene.add(sun);

  // Controls - orbit style matching "exploration" feel (can be swapped for FPS later)
  controls = new OrbitControls(camera, renderer.domElement);
  controls.enableDamping = true;
  controls.dampingFactor = 0.1;
  controls.minDistance = 1.5;
  controls.maxDistance = 50;

  // Resize handler
  window.addEventListener('resize', () => {
    camera.aspect = window.innerWidth / window.innerHeight;
    camera.updateProjectionMatrix();
    renderer.setSize(window.innerWidth, window.innerHeight);
  });

  // Animate
  animate();

  console.log('[threejs-companion] Phase 0 ready. WebGPU path:', useWebGPU);
  infoEl.textContent = infoEl.textContent.replace(/Phase 0.*/, 'Phase 0: rotating Earth. Drag to orbit, scroll to zoom.');
}

function createEarthMaterial() {
  // Simple procedural canvas "texture" for Earth-like look (blue oceans, green/brown land, lat/lon lines)
  const size = 512;
  const canvas = document.createElement('canvas');
  canvas.width = canvas.height = size;
  const ctx = canvas.getContext('2d')!;

  // Ocean
  ctx.fillStyle = '#1a3c6e';
  ctx.fillRect(0, 0, size, size);

  // Fake continents
  ctx.fillStyle = '#2e6b3e';
  ctx.beginPath();
  ctx.ellipse(size * 0.3, size * 0.4, size * 0.25, size * 0.15, 0.3, 0, Math.PI * 2);
  ctx.fill();
  ctx.fillStyle = '#4a3c2a';
  ctx.beginPath();
  ctx.ellipse(size * 0.65, size * 0.55, size * 0.2, size * 0.18, -0.5, 0, Math.PI * 2);
  ctx.fill();

  // Latitude / longitude grid
  ctx.strokeStyle = 'rgba(255,255,255,0.25)';
  ctx.lineWidth = 1;
  for (let i = 1; i < 8; i++) {
    const y = (size / 8) * i;
    ctx.beginPath(); ctx.moveTo(0, y); ctx.lineTo(size, y); ctx.stroke();
  }
  for (let i = 1; i < 16; i++) {
    const x = (size / 16) * i;
    ctx.beginPath(); ctx.moveTo(x, 0); ctx.lineTo(x, size); ctx.stroke();
  }

  const texture = new THREE.CanvasTexture(canvas);
  texture.wrapS = texture.wrapT = THREE.RepeatWrapping;

  return new THREE.MeshPhongMaterial({
    map: texture,
    shininess: 5,
    specular: 0x222222,
  });
}

function animate() {
  requestAnimationFrame(animate);

  // Slow planet rotation (matches demo feel)
  earth.rotation.y += 0.002;

  controls.update();
  renderer.render(scene, camera);
}

init().catch(err => {
  console.error(err);
  document.body.innerHTML = `<pre style="color:red">Init failed: ${err}</pre>`;
});
