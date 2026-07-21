import type { OrbitScaleMode, PlanetIndex } from './SolarSystem.js';

export interface PlanetFactBody {
    index: PlanetIndex;
    id: string;
    name: string;
    nameRu?: string;
    type: string;
    diameterKm: number;
    massEarths?: number;
    siderealRotationDays?: number;
    orbitalPeriodDays: number | null;
    distanceAu: number;
    moons?: number;
    summary: string;
    summaryRu?: string;
}

export interface PlanetFactsFile {
    version: number;
    sceneNote?: string;
    bodies: PlanetFactBody[];
    scaleModes: Record<string, {
        label: string;
        labelRu?: string;
        description: string;
        descriptionRu?: string;
    }>;
}

export interface PlanetExplorerBindings {
    focusPlanet?: (index: PlanetIndex) => void;
    getNearestPlanetIndex?: () => number;
    getFocusedPlanetIndex?: () => number;
    getPlanetSceneDistance?: (index: PlanetIndex) => number;
    getOrbitScaleMode?: () => OrbitScaleMode;
    setOrbitScaleMode?: (mode: OrbitScaleMode) => void;
}

interface PlanetExplorerElements {
    panel: HTMLElement;
    toggle: HTMLButtonElement;
    toggleIcon: HTMLElement;
    bodyList: HTMLElement;
    card: HTMLElement;
    cardHeader: HTMLElement;
    cardTitle: HTMLElement;
    cardDismiss: HTMLButtonElement;
    cardSummary: HTMLElement;
    cardFacts: HTMLElement;
    scaleSelect: HTMLSelectElement;
    scaleHelp: HTMLElement;
    status: HTMLElement;
}

const EXPLORER_STORAGE_KEY = 'solar-system.explorer.v1';

type ExplorerLocale = 'en' | 'ru';

const LABELS: Record<ExplorerLocale, Record<string, string>> = {
    en: {
        type: 'Type',
        diameter: 'Diameter',
        mass: 'Mass (Earths)',
        dayLength: 'Day length',
        orbitalPeriod: 'Orbital period',
        distanceFromSun: 'Distance from Sun',
        sceneDistance: 'Scene distance',
        scaleMode: 'Scale mode',
        knownMoons: 'Known moons',
        centerOfSystem: 'Center of system',
        retrograde: 'retrograde',
        compressedSpacing: 'Compressed spacing',
        realisticSpacing: 'Realistic spacing',
        dismissCard: 'Dismiss info card',
        focused: 'Focused',
    },
    ru: {
        type: 'Тип',
        diameter: 'Диаметр',
        mass: 'Масса (Земли)',
        dayLength: 'Длина суток',
        orbitalPeriod: 'Орбитальный период',
        distanceFromSun: 'Расстояние от Солнца',
        sceneDistance: 'Расстояние в сцене',
        scaleMode: 'Режим масштаба',
        knownMoons: 'Известные спутники',
        centerOfSystem: 'Центр системы',
        retrograde: 'ретроградное',
        compressedSpacing: 'Сжатые орбиты',
        realisticSpacing: 'Реалистичные расстояния',
        dismissCard: 'Закрыть карточку',
        focused: 'Фокус',
    },
};

function detectLocale(): ExplorerLocale {
    const lang = (navigator.language || 'en').toLowerCase();
    return lang.startsWith('ru') ? 'ru' : 'en';
}

function formatDiameter(km: number, locale: ExplorerLocale): string {
    const formatted = km >= 10000
        ? km.toLocaleString(locale === 'ru' ? 'ru-RU' : 'en-US')
        : km.toLocaleString(locale === 'ru' ? 'ru-RU' : 'en-US', { maximumFractionDigits: 0 });
    return `${formatted} km`;
}

function formatOrbitalPeriod(days: number | null, locale: ExplorerLocale): string {
    if (days === null) return '—';
    if (days < 400) {
        return locale === 'ru'
            ? `${days.toLocaleString('ru-RU')} сут.`
            : `${days.toLocaleString()} days`;
    }
    const years = days / 365.25;
    if (years < 10) {
        return locale === 'ru' ? `${years.toFixed(1)} лет` : `${years.toFixed(1)} years`;
    }
    return locale === 'ru'
        ? `${Math.round(years).toLocaleString('ru-RU')} лет`
        : `${Math.round(years).toLocaleString()} years`;
}

