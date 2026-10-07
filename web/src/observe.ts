import { isoDateFromJulianDate, julianDateFromIsoDate, julianDateUtcNow } from './ephemeris';
import type { SolarSystemRuntime } from './wasmBridge';

/**
 * Observe mode: the camera stands on Earth at a lat/lon and looks up. State comes from
 * GetObserveStateJson (C++ Application::GetObserveStateJson); the sky itself is drawn by
 * ObserveSky in C++. Everything here is UI around the typed runtime façade.
 *
 * Positions are a visualization, not an almanac: Standish planet series (~arcminute), a mean-
 * element Moon, UTC fed straight into sidereal time, and no refraction or nutation.
 */
export interface ObserveState {
    active: boolean;
    latDeg: number;
    lonDeg: number;
    altM: number;
    julianDate: number;
    utc: string;
    azDeg: number;
    elDeg: number;
    fovDeg: number;
    /** Simulated seconds per wall second while running; 1 = real time. */
    timeRate: number;
    paused: boolean;
    lstDeg: number;
    sunAltDeg: number;
    sunAzDeg: number;
    moonAltDeg: number;
    moonAzDeg: number;
    moonIllum: number;
    /** Fraction (0–1) of the Sun's disc the Moon covers from this site; 1 is totality. */
    sunCoverage: number;
    starCount: number;
    /** Selenographic longitude/latitude of the sub-Earth point (the Moon's libration), degrees. */
    moonLibLonDeg: number;
    moonLibLatDeg: number;
    /**
     * Whether the bodies the next sky event needs (the Sun for a solar eclipse, the Moon for a lunar
     * one, both planets for a conjunction…) are above this site's horizon at the event's instant.
     */
    eventVisibility: 'above' | 'below' | 'unknown' | 'none';
}

export interface ObserverSite {
    id: string;
    name: string;
    latDeg: number;
    lonDeg: number;
    altM: number;
}

/** Documented viewing sites. The last entry is the 2017-08-21 total solar eclipse path. */
export const OBSERVER_SITES: readonly ObserverSite[] = [
    { id: 'greenwich', name: 'Greenwich, UK', latDeg: 51.4769, lonDeg: 0, altM: 0 },
    { id: 'new-york', name: 'New York, USA', latDeg: 40.7128, lonDeg: -74.006, altM: 10 },
    { id: 'quito', name: 'Quito, Ecuador (equator)', latDeg: -0.1807, lonDeg: -78.4678, altM: 2850 },
    { id: 'sydney', name: 'Sydney, Australia', latDeg: -33.8688, lonDeg: 151.2093, altM: 20 },
    { id: 'longyearbyen', name: 'Longyearbyen, Svalbard', latDeg: 78.2232, lonDeg: 15.6267, altM: 10 },
    { id: 'casper', name: 'Casper, Wyoming (2017 eclipse)', latDeg: 42.8666, lonDeg: -106.3131, altM: 1600 },
];

export const DEFAULT_OBSERVER_SITE = OBSERVER_SITES[0];

/** Time-rate choices: simulated seconds per wall second. 0 is "paused". */
export const OBSERVE_RATES: readonly { value: number; label: string }[] = [
    { value: 0, label: 'Paused' },
    { value: 1, label: 'Real time' },
    { value: 60, label: '×60 (1 min/s)' },
    { value: 600, label: '×600 (10 min/s)' },
    { value: 3600, label: '×3600 (1 h/s)' },
    { value: 86400, label: '×86400 (1 day/s)' },
];

/**
 * The shipped moment: the 2017-08-21 total eclipse over Casper, WY, at mid-totality
 * (17:43:45 UTC; totality there ran about 17:42:30–17:45:00, matching NASA's circumstances).
 */
export const ECLIPSE_2017 = {
    siteId: 'casper',
    julianDate: julianDateFromIsoDate('2017-08-21')! + (17 + 43 / 60 + 45 / 3600) / 24,
    fovDeg: 10,
} as const;

