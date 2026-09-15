import { copyShareableLink, type DeepLinkViewState } from './deepLink.js';
import { isoDateFromJulianDate } from './ephemeris.js';
import { createTourPlayer, loadTour, type TourPlayer } from './tourPlayer.js';
import type { SolarSystemRuntime } from './wasmBridge.js';

export function initEducationalLayer(options: {
    runtime: SolarSystemRuntime;
    deepLink: DeepLinkViewState;
    missionList: HTMLElement;
    missionsRoot: HTMLElement;
    tourPlay: HTMLButtonElement;
    tourStop: HTMLButtonElement;
    tourCopyStep: HTMLButtonElement;
    tourCaption: HTMLElement;
    settingsStatus: HTMLElement;
}): TourPlayer {
    const {
        runtime,
        deepLink,
        missionList,
        missionsRoot,
        tourPlay,
        tourStop,
        tourCopyStep,
        tourCaption,
        settingsStatus,
    } = options;

    const catalog = runtime.getMissionCatalog();
    missionsRoot.hidden = catalog.length === 0;
    missionList.replaceChildren();
    for (const mission of catalog) {
        const button = document.createElement('button');
        button.type = 'button';
        button.className = 'explorer-body-btn';
        button.dataset.missionIndex = String(mission.index);
        button.textContent = mission.name;
        button.addEventListener('click', () => {
            runtime.focusMission(mission.index);
            settingsStatus.textContent = `Following ${mission.name}`;
            highlightMission(mission.index);
        });
        missionList.appendChild(button);
    }

    const highlightMission = (index: number): void => {
        for (const button of missionList.querySelectorAll<HTMLButtonElement>('.explorer-body-btn')) {
            button.classList.toggle('is-selected', Number(button.dataset.missionIndex) === index);
        }
    };

    window.setInterval(() => {
        highlightMission(runtime.getFocusedMissionIndex());
    }, 750);

    if (deepLink.mission) {
        const match = catalog.find((mission) => mission.id === deepLink.mission);
        if (match) {
            highlightMission(match.index);
        }
    }

    const player = createTourPlayer({
        runtime,
        onCaption: (text) => {
            tourCaption.hidden = !text;
            tourCaption.textContent = text;
        },
        onStatus: (text) => {
            settingsStatus.textContent = text;
            if (text.endsWith('complete') || text === 'Tour stopped') {
                tourStop.hidden = true;
                tourCopyStep.hidden = true;
                tourPlay.disabled = false;
            }
        },
        onStep: () => {
            tourCopyStep.hidden = false;
        },
    });

    const setTourPlaying = (playing: boolean): void => {
        tourStop.hidden = !playing;
        tourCopyStep.hidden = !playing;
        tourPlay.disabled = playing;
    };

    tourPlay.addEventListener('click', () => {
        void loadTour('inner-system').then((tour) => {
            if (!tour) {
                settingsStatus.textContent = 'Could not load tour';
                return;
            }
            player.play(tour, 0);
            setTourPlaying(true);
        }).catch((error: unknown) => {
            console.warn('[Tour] load failed:', error);
            settingsStatus.textContent = 'Could not load tour';
        });
    });

    tourStop.addEventListener('click', () => {
        player.stop();
        setTourPlaying(false);
    });

    tourCopyStep.addEventListener('click', () => {
        const tour = player.currentTour();
        const step = player.currentStepIndex();
        void copyShareableLink({
            getQualityPreset: () => runtime.getQualityPreset(),
            getTimeScale: () => runtime.getTimeScale(),
            getPaused: () => runtime.getPaused(),
            getSimulationEpoch: () => runtime.getSimulationEpoch(),
            getOrbitScaleMode: () => runtime.getOrbitScaleMode(),
            getShadowQuality: () => runtime.getShadowQuality(),
            getOrbitLines: () => runtime.getOrbitLines(),
            getMagneticFields: () => runtime.getMagneticFields(),
            getFocusedPlanetIndex: () => runtime.getFocusedPlanetIndex(),
            getFocusedMission: () => runtime.getFocusedMission(),
            getCameraPose: () => runtime.getCameraPose(),
            isoDateFromJulianDate,
        }).then((link) => {
            const url = new URL(link);
            if (tour) {
                url.searchParams.set('tour', tour.id);
                if (step >= 0) {
                    url.searchParams.set('step', String(step));
                }
            }
            return navigator.clipboard?.writeText(url.toString()) ?? Promise.resolve();
        }).then(() => {
            settingsStatus.textContent = 'Tour step link copied';
        }).catch((error: unknown) => {
            console.warn('[Tour] copy failed:', error);
            settingsStatus.textContent = 'Could not copy tour step';
        });
    });

    window.startTour = (id = 'inner-system') => {
        void loadTour(id).then((tour) => {
            if (!tour) {
                settingsStatus.textContent = 'Could not load tour';
                return;
            }
            player.play(tour, deepLink.tourStep ?? 0);
            setTourPlaying(true);
        });
    };

    if (deepLink.tour) {
        window.startTour(deepLink.tour);
    }

    return player;
}