function formatDayLength(days: number | undefined, locale: ExplorerLocale): string {
    if (days === undefined) return '—';
    const retrograde = days < 0;
    const absDays = Math.abs(days);
    const hours = absDays * 24;
    const suffix = retrograde ? ` (${LABELS[locale].retrograde})` : '';
    if (hours < 48) {
        return `${hours.toFixed(1)} h${suffix}`;
    }
    return locale === 'ru'
        ? `${absDays.toFixed(2)} сут.${suffix}`
        : `${absDays.toFixed(2)} days${suffix}`;
}

function formatSceneDistance(units: number, locale: ExplorerLocale): string {
    const rounded = Math.round(units).toLocaleString(locale === 'ru' ? 'ru-RU' : 'en-US');
    return locale === 'ru' ? `${rounded} ед. сцены` : `${rounded} scene units`;
}

export class PlanetExplorer {
    private readonly elements: PlanetExplorerElements;
    private facts: PlanetFactsFile | null = null;
    private bindings: PlanetExplorerBindings = {};
    private selectedIndex: PlanetIndex | null = null;
    private cardDismissed = false;
    private readonly locale: ExplorerLocale;
    private skipPlanetRestore = false;
    private initialOrbitScale: OrbitScaleMode | undefined;

    constructor(root: HTMLElement) {
        this.locale = detectLocale();
        this.elements = {
            panel: root,
            toggle: root.querySelector('#explorer-toggle') as HTMLButtonElement,
            toggleIcon: root.querySelector('.explorer-toggle-icon') as HTMLElement,
            bodyList: root.querySelector('#explorer-body-list') as HTMLElement,
            card: root.querySelector('#planet-info-card') as HTMLElement,
            cardHeader: root.querySelector('#planet-info-header') as HTMLElement,
            cardTitle: root.querySelector('#planet-info-title') as HTMLElement,
            cardDismiss: root.querySelector('#planet-info-dismiss') as HTMLButtonElement,
            cardSummary: root.querySelector('#planet-info-summary') as HTMLElement,
            cardFacts: root.querySelector('#planet-info-facts') as HTMLElement,
            scaleSelect: root.querySelector('#orbit-scale-mode') as HTMLSelectElement,
            scaleHelp: root.querySelector('#orbit-scale-help') as HTMLElement,
            status: root.querySelector('#explorer-status') as HTMLElement,
        };

        this.elements.toggle.addEventListener('click', () => {
            this.setCollapsed(this.elements.panel.classList.contains('is-collapsed') === false);
        });

        this.elements.cardDismiss.addEventListener('click', () => {
            this.dismissCard();
        });

        for (const eventName of ['pointerdown', 'pointerup', 'click', 'keydown', 'keyup']) {
            root.addEventListener(eventName, (event) => event.stopPropagation());
        }

        this.elements.scaleSelect.addEventListener('change', () => {
            const mode = Number(this.elements.scaleSelect.value) as OrbitScaleMode;
            this.bindings.setOrbitScaleMode?.(mode);
            this.updateScaleHelp();
            this.persistState();
            if (this.selectedIndex !== null) {
                this.renderCard(this.selectedIndex);
            }
        });
    }

    async init(
        bindings: PlanetExplorerBindings,
        options?: { skipPlanetRestore?: boolean; initialOrbitScale?: OrbitScaleMode },
    ): Promise<void> {
        this.bindings = bindings;
        this.skipPlanetRestore = options?.skipPlanetRestore ?? false;
        this.initialOrbitScale = options?.initialOrbitScale;
        const factsUrl = new URL('planet_facts.json', import.meta.env.BASE_URL).toString();
        const response = await fetch(factsUrl);
        if (!response.ok) {
            throw new Error(`Failed to load planet facts (${response.status})`);
        }
        this.facts = await response.json() as PlanetFactsFile;
        this.elements.cardDismiss.setAttribute('aria-label', LABELS[this.locale].dismissCard);
        this.renderBodyList();
        if (!this.skipPlanetRestore) {
            this.restoreState();
        } else {
            this.restoreState({ skipPlanet: true, skipScale: this.initialOrbitScale !== undefined });
        }
        if (this.initialOrbitScale === 0 || this.initialOrbitScale === 1) {
            this.elements.scaleSelect.value = String(this.initialOrbitScale);
            this.updateScaleHelp();
        }
        this.syncScaleFromRuntime();
        this.elements.status.textContent = 'Explorer ready';
        this.startPolling();
        window.onPlanetFocused = (index: number) => {
            this.onExternalFocus(index as PlanetIndex);
        };
    }