export const OBSERVE_DISCLAIMER =
    'Visualization, not an almanac: planets ~arcminute, Sun and Moon a few arcseconds, no refraction, times are UTC.';

const INACTIVE_STATE: ObserveState = {
    active: false,
    latDeg: DEFAULT_OBSERVER_SITE.latDeg,
    lonDeg: DEFAULT_OBSERVER_SITE.lonDeg,
    altM: 0,
    julianDate: 0,
    utc: '',
    azDeg: 180,
    elDeg: 25,
    fovDeg: 60,
    timeRate: 1,
    paused: false,
    lstDeg: 0,
    sunAltDeg: 0,
    sunAzDeg: 0,
    moonAltDeg: 0,
    moonAzDeg: 0,
    moonIllum: 0,
    sunCoverage: 0,
    starCount: 0,
    moonLibLonDeg: 0,
    moonLibLatDeg: 0,
    eventVisibility: 'none',
};

function finite(value: unknown, fallback: number): number {
    return typeof value === 'number' && Number.isFinite(value) ? value : fallback;
}

export function parseObserveStateJson(json: string): ObserveState {
    try {
        const raw = JSON.parse(json) as Record<string, unknown>;
        if (typeof raw !== 'object' || raw === null) {
            return INACTIVE_STATE;
        }
        return {
            active: raw.active === true,
            latDeg: finite(raw.latDeg, INACTIVE_STATE.latDeg),
            lonDeg: finite(raw.lonDeg, INACTIVE_STATE.lonDeg),
            altM: finite(raw.altM, 0),
            julianDate: finite(raw.julianDate, 0),
            utc: typeof raw.utc === 'string' ? raw.utc : '',
            azDeg: finite(raw.azDeg, INACTIVE_STATE.azDeg),
            elDeg: finite(raw.elDeg, INACTIVE_STATE.elDeg),
            fovDeg: finite(raw.fovDeg, INACTIVE_STATE.fovDeg),
            timeRate: finite(raw.timeRate, 1),
            paused: raw.paused === true,
            lstDeg: finite(raw.lstDeg, 0),
            sunAltDeg: finite(raw.sunAltDeg, 0),
            sunAzDeg: finite(raw.sunAzDeg, 0),
            moonAltDeg: finite(raw.moonAltDeg, 0),
            moonAzDeg: finite(raw.moonAzDeg, 0),
            moonIllum: finite(raw.moonIllum, 0),
            sunCoverage: finite(raw.sunCoverage, 0),
            starCount: finite(raw.starCount, 0),
            moonLibLonDeg: finite(raw.moonLibLonDeg, 0),
            moonLibLatDeg: finite(raw.moonLibLatDeg, 0),
            eventVisibility: raw.eventVisibility === 'above' || raw.eventVisibility === 'below'
                || raw.eventVisibility === 'unknown' ? raw.eventVisibility : 'none',
        };
    } catch {
        return INACTIVE_STATE;
    }
}

/**
 * Chip suffix for the next sky event while Observe is active: whether the Sun/Moon/planets it
 * involves are above this site's horizon when it happens. The events themselves are geocentric
 * ("somewhere on Earth"), so this is the honest minimum, not a visibility prediction.
 */
export function eventVisibilitySuffix(state: ObserveState | null): string {
    if (!state?.active) {
        return '';
    }
    if (state.eventVisibility === 'above') return ' · above your horizon then';
    if (state.eventVisibility === 'below') return ' · below your horizon then';
    return '';
}

function eclipseLabel(coverage: number): string {
    return coverage >= 0.9999 ? 'TOTAL ECLIPSE' : `eclipse: ${Math.floor(coverage * 100)}% of the Sun covered`;
}

export function clampLatitude(latDeg: number): number {
    return Math.min(90, Math.max(-90, latDeg));
}

/** Fold any longitude into [-180, 180). */
export function normalizeLongitude(lonDeg: number): number {
    const wrapped = ((((lonDeg + 180) % 360) + 360) % 360) - 180;
    return wrapped;
}

