import type { KTX2Loader } from 'three/addons/loaders/KTX2Loader.js';
import * as THREE from 'three';

/** Match C++ `Planet::_lodThreshold` upgrade distance. */
export const LOD_UPGRADE_DISTANCE = 50;

/** Match C++ downgrade at `threshold * 2.0f`. */
export const LOD_DOWNGRADE_DISTANCE = 100;

/** Match C++ cancel-in-flight at `threshold * 1.8f`. */
export const LOD_CANCEL_DISTANCE = 90;

export type TextureTier = 'low' | 'high';

export interface PlanetTextureLodTarget {
  id: string;
  name: string;
  mesh: THREE.Mesh<THREE.SphereGeometry, THREE.MeshPhongMaterial>;
  worldPosition: THREE.Vector3;
  lowUrl: string;
  highUrl: string;
}

interface PlanetLodState {
  lowTexture: THREE.CompressedTexture | null;
  highTexture: THREE.CompressedTexture | null;
  activeTier: TextureTier;
  loadGeneration: number;
}

interface QueuedUpgrade {
  target: PlanetTextureLodTarget;
  distance: number;
}

/**
 * Distance-driven low→high KTX2 swaps with hysteresis and a single in-flight
 * high-res fetch, mirroring the C++ TextureLODController / TextureLoadingQueue.
 */
export class PlanetTextureLodManager {
  private readonly states = new Map<string, PlanetLodState>();
  private readonly scratchPosition = new THREE.Vector3();
  private inFlightPlanetId: string | null = null;
  private readonly loader: KTX2Loader;
  private readonly onTierChange?: (planetId: string, tier: TextureTier) => void;

  constructor(
    loader: KTX2Loader,
    onTierChange?: (planetId: string, tier: TextureTier) => void,
  ) {
    this.loader = loader;
    this.onTierChange = onTierChange;
  }

  register(target: PlanetTextureLodTarget): void {
    this.states.set(target.id, {
      lowTexture: null,
      highTexture: null,
      activeTier: 'low',
      loadGeneration: 0,
    });
    void this.loadLowTexture(target);
  }

  update(camera: THREE.PerspectiveCamera, targets: PlanetTextureLodTarget[]): void {
    const upgrades: QueuedUpgrade[] = [];

    for (const target of targets) {
      const distance = this.distanceTo(camera, target);
      const state = this.mustGetState(target.id);

      if (state.activeTier === 'high' && distance > LOD_DOWNGRADE_DISTANCE) {
        this.downgrade(target, state);
        continue;
      }

      if (this.isLoading(state) && distance > LOD_CANCEL_DISTANCE) {
        this.cancelInFlight(target.id, state);
      }

      if (state.activeTier === 'low' && !this.isLoading(state) && distance < LOD_UPGRADE_DISTANCE) {
        upgrades.push({ target, distance });
      }
    }

    if (this.inFlightPlanetId !== null || upgrades.length === 0) {
      return;
    }

    upgrades.sort((left, right) => left.distance - right.distance);
    this.startHighUpgrade(camera, upgrades[0].target);
  }

  dispose(): void {
    for (const state of this.states.values()) {
      state.lowTexture?.dispose();
      state.highTexture?.dispose();
    }
    this.states.clear();
    this.inFlightPlanetId = null;
  }

  getActiveTier(planetId: string): TextureTier {
    return this.states.get(planetId)?.activeTier ?? 'low';
  }

  private mustGetState(planetId: string): PlanetLodState {
    const state = this.states.get(planetId);
    if (!state) {
      throw new Error(`Missing texture LOD state for ${planetId}`);
    }
    return state;
  }

  private isLoading(state: PlanetLodState): boolean {
    return state.loadGeneration > 0;
  }

  private distanceTo(camera: THREE.PerspectiveCamera, target: PlanetTextureLodTarget): number {
    target.mesh.getWorldPosition(this.scratchPosition);
    return camera.position.distanceTo(this.scratchPosition);
  }

  private async loadLowTexture(target: PlanetTextureLodTarget): Promise<void> {
    const state = this.mustGetState(target.id);

    try {
      const texture = await this.loader.loadAsync(target.lowUrl);
      this.configureTexture(texture);
      state.lowTexture = texture;
      if (state.activeTier === 'low') {
        this.setActiveTexture(target, state, texture, 'low');
      }
      console.info(`[texture-lod] ${target.name} low KTX2 ready`, target.lowUrl);
    } catch (error) {
      console.warn(`[texture-lod] ${target.name} low KTX2 unavailable; keeping color stub.`, target.lowUrl, error);
    }
  }

  private startHighUpgrade(camera: THREE.PerspectiveCamera, target: PlanetTextureLodTarget): void {
    const state = this.mustGetState(target.id);
    const generation = ++state.loadGeneration;
    this.inFlightPlanetId = target.id;

    void this.loader.loadAsync(target.highUrl)
      .then((texture) => {
        if (state.loadGeneration !== generation) {
          texture.dispose();
          return;
        }

        state.loadGeneration = 0;
        if (this.inFlightPlanetId === target.id) {
          this.inFlightPlanetId = null;
        }

        if (this.distanceTo(camera, target) > LOD_CANCEL_DISTANCE) {
          texture.dispose();
          console.info(`[texture-lod] discarded late high KTX2 for ${target.name} (camera moved away)`);
          return;
        }

        if (state.highTexture && state.highTexture !== texture) {
          state.highTexture.dispose();
        }
        state.highTexture = texture;
        this.configureTexture(texture);
        this.setActiveTexture(target, state, texture, 'high');
        console.info(`[texture-lod] ${target.name} upgraded to high KTX2`, target.highUrl);
      })
      .catch((error: unknown) => {
        if (state.loadGeneration === generation) {
          state.loadGeneration = 0;
          if (this.inFlightPlanetId === target.id) {
            this.inFlightPlanetId = null;
          }
        }
        console.warn(`[texture-lod] ${target.name} high KTX2 unavailable.`, target.highUrl, error);
      });
  }

  private downgrade(target: PlanetTextureLodTarget, state: PlanetLodState): void {
    if (this.isLoading(state)) {
      this.cancelInFlight(target.id, state);
    }

    if (state.highTexture) {
      state.highTexture.dispose();
      state.highTexture = null;
    }

    if (state.lowTexture) {
      this.setActiveTexture(target, state, state.lowTexture, 'low');
      console.info(`[texture-lod] ${target.name} downgraded to low KTX2`);
    } else {
      state.activeTier = 'low';
      this.onTierChange?.(target.id, 'low');
    }
  }

  private cancelInFlight(planetId: string, state: PlanetLodState): void {
    state.loadGeneration++;
    if (this.inFlightPlanetId === planetId) {
      this.inFlightPlanetId = null;
    }
    console.info(`[texture-lod] cancelled distant high-res load for ${planetId}`);
  }

  private setActiveTexture(
    target: PlanetTextureLodTarget,
    state: PlanetLodState,
    texture: THREE.CompressedTexture,
    tier: TextureTier,
  ): void {
    target.mesh.material.map = texture;
    target.mesh.material.color.set(0xffffff);
    target.mesh.material.needsUpdate = true;
    state.activeTier = tier;
    this.onTierChange?.(target.id, tier);
  }

  private configureTexture(texture: THREE.CompressedTexture): void {
    texture.colorSpace = THREE.SRGBColorSpace;
    texture.anisotropy = 4;
  }
}