    applyDeepLinkPlanet(index: PlanetIndex, options: { focusCamera: boolean }): void {
        this.cardDismissed = false;
        this.selectPlanet(index, options);
        this.setCollapsed(false);
    }

    private onExternalFocus(index: PlanetIndex): void {
        this.cardDismissed = false;
        this.selectPlanet(index, { focusCamera: false });
        this.setCollapsed(false);
    }

    private dismissCard(): void {
        this.cardDismissed = true;
        this.elements.card.hidden = true;
        this.elements.card.classList.remove('is-focused');
    }

    private startPolling(): void {
        window.setInterval(() => {
            const nearest = this.bindings.getNearestPlanetIndex?.() ?? -1;
            if (nearest >= 0 && this.selectedIndex === null) {
                this.highlightNearest(nearest as PlanetIndex);
            }
            this.syncScaleFromRuntime();
        }, 750);
    }

    private syncScaleFromRuntime(): void {
        const mode = this.bindings.getOrbitScaleMode?.();
        if (mode === undefined) return;
        if (document.activeElement !== this.elements.scaleSelect) {
            this.elements.scaleSelect.value = String(mode);
        }
        this.updateScaleHelp();
    }

    private updateScaleHelp(): void {
        const mode = Number(this.elements.scaleSelect.value);
        const key = mode === 1 ? 'realistic' : 'compressed';
        const entry = this.facts?.scaleModes[key];
        const description = this.locale === 'ru'
            ? entry?.descriptionRu ?? entry?.description ?? ''
            : entry?.description ?? '';
        this.elements.scaleHelp.textContent = description;
    }

    private localizedName(body: PlanetFactBody): string {
        return this.locale === 'ru' ? (body.nameRu ?? body.name) : body.name;
    }

    private localizedSummary(body: PlanetFactBody): string {
        return this.locale === 'ru' ? (body.summaryRu ?? body.summary) : body.summary;
    }

    private renderBodyList(): void {
        if (!this.facts) return;
        this.elements.bodyList.replaceChildren();
        for (const body of this.facts.bodies) {
            const button = document.createElement('button');
            button.type = 'button';
            button.className = 'explorer-body-btn';
            button.dataset.planetIndex = String(body.index);
            button.textContent = this.localizedName(body);
            button.addEventListener('click', () => {
                this.cardDismissed = false;
                this.selectPlanet(body.index, { focusCamera: true });
            });
            this.elements.bodyList.appendChild(button);
        }
    }

    private highlightNearest(index: PlanetIndex): void {
        for (const button of this.elements.bodyList.querySelectorAll<HTMLButtonElement>('.explorer-body-btn')) {
            button.classList.toggle('is-nearest', Number(button.dataset.planetIndex) === index);
        }
    }

    selectPlanet(index: PlanetIndex, options: { focusCamera: boolean }): void {
        this.selectedIndex = index;
        if (options.focusCamera) {
            this.bindings.focusPlanet?.(index);
        }
        this.renderCard(index);
        this.highlightSelection(index);
        this.persistState();
        const body = this.facts?.bodies.find((entry) => entry.index === index);
        this.elements.status.textContent = `${LABELS[this.locale].focused} ${body ? this.localizedName(body) : 'body'}`;
    }

    private highlightSelection(index: PlanetIndex): void {
        for (const button of this.elements.bodyList.querySelectorAll<HTMLButtonElement>('.explorer-body-btn')) {
            const btnIndex = Number(button.dataset.planetIndex);
            button.classList.toggle('is-selected', btnIndex === index);
            button.classList.toggle('is-nearest', false);
        }
    }

    private renderCard(index: PlanetIndex): void {
        const body = this.facts?.bodies.find((entry) => entry.index === index);
        if (!body || this.cardDismissed) {
            this.elements.card.hidden = true;
            this.elements.card.classList.remove('is-focused');
            return;
        }

        const labels = LABELS[this.locale];
        const sceneDistance = this.bindings.getPlanetSceneDistance?.(index) ?? 0;
        const scaleMode = this.bindings.getOrbitScaleMode?.() ?? 0;
        const scaleLabel = scaleMode === 1 ? labels.realisticSpacing : labels.compressedSpacing;

        this.elements.card.hidden = false;
        this.elements.card.classList.add('is-focused');
        this.elements.cardTitle.textContent = this.localizedName(body);
        this.elements.cardSummary.textContent = this.localizedSummary(body);

        const factRows: Array<[string, string]> = [
            [labels.type, body.type.replace(/_/g, ' ')],
            [labels.diameter, formatDiameter(body.diameterKm, this.locale)],
            [labels.dayLength, formatDayLength(body.siderealRotationDays, this.locale)],
            [labels.distanceFromSun, body.distanceAu > 0 ? `${body.distanceAu} AU` : labels.centerOfSystem],
        ];

        if (body.moons !== undefined) {
            factRows.push([labels.knownMoons, String(body.moons)]);
        }
        if (body.massEarths !== undefined && body.index > 0) {
            factRows.push([labels.mass, body.massEarths.toLocaleString(this.locale === 'ru' ? 'ru-RU' : 'en-US')]);
        }
        if (body.orbitalPeriodDays !== null) {
            factRows.push([labels.orbitalPeriod, formatOrbitalPeriod(body.orbitalPeriodDays, this.locale)]);
        }
        factRows.push(
            [labels.sceneDistance, formatSceneDistance(sceneDistance, this.locale)],
            [labels.scaleMode, scaleLabel],
        );

        this.elements.cardFacts.replaceChildren();
        for (const [label, value] of factRows) {
            const row = document.createElement('div');
            row.className = 'planet-fact-row';
            const dt = document.createElement('dt');
            dt.textContent = label;
            const dd = document.createElement('dd');
            dd.textContent = value;
            row.append(dt, dd);
            this.elements.cardFacts.appendChild(row);
        }
    }

    private setCollapsed(collapsed: boolean): void {
        this.elements.panel.classList.toggle('is-collapsed', collapsed);
        this.elements.toggle.setAttribute('aria-expanded', String(!collapsed));
        this.elements.toggleIcon.textContent = collapsed ? '+' : '−';
        this.persistState();
    }

    private persistState(): void {
        try {
            window.localStorage.setItem(EXPLORER_STORAGE_KEY, JSON.stringify({
                collapsed: this.elements.panel.classList.contains('is-collapsed'),
                selectedIndex: this.selectedIndex,
                scaleMode: Number(this.elements.scaleSelect.value),
            }));
        } catch {
            // ignore storage failures
        }
    }

    private restoreState(options?: { skipPlanet?: boolean; skipScale?: boolean }): void {
        try {
            const raw = window.localStorage.getItem(EXPLORER_STORAGE_KEY);
            if (!raw) return;
            const parsed = JSON.parse(raw) as {
                collapsed?: boolean;
                selectedIndex?: PlanetIndex;
                scaleMode?: OrbitScaleMode;
            };
            if (typeof parsed.collapsed === 'boolean') {
                this.setCollapsed(parsed.collapsed);
            }
            if (!options?.skipScale && (parsed.scaleMode === 0 || parsed.scaleMode === 1)) {
                this.elements.scaleSelect.value = String(parsed.scaleMode);
                this.bindings.setOrbitScaleMode?.(parsed.scaleMode);
                this.updateScaleHelp();
            }
            if (!options?.skipPlanet && parsed.selectedIndex !== undefined) {
                this.selectPlanet(parsed.selectedIndex, { focusCamera: false });
            }
        } catch {
            // ignore corrupt storage
        }
    }
}
