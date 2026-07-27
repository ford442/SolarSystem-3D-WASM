import { copyShareableLink, type DeepLinkViewState } from './deepLink';
import {
    isoDateFromJulianDate,
    isoDateUtcNow,
    julianDateFromIsoDate,
    julianDateUtcNow,
} from './ephemeris';
import type { QualityPreset } from './SolarSystem.js';
import type { SettingsChangeField, SolarSystemRuntime } from './wasmBridge';
import { shadowQualityForPreset, subscribeSettingsChanges } from './wasmBridge';

const SETTINGS_STORAGE_KEY = 'solar-system.settings.v1';
const MIN_TIME_SCALE = 0.01;
const MAX_TIME_SCALE = 10000;

export interface PersistedSettings {
    quality: QualityPreset;
    timeScale: number;
    paused: boolean;
    shadows: boolean;
    orbitLines: boolean;
    musicVolume: number;
    musicMuted: boolean;
    simulationDate?: string;
}

export interface SettingsPanelElements {
    settingsPanel: HTMLElement;
    settingsToggle: HTMLButtonElement;
    settingsToggleIcon: HTMLElement;
    settingsFieldset: HTMLFieldSetElement;
    qualitySelect: HTMLSelectElement;
    timeScaleInput: HTMLInputElement;
    timeScaleValue: HTMLOutputElement;
    simulationDateInput: HTMLInputElement;
    simulationDateSet: HTMLButtonElement;
    simulationDateNow: HTMLButtonElement;
    pausedInput: HTMLInputElement;
    shadowsInput: HTMLInputElement;
    orbitLinesInput: HTMLInputElement;
    musicVolumeInput: HTMLInputElement;
    musicVolumeValue: HTMLOutputElement;
    musicMutedInput: HTMLInputElement;
    settingsReset: HTMLButtonElement;
    copyViewLinkButton: HTMLButtonElement;
    settingsStatus: HTMLElement;
}

export interface SettingsPanelInitOptions {
    elements: SettingsPanelElements;
    runtime: SolarSystemRuntime;
    deepLink: DeepLinkViewState;
    isMobileDevice: boolean;
    onSettingsChanged?: (field: SettingsChangeField) => void;
}

function isQualityPreset(value: unknown): value is QualityPreset {
    return value === 0 || value === 1 || value === 2;
}

function readPersistedSettings(): Partial<PersistedSettings> {
    try {
        const raw = window.localStorage.getItem(SETTINGS_STORAGE_KEY);
        if (!raw) return {};
        const parsed = JSON.parse(raw) as Record<string, unknown>;
        return {
            quality: isQualityPreset(parsed.quality) ? parsed.quality : undefined,
            timeScale: typeof parsed.timeScale === 'number' && Number.isFinite(parsed.timeScale)
                ? Math.min(MAX_TIME_SCALE, Math.max(MIN_TIME_SCALE, parsed.timeScale))
                : undefined,
            paused: typeof parsed.paused === 'boolean' ? parsed.paused : undefined,
            shadows: typeof parsed.shadows === 'boolean' ? parsed.shadows : undefined,
            orbitLines: typeof parsed.orbitLines === 'boolean' ? parsed.orbitLines : undefined,
            musicVolume: typeof parsed.musicVolume === 'number' && Number.isFinite(parsed.musicVolume)
                ? Math.min(100, Math.max(0, Math.round(parsed.musicVolume)))
                : undefined,
            musicMuted: typeof parsed.musicMuted === 'boolean' ? parsed.musicMuted : undefined,
            simulationDate: typeof parsed.simulationDate === 'string' && /^\d{4}-\d{2}-\d{2}$/.test(parsed.simulationDate)
                ? parsed.simulationDate
                : undefined,
        };
    } catch (error) {
        console.warn('[Settings] Could not read saved settings:', error);
        return {};
    }
}

function formatTimeScale(scale: number): string {
    if (scale < 0.1) return `${scale.toFixed(2)}×`;
    if (scale < 10) return `${scale.toFixed(scale < 1 ? 2 : 1).replace(/\.0+$/, '')}×`;
    return `${Math.round(scale).toLocaleString()}×`;
}