/** Fraction of a day, from "HH:MM" or "HH:MM:SS"; undefined when it does not parse. */
export function parseTimeOfDayDays(value: string): number | undefined {
    const match = /^(\d{2}):(\d{2})(?::(\d{2}))?$/.exec(value);
    if (!match) {
        return undefined;
    }
    const hours = Number(match[1]);
    const minutes = Number(match[2]);
    const seconds = Number(match[3] ?? 0);
    if (hours > 23 || minutes > 59 || seconds > 59) {
        return undefined;
    }
    return (hours * 3600 + minutes * 60 + seconds) / 86400;
}

/** "HH:MM:SS" (UTC) for a Julian Date. */
export function timeOfDayFromJulianDate(julianDate: number): string {
    const shifted = julianDate + 0.5;
    const secondOfDay = Math.min(86399, Math.floor((shifted - Math.floor(shifted)) * 86400 + 0.5));
    const hh = String(Math.floor(secondOfDay / 3600)).padStart(2, '0');
    const mm = String(Math.floor((secondOfDay % 3600) / 60)).padStart(2, '0');
    const ss = String(secondOfDay % 60).padStart(2, '0');
    return `${hh}:${mm}:${ss}`;
}

export interface ObservePanelElements {
    exploreButton: HTMLButtonElement;
    observeButton: HTMLButtonElement;
    controls: HTMLElement;
    siteSelect: HTMLSelectElement;
    latInput: HTMLInputElement;
    lonInput: HTMLInputElement;
    locateButton: HTMLButtonElement;
    timeInput: HTMLInputElement;
    rateSelect: HTMLSelectElement;
    stepButtons: HTMLButtonElement[];
    nowButton: HTMLButtonElement;
    lookSunButton: HTMLButtonElement;
    lookMoonButton: HTMLButtonElement;
    eclipseButton: HTMLButtonElement;
    readout: HTMLElement;
    dateInput: HTMLInputElement;
    status: HTMLElement;
}

export interface ObserveDeepLink {
    lat?: number;
    lon?: number;
    az?: number;
    el?: number;
    fov?: number;
    rate?: number;
    paused?: boolean;
}

export interface ObservePanelOptions {
    elements: ObservePanelElements;
    runtime: SolarSystemRuntime;
    /** True when the page URL carried `?mode=observe`. */
    startInObserve: boolean;
    deepLink?: ObserveDeepLink;
    /** True when the URL pinned an epoch (`jd=` / `date=`); entering must not reset it to now. */
    epochFromLink: boolean;
    /** Called after the mode changes so the host can tidy other panels. */
    onModeChanged?: (active: boolean) => void;
}

export interface ObservePanelController {
    /** Fraction of a day for the UTC time field; 0 when it is empty. */
    timeOfDayDays(): number;
    isActive(): boolean;
}

const STORAGE_KEY = 'solar-system.observe.v1';

interface PersistedObserve {
    siteId?: string;
    latDeg?: number;
    lonDeg?: number;
    rate?: number;
}

function readPersisted(): PersistedObserve {
    try {
        const raw = window.localStorage.getItem(STORAGE_KEY);
        if (!raw) return {};
        const parsed = JSON.parse(raw) as Record<string, unknown>;
        return {
            siteId: typeof parsed.siteId === 'string' ? parsed.siteId : undefined,
            latDeg: typeof parsed.latDeg === 'number' && Number.isFinite(parsed.latDeg) ? parsed.latDeg : undefined,
            lonDeg: typeof parsed.lonDeg === 'number' && Number.isFinite(parsed.lonDeg) ? parsed.lonDeg : undefined,
            rate: typeof parsed.rate === 'number' && Number.isFinite(parsed.rate) ? parsed.rate : undefined,
        };
    } catch {
        return {};
    }
}

function writePersisted(value: PersistedObserve): void {
    try {
        window.localStorage.setItem(STORAGE_KEY, JSON.stringify(value));
    } catch {
        // Storage can be blocked or full; the panel works without it.
    }
}

const CUSTOM_SITE_ID = 'custom';

export function initObservePanel(options: ObservePanelOptions): ObservePanelController {
    const { elements, runtime } = options;
    const {
        exploreButton,
        observeButton,
        controls,
        siteSelect,
        latInput,
        lonInput,
        locateButton,
        timeInput,
        rateSelect,
        stepButtons,
        nowButton,
        lookSunButton,
        lookMoonButton,
        eclipseButton,
        readout,
        dateInput,
        status,
    } = elements;

    for (const site of OBSERVER_SITES) {
        siteSelect.add(new Option(site.name, site.id));
    }
    siteSelect.add(new Option('Custom lat/lon', CUSTOM_SITE_ID));
    for (const rate of OBSERVE_RATES) {
        rateSelect.add(new Option(rate.label, String(rate.value)));
    }

    const saved = readPersisted();
    const link = options.deepLink ?? {};
    let activeSiteId = OBSERVER_SITES.some((site) => site.id === saved.siteId) ? saved.siteId! : DEFAULT_OBSERVER_SITE.id;
    let latDeg = link.lat ?? saved.latDeg ?? DEFAULT_OBSERVER_SITE.latDeg;
    let lonDeg = link.lon ?? saved.lonDeg ?? DEFAULT_OBSERVER_SITE.lonDeg;
    if (link.lat !== undefined || link.lon !== undefined) {
        activeSiteId = matchingSiteId(latDeg, lonDeg);
    } else if (saved.siteId === CUSTOM_SITE_ID) {
        activeSiteId = CUSTOM_SITE_ID;
    } else {
        const site = OBSERVER_SITES.find((s) => s.id === activeSiteId)!;
        latDeg = site.latDeg;
        lonDeg = site.lonDeg;
    }
    latDeg = clampLatitude(latDeg);
    lonDeg = normalizeLongitude(lonDeg);
    let rate = link.rate ?? saved.rate ?? 1;

    function matchingSiteId(lat: number, lon: number): string {
        const match = OBSERVER_SITES.find(
            (site) => Math.abs(site.latDeg - lat) < 0.005 && Math.abs(site.lonDeg - lon) < 0.005,
        );
        return match ? match.id : CUSTOM_SITE_ID;
    }

    function siteAltitude(): number {
        return OBSERVER_SITES.find((site) => site.id === activeSiteId)?.altM ?? 0;
    }

    function persist(): void {
        writePersisted({ siteId: activeSiteId, latDeg, lonDeg, rate });
    }

    function showSiteFields(): void {
        siteSelect.value = activeSiteId;
        latInput.value = latDeg.toFixed(4);
        lonInput.value = lonDeg.toFixed(4);
    }

    function applySite(): void {
        runtime.setObserverSite(latDeg, lonDeg, siteAltitude());
        persist();
    }

    function showRate(): void {
        const paused = runtime.getPaused();
        const choice = paused ? 0 : rate;
        // A rate that is not one of the presets (from a link) still gets an entry to show.
        if (!OBSERVE_RATES.some((entry) => entry.value === choice)) {
            rateSelect.add(new Option(`×${choice}`, String(choice)));
        }
        rateSelect.value = String(choice);
    }

    function applyRate(newRate: number): void {
        if (newRate <= 0) {
            runtime.setPaused(true);
        } else {
            rate = newRate;
            runtime.setObserveTimeRate(newRate);
            runtime.setPaused(false);
        }
        persist();
        showRate();
    }

    function composeJulianDate(): number | undefined {
        const day = julianDateFromIsoDate(dateInput.value);
        if (day === undefined) {
            return undefined;
        }
        return day + (parseTimeOfDayDays(timeInput.value) ?? 0);
    }

    function pushEpochFromFields(): void {
        const jd = composeJulianDate();
        if (jd === undefined) {
            status.textContent = 'Invalid date or time';
            return;
        }
        runtime.setSimulationEpoch(jd);
        status.textContent = `Set to ${isoDateFromJulianDate(jd)} ${timeInput.value} UTC`;
    }

    function stepEpoch(deltaDays: number): void {
        runtime.setSimulationEpoch(runtime.getSimulationEpoch() + deltaDays);
    }

    function lookAt(azDeg: number, elDeg: number): void {
        runtime.setObserveView(azDeg, Math.max(-10, elDeg));
    }

    let lastActive = false;

    function renderMode(active: boolean): void {
        exploreButton.setAttribute('aria-pressed', String(!active));
        observeButton.setAttribute('aria-pressed', String(active));
        controls.hidden = !active;
        document.body.classList.toggle('observe-mode', active);
        if (active !== lastActive) {
            lastActive = active;
            options.onModeChanged?.(active);
        }
    }

    function refresh(): void {
        const state = runtime.getObserveState();
        renderMode(state.active);
        if (!state.active) {
            return;
        }
        if (document.activeElement !== timeInput) {
            timeInput.value = timeOfDayFromJulianDate(state.julianDate);
        }
        if (document.activeElement !== dateInput) {
            dateInput.value = isoDateFromJulianDate(state.julianDate);
        }
        const sunState = state.sunAltDeg > 0 ? 'day' : state.sunAltDeg > -6 ? 'civil twilight' : state.sunAltDeg > -18 ? 'twilight' : 'night';
        readout.textContent =
            `Az ${Math.round(state.azDeg)}° Alt ${Math.round(state.elDeg)}° FOV ${Math.round(state.fovDeg)}° · ` +
            `Sun ${state.sunAltDeg.toFixed(1)}° (${sunState}) · ` +
            `Moon ${state.moonAltDeg.toFixed(1)}°, ${Math.round(state.moonIllum * 100)}% lit · ` +
            `${state.starCount} stars` +
            (state.sunCoverage > 0.001 ? ` · ${eclipseLabel(state.sunCoverage)}` : '');
    }

    // A shared link pins the epoch; the first entry from it holds still instead of jumping to now.
    let pinnedEpochPending = options.startInObserve && options.epochFromLink;

    function enterObserve(): void {
        applySite();
        if (pinnedEpochPending) {
            pinnedEpochPending = false;
            runtime.setObserveTimeRate(rate);
        } else {
            // Fresh entry from the toolbar: start at the current UTC, running live.
            runtime.setSimulationEpoch(julianDateUtcNow());
            rate = 1;
            runtime.setObserveTimeRate(1);
            runtime.setPaused(false);
        }
        runtime.setObserveMode(true);
        showRate();
        refresh();
        status.textContent = 'Observe: look around with the mouse or touch; scroll to zoom';
    }

    observeButton.addEventListener('click', () => {
        if (!runtime.getObserveMode()) {
            enterObserve();
        }
    });

    exploreButton.addEventListener('click', () => {
        if (runtime.getObserveMode()) {
            runtime.setObserveMode(false);
            refresh();
            status.textContent = 'Explore mode';
        }
    });

    siteSelect.addEventListener('change', () => {
        if (siteSelect.value === CUSTOM_SITE_ID) {
            activeSiteId = CUSTOM_SITE_ID;
        } else {
            const site = OBSERVER_SITES.find((candidate) => candidate.id === siteSelect.value);
            if (!site) return;
            activeSiteId = site.id;
            latDeg = site.latDeg;
            lonDeg = site.lonDeg;
        }
        showSiteFields();
        applySite();
        status.textContent = `Site: ${siteSelect.selectedOptions[0]?.text ?? ''}`;
    });

    function onLatLonEdited(): void {
        const lat = Number(latInput.value);
        const lon = Number(lonInput.value);
        if (!Number.isFinite(lat) || !Number.isFinite(lon) || latInput.value === '' || lonInput.value === '') {
            return;
        }
        latDeg = clampLatitude(lat);
        lonDeg = normalizeLongitude(lon);
        activeSiteId = matchingSiteId(latDeg, lonDeg);
        siteSelect.value = activeSiteId;
        applySite();
    }
    latInput.addEventListener('change', onLatLonEdited);
    lonInput.addEventListener('change', onLatLonEdited);

    locateButton.addEventListener('click', () => {
        if (!navigator.geolocation) {
            status.textContent = 'Geolocation is not available in this browser';
            return;
        }
        status.textContent = 'Asking the browser for your location…';
        navigator.geolocation.getCurrentPosition(
            (position) => {
                latDeg = clampLatitude(position.coords.latitude);
                lonDeg = normalizeLongitude(position.coords.longitude);
                activeSiteId = CUSTOM_SITE_ID;
                showSiteFields();
                runtime.setObserverSite(latDeg, lonDeg, Math.max(0, position.coords.altitude ?? 0));
                persist();
                status.textContent = `Site: ${latDeg.toFixed(2)}°, ${lonDeg.toFixed(2)}°`;
            },
            () => {
                status.textContent = 'Location unavailable or denied';
            },
            { maximumAge: 600000, timeout: 10000 },
        );
    });

    timeInput.addEventListener('change', pushEpochFromFields);
    dateInput.addEventListener('change', () => {
        if (runtime.getObserveMode()) {
            pushEpochFromFields();
        }
    });

    for (const button of stepButtons) {
        button.addEventListener('click', () => {
            const days = Number(button.dataset.stepDays);
            if (Number.isFinite(days)) {
                stepEpoch(days);
            }
        });
    }

    nowButton.addEventListener('click', () => {
        runtime.setSimulationEpoch(julianDateUtcNow());
        applyRate(1);
        status.textContent = 'Set to now (UTC), running in real time';
    });

    rateSelect.addEventListener('change', () => applyRate(Number(rateSelect.value)));

    lookSunButton.addEventListener('click', () => {
        const state = runtime.getObserveState();
        lookAt(state.sunAzDeg, state.sunAltDeg);
    });
    lookMoonButton.addEventListener('click', () => {
        const state = runtime.getObserveState();
        lookAt(state.moonAzDeg, state.moonAltDeg);
    });

    eclipseButton.addEventListener('click', () => {
        const site = OBSERVER_SITES.find((candidate) => candidate.id === ECLIPSE_2017.siteId)!;
        activeSiteId = site.id;
        latDeg = site.latDeg;
        lonDeg = site.lonDeg;
        showSiteFields();
        applySite();
        runtime.setSimulationEpoch(ECLIPSE_2017.julianDate);
        runtime.setPaused(true);
        runtime.setObserveMode(true);
        runtime.setObserveFov(ECLIPSE_2017.fovDeg);
        showRate();
        const state = runtime.getObserveState();
        lookAt(state.sunAzDeg, state.sunAltDeg);
        refresh();
        status.textContent = '2017-08-21 17:43:45 UTC, Casper WY — mid-totality (visualization; Moon and Sun positions to a few arcseconds)';
    });

    // Keep the buttons honest when C++ flips the mode itself (the native O key), and keep the
    // live readout moving while time runs.
    window.setInterval(refresh, 250);

    showSiteFields();
    showRate();
    renderMode(false);

    if (options.startInObserve) {
        applySite();
        if (link.fov !== undefined) {
            runtime.setObserveFov(link.fov);
        }
        // The settings panel already applied the link's epoch and pause flag; a pinned epoch
        // with no explicit `paused` holds still rather than drifting away from the shared moment.
        if (options.epochFromLink && link.paused === undefined) {
            runtime.setPaused(true);
        }
        enterObserve();
        if (link.az !== undefined || link.el !== undefined) {
            const state = runtime.getObserveState();
            runtime.setObserveView(link.az ?? state.azDeg, link.el ?? state.elDeg);
        }
    }

    return {
        timeOfDayDays: () => parseTimeOfDayDays(timeInput.value) ?? 0,
        isActive: () => runtime.getObserveMode(),
    };
}
