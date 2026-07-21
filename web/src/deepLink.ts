import type { OrbitScaleMode, PlanetIndex, QualityPreset } from './SolarSystem.js';

export interface DeepLinkViewState {
    quality?: QualityPreset;
    timeScale?: number;
    paused?: boolean;
    simulationDate?: string;
    orbitScale?: OrbitScaleMode;
    shadows?: boolean;
    orbitLines?: boolean;
    planet?: PlanetIndex;
    camera?: {
        x: number;
        y: number;
        z: number;
        yaw: number;
        pitch: number;
    };
}

export interface DeepLinkRuntimeReaders {
    getQualityPreset?: () => QualityPreset;
    getTimeScale?: () => number;
    getPaused?: () => number;
    getSimulationEpoch?: () => number;
    getOrbitScaleMode?: () => OrbitScaleMode;
    getShadowQuality?: () => number;
    getOrbitLines?: () => number;
    getFocusedPlanetIndex?: () => number;
    getCameraPosition?: () => { x: number; y: number; z: number };
    getCameraYaw?: () => number;
    getCameraPitch?: () => number;
    isoDateFromJulianDate?: (jd: number) => string;
}

const PLANET_IDS: Record<string, PlanetIndex> = {
    sun: 0,
    mercury: 1,
    venus: 2,
    earth: 3,
    mars: 4,
    jupiter: 5,
    saturn: 6,
    uranus: 7,
    neptune: 8,
    pluto: 9,
};

const PLANET_ID_BY_INDEX = Object.fromEntries(
    Object.entries(PLANET_IDS).map(([id, index]) => [index, id]),
) as Record<PlanetIndex, string>;

function parseFiniteNumber(value: string | null): number | undefined {
    if (!value) return undefined;
    const parsed = Number(value);
    return Number.isFinite(parsed) ? parsed : undefined;
}

function parseBooleanParam(value: string | null): boolean | undefined {
    if (!value) return undefined;
    const normalized = value.toLowerCase();
    if (normalized === '1' || normalized === 'true' || normalized === 'yes' || normalized === 'on') {
        return true;
    }
    if (normalized === '0' || normalized === 'false' || normalized === 'no' || normalized === 'off') {
        return false;
    }
    return undefined;
}

function parseQuality(value: string | null): QualityPreset | undefined {
    if (!value) return undefined;
    const normalized = value.toLowerCase();
    if (normalized === 'low' || normalized === '0') return 0;
    if (normalized === 'medium' || normalized === 'med' || normalized === '1') return 1;
    if (normalized === 'full' || normalized === '2') return 2;
    const asNumber = Number(value);
    if (asNumber === 0 || asNumber === 1 || asNumber === 2) return asNumber;
    return undefined;
}

function parsePlanetIndex(value: string | null): PlanetIndex | undefined {
    if (!value) return undefined;
    const normalized = value.toLowerCase();
    if (normalized in PLANET_IDS) {
        return PLANET_IDS[normalized];
    }
    const asNumber = Number(value);
    if (Number.isInteger(asNumber) && asNumber >= 0 && asNumber <= 9) {
        return asNumber as PlanetIndex;
    }
    return undefined;
}

function parseOrbitScale(value: string | null): OrbitScaleMode | undefined {
    if (!value) return undefined;
    const normalized = value.toLowerCase();
    if (normalized === 'compressed' || normalized === 'art' || normalized === '0') return 0;
    if (normalized === 'realistic' || normalized === 'au' || normalized === '1') return 1;
    const asNumber = Number(value);
    if (asNumber === 0 || asNumber === 1) return asNumber;
    return undefined;
}

function parseSimulationDate(value: string | null): string | undefined {
    if (!value) return undefined;
    return /^\d{4}-\d{2}-\d{2}$/.test(value) ? value : undefined;
}

function roundCoord(value: number): number {
    return Math.round(value * 100) / 100;
}

function roundAngle(value: number): number {
    return Math.round(value * 10) / 10;
}

/** Parse shareable view state from the current page URL. */
export function parseDeepLinkFromUrl(search = window.location.search): DeepLinkViewState {
    const params = new URLSearchParams(search);
    const quality = parseQuality(params.get('quality') ?? params.get('q'));
    const timeScale = parseFiniteNumber(params.get('time') ?? params.get('ts'));
    const paused = parseBooleanParam(params.get('paused') ?? params.get('p'));
    const simulationDate = parseSimulationDate(params.get('date'));
    const orbitScale = parseOrbitScale(params.get('orbit') ?? params.get('scale'));
    const shadows = parseBooleanParam(params.get('shadows'));
    const orbitLines = parseBooleanParam(params.get('orbits') ?? params.get('orbitLines'));
    const planet = parsePlanetIndex(params.get('planet') ?? params.get('focus'));

    const x = parseFiniteNumber(params.get('x'));
    const y = parseFiniteNumber(params.get('y'));
    const z = parseFiniteNumber(params.get('z'));
    const yaw = parseFiniteNumber(params.get('yaw'));
    const pitch = parseFiniteNumber(params.get('pitch'));
    const camera = x !== undefined && y !== undefined && z !== undefined
        && yaw !== undefined && pitch !== undefined
        ? { x, y, z, yaw, pitch }
        : undefined;

    return {
        quality,
        timeScale,
        paused,
        simulationDate,
        orbitScale,
        shadows,
        orbitLines,
        planet,
        camera,
    };
}

export function hasDeepLinkViewState(state: DeepLinkViewState): boolean {
    return Object.values(state).some((value) => value !== undefined);
}

/** Build a shareable URL for the current simulation view. */
export function buildShareableUrl(
    readers: DeepLinkRuntimeReaders,
    baseUrl = window.location.href,
): string {
    const url = new URL(baseUrl);
    url.search = '';

    const quality = readers.getQualityPreset?.();
    if (quality !== undefined) {
        url.searchParams.set('quality', String(quality));
    }

    const timeScale = readers.getTimeScale?.();
    if (timeScale !== undefined && Number.isFinite(timeScale)) {
        url.searchParams.set('time', String(roundCoord(timeScale)));
    }

    const paused = readers.getPaused?.();
    if (paused !== undefined) {
        url.searchParams.set('paused', paused ? '1' : '0');
    }

    const jd = readers.getSimulationEpoch?.();
    if (jd !== undefined && Number.isFinite(jd) && readers.isoDateFromJulianDate) {
        url.searchParams.set('date', readers.isoDateFromJulianDate(jd));
    }

    const orbitScale = readers.getOrbitScaleMode?.();
    if (orbitScale !== undefined) {
        url.searchParams.set('orbit', String(orbitScale));
    }

    const shadowQuality = readers.getShadowQuality?.();
    if (shadowQuality !== undefined) {
        url.searchParams.set('shadows', shadowQuality > 0 ? '1' : '0');
    }

    const orbitLines = readers.getOrbitLines?.();
    if (orbitLines !== undefined) {
        url.searchParams.set('orbits', orbitLines ? '1' : '0');
    }

    const focusedPlanet = readers.getFocusedPlanetIndex?.() ?? -1;
    if (focusedPlanet >= 0 && focusedPlanet <= 9) {
        url.searchParams.set('planet', PLANET_ID_BY_INDEX[focusedPlanet as PlanetIndex]);
    }

    const position = readers.getCameraPosition?.();
    const yaw = readers.getCameraYaw?.();
    const pitch = readers.getCameraPitch?.();
    if (position && yaw !== undefined && pitch !== undefined) {
        url.searchParams.set('x', String(roundCoord(position.x)));
        url.searchParams.set('y', String(roundCoord(position.y)));
        url.searchParams.set('z', String(roundCoord(position.z)));
        url.searchParams.set('yaw', String(roundAngle(yaw)));
        url.searchParams.set('pitch', String(roundAngle(pitch)));
    }

    return url.toString();
}

export async function copyShareableLink(
    readers: DeepLinkRuntimeReaders,
    baseUrl = window.location.href,
): Promise<string> {
    const link = buildShareableUrl(readers, baseUrl);
    if (navigator.clipboard?.writeText) {
        await navigator.clipboard.writeText(link);
    } else {
        const textarea = document.createElement('textarea');
        textarea.value = link;
        textarea.setAttribute('readonly', '');
        textarea.style.position = 'fixed';
        textarea.style.left = '-9999px';
        document.body.appendChild(textarea);
        textarea.select();
        document.execCommand('copy');
        textarea.remove();
    }
    return link;
}