export function initSettingsPanel(options: SettingsPanelInitOptions): void {
    const { elements, runtime, deepLink, isMobileDevice } = options;
    const {
        settingsPanel,
        settingsToggle,
        settingsToggleIcon,
        settingsFieldset,
        qualitySelect,
        timeScaleInput,
        timeScaleValue,
        simulationDateInput,
        simulationDateSet,
        simulationDateNow,
        pausedInput,
        shadowsInput,
        orbitLinesInput,
        musicVolumeInput,
        musicVolumeValue,
        musicMutedInput,
        settingsReset,
        copyViewLinkButton,
        settingsStatus,
    } = elements;

    function currentPanelSettings(): PersistedSettings {
        return {
            quality: Number(qualitySelect.value) as QualityPreset,
            timeScale: Math.pow(10, Number(timeScaleInput.value)),
            paused: pausedInput.checked,
            shadows: shadowsInput.checked,
            orbitLines: orbitLinesInput.checked,
            musicVolume: Number(musicVolumeInput.value),
            musicMuted: musicMutedInput.checked,
            simulationDate: simulationDateInput.value || undefined,
        };
    }

    function persistPanelSettings(): void {
        try {
            window.localStorage.setItem(SETTINGS_STORAGE_KEY, JSON.stringify(currentPanelSettings()));
        } catch (error) {
            console.warn('[Settings] Could not save settings:', error);
        }
    }

    function updateMusicVolumeDisplay(percent: number): void {
        const label = `${percent}%`;
        musicVolumeValue.value = label;
        musicVolumeValue.textContent = label;
    }

    function applyMusicVolumePercent(percent: number): void {
        const clamped = Math.min(100, Math.max(0, percent));
        musicVolumeInput.value = String(clamped);
        updateMusicVolumeDisplay(clamped);
        runtime.setMusicVolume(clamped / 100);
    }

    function updateTimeScaleDisplay(scale: number): void {
        const label = formatTimeScale(scale);
        timeScaleValue.value = label;
        timeScaleValue.textContent = label;
    }

    function applySimulationDateIso(isoDate: string, statusMessage?: string): void {
        const jd = julianDateFromIsoDate(isoDate);
        if (jd === undefined) {
            settingsStatus.textContent = 'Invalid date';
            return;
        }
        simulationDateInput.value = isoDate;
        runtime.setSimulationEpoch(jd);
        settingsStatus.textContent = statusMessage ?? `Date set to ${isoDate}`;
        persistPanelSettings();
    }

    function syncSimulationDateFromRuntime(): void {
        if (document.activeElement === simulationDateInput) {
            return;
        }
        const jd = runtime.getSimulationEpoch();
        if (!Number.isFinite(jd)) {
            return;
        }
        simulationDateInput.value = isoDateFromJulianDate(jd);
    }

    function syncFieldFromRuntime(field: SettingsChangeField): void {
        switch (field) {
            case 'quality':
                if (document.activeElement !== qualitySelect) {
                    qualitySelect.value = String(runtime.getQualityPreset());
                }
                break;
            case 'timeScale': {
                if (document.activeElement !== timeScaleInput) {
                    const runtimeScale = runtime.getTimeScale();
                    timeScaleInput.value = String(Math.log10(runtimeScale));
                    updateTimeScaleDisplay(runtimeScale);
                }
                break;
            }
            case 'paused':
                pausedInput.checked = runtime.getPaused();
                break;
            case 'shadowQuality':
                shadowsInput.checked = runtime.getShadowQuality() > 0;
                break;
            case 'musicVolume':
                if (document.activeElement !== musicVolumeInput) {
                    applyMusicVolumePercent(Math.round(runtime.getMusicVolume() * 100));
                }
                break;
            case 'musicMuted':
                musicMutedInput.checked = runtime.getMusicMuted();
                break;
            case 'simulationEpoch':
                syncSimulationDateFromRuntime();
                break;
            case 'orbitLines':
                orbitLinesInput.checked = runtime.getOrbitLines();
                break;
        }
    }

    function setPanelCollapsed(collapsed: boolean): void {
        settingsPanel.classList.toggle('is-collapsed', collapsed);
        settingsToggle.setAttribute('aria-expanded', String(!collapsed));
        settingsToggleIcon.textContent = collapsed ? '+' : '−';
    }

    settingsToggle.addEventListener('click', () => {
        setPanelCollapsed(!settingsPanel.classList.contains('is-collapsed'));
    });

    if (isMobileDevice) {
        setPanelCollapsed(true);
    }

    const saved = readPersistedSettings();
    const defaultQuality: QualityPreset = isMobileDevice ? 0 : 2;
    const quality = deepLink.quality ?? saved.quality ?? defaultQuality;
    const timeScale = deepLink.timeScale ?? saved.timeScale ?? runtime.getTimeScale();
    const paused = deepLink.paused ?? saved.paused ?? runtime.getPaused();
    const shadows = deepLink.shadows ?? saved.shadows ?? true;
    const orbitLines = deepLink.orbitLines ?? saved.orbitLines ?? true;
    const musicVolumePercent = saved.musicVolume ?? Math.round(runtime.getMusicVolume() * 100);
    const musicMuted = saved.musicMuted ?? runtime.getMusicMuted();
    const simulationDate = deepLink.simulationDate
        ?? saved.simulationDate
        ?? isoDateFromJulianDate(runtime.getSimulationEpoch());
    const orbitScale = deepLink.orbitScale;

    qualitySelect.value = String(quality);
    timeScaleInput.value = String(Math.log10(timeScale));
    updateTimeScaleDisplay(timeScale);
    simulationDateInput.value = simulationDate;
    pausedInput.checked = paused;
    shadowsInput.checked = shadows;
    orbitLinesInput.checked = orbitLines;
    applyMusicVolumePercent(musicVolumePercent);
    musicMutedInput.checked = musicMuted;

    runtime.setQualityPreset(quality);
    runtime.setTimeScale(timeScale);
    runtime.setPaused(paused);
    if (orbitScale === 0 || orbitScale === 1) {
        runtime.setOrbitScaleMode(orbitScale);
    }
    {
        const jd = julianDateFromIsoDate(simulationDate);
        if (jd !== undefined) {
            runtime.setSimulationEpoch(jd);
        }
    }
    runtime.setShadowQuality(shadowQualityForPreset(quality, shadows));
    runtime.setOrbitLines(orbitLines);
    runtime.setMusicMuted(musicMuted);
    if (!musicMuted) {
        runtime.setMusicVolume(musicVolumePercent / 100);
    }

    if (deepLink.camera) {
        runtime.setCameraPose(
            deepLink.camera.x,
            deepLink.camera.y,
            deepLink.camera.z,
            deepLink.camera.yaw,
            deepLink.camera.pitch,
        );
    } else if (deepLink.planet !== undefined) {
        runtime.focusPlanet(deepLink.planet);
    }

    settingsFieldset.disabled = false;
    settingsStatus.textContent = 'Controls ready';
    persistPanelSettings();

    qualitySelect.addEventListener('change', () => {
        const preset = Number(qualitySelect.value) as QualityPreset;
        runtime.setQualityPreset(preset);
        if (shadowsInput.checked) {
            runtime.setShadowQuality(shadowQualityForPreset(preset, true));
        }
        settingsStatus.textContent = `Quality set to ${qualitySelect.selectedOptions[0]?.text ?? preset}`;
        persistPanelSettings();
    });

    timeScaleInput.addEventListener('input', () => {
        const scale = Math.pow(10, Number(timeScaleInput.value));
        updateTimeScaleDisplay(scale);
        runtime.setTimeScale(scale);
        settingsStatus.textContent = `Animation speed ${formatTimeScale(scale)}`;
        persistPanelSettings();
    });

    simulationDateSet.addEventListener('click', () => {
        applySimulationDateIso(simulationDateInput.value);
    });

    simulationDateInput.addEventListener('change', () => {
        applySimulationDateIso(simulationDateInput.value);
    });

    simulationDateNow.addEventListener('click', () => {
        const iso = isoDateUtcNow();
        simulationDateInput.value = iso;
        runtime.setSimulationEpoch(julianDateUtcNow());
        settingsStatus.textContent = `Date set to now (${iso} UTC)`;
        persistPanelSettings();
    });

    pausedInput.addEventListener('change', () => {
        runtime.setPaused(pausedInput.checked);
        settingsStatus.textContent = pausedInput.checked ? 'Animation paused' : 'Animation resumed';
        persistPanelSettings();
    });

    shadowsInput.addEventListener('change', () => {
        const preset = Number(qualitySelect.value) as QualityPreset;
        runtime.setShadowQuality(shadowQualityForPreset(preset, shadowsInput.checked));
        settingsStatus.textContent = shadowsInput.checked ? 'Shadows enabled' : 'Shadows disabled';
        persistPanelSettings();
    });

    orbitLinesInput.addEventListener('change', () => {
        runtime.setOrbitLines(orbitLinesInput.checked);
        settingsStatus.textContent = orbitLinesInput.checked ? 'Orbit lines enabled' : 'Orbit lines disabled';
        persistPanelSettings();
    });

    musicVolumeInput.addEventListener('input', () => {
        const percent = Number(musicVolumeInput.value);
        updateMusicVolumeDisplay(percent);
        if (!musicMutedInput.checked) {
            runtime.setMusicVolume(percent / 100);
        }
        settingsStatus.textContent = `Music volume ${percent}%`;
        persistPanelSettings();
    });

    musicMutedInput.addEventListener('change', () => {
        runtime.setMusicMuted(musicMutedInput.checked);
        if (!musicMutedInput.checked) {
            runtime.setMusicVolume(Number(musicVolumeInput.value) / 100);
        }
        settingsStatus.textContent = musicMutedInput.checked ? 'Music muted' : 'Music unmuted';
        persistPanelSettings();
    });

    copyViewLinkButton.addEventListener('click', () => {
        void copyShareableLink({
            getQualityPreset: () => runtime.getQualityPreset(),
            getTimeScale: () => runtime.getTimeScale(),
            getPaused: () => (runtime.getPaused() ? 1 : 0),
            getSimulationEpoch: () => runtime.getSimulationEpoch(),
            getOrbitScaleMode: () => runtime.getOrbitScaleMode(),
            getShadowQuality: () => runtime.getShadowQuality(),
            getOrbitLines: () => (runtime.getOrbitLines() ? 1 : 0),
            getFocusedPlanetIndex: () => runtime.getFocusedPlanetIndex(),
            getCameraPosition: () => runtime.getCameraPosition(),
            getCameraYaw: () => runtime.getCameraYaw(),
            getCameraPitch: () => runtime.getCameraPitch(),
            isoDateFromJulianDate,
        })
            .then(() => {
                settingsStatus.textContent = 'View link copied to clipboard';
            })
            .catch((error: unknown) => {
                console.warn('[DeepLink] copy failed:', error);
                settingsStatus.textContent = 'Could not copy link';
            });
    });

    settingsReset.addEventListener('click', () => {
        const resetQuality: QualityPreset = isMobileDevice ? 0 : 2;
        const resetShadowQuality = shadowQualityForPreset(resetQuality, true);
        qualitySelect.value = String(resetQuality);
        timeScaleInput.value = '0';
        pausedInput.checked = false;
        shadowsInput.checked = true;
        orbitLinesInput.checked = true;
        musicMutedInput.checked = false;
        updateTimeScaleDisplay(1);
        applyMusicVolumePercent(30);
        runtime.setQualityPreset(resetQuality);
        runtime.setTimeScale(1);
        runtime.setPaused(false);
        runtime.setShadowQuality(resetShadowQuality);
        runtime.setOrbitLines(true);
        runtime.setMusicMuted(false);
        runtime.setMusicVolume(0.3);
        const nowIso = isoDateUtcNow();
        simulationDateInput.value = nowIso;
        runtime.setSimulationEpoch(julianDateUtcNow());
        settingsStatus.textContent = 'Settings reset';
        persistPanelSettings();
    });

    subscribeSettingsChanges(syncFieldFromRuntime);
}