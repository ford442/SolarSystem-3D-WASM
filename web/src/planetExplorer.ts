import type { OrbitScaleMode, PlanetIndex } from './SolarSystem.js';

export interface PlanetFactBody {
    index: PlanetIndex;
    id: string;
    name: string;
    type: string;
    diameterKm: number;
    massEarths?: number;
    orbitalPeriodDays: number | null;
    distanceAu: number;
    moons?: number;
    summary: string;
}

export interface PlanetFactsFile {
    version: number;
    sceneNote?: string;
    bodies: PlanetFactBody[];
    scaleModes: Record<string, { label: string; description: string }>;
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
    cardTitle: HTMLElement;
    cardSummary: HTMLElement;
    cardFacts: HTMLElement;
    scaleSelect: HTMLSelectElement;
    scaleHelp: HTMLElement;
    status: HTMLElement;
}

const EXPLORER_STORAGE_KEY = 'solar-system.explorer.v1';

function formatDiameter(km: number): string {
    if (km >= 10000) {
        return `${km.toLocaleString()} km`;
    }
    return `${km.toLocaleString(undefined, { maximumFractionDigits: 0 })} km`;
}

function formatOrbitalPeriod(days: number | null): string {
    if (days === null) return '—';
    if (days < 400) return `${days.toLocaleString()} days`;
    const years = days / 365.25;
    if (years < 10) return `${years.toFixed(1)} years`;
    return `${Math.round(years).toLocaleString()} years`;
}

function formatSceneDistance(units: number): string {
    return `${Math.round(units).toLocaleString()} scene units`;
}

export class PlanetExplorer {
    private readonly elements: PlanetExplorerElements;
    private facts: PlanetFactsFile | null = null;
    private bindings: PlanetExplorerBindings = {};
    private selectedIndex: PlanetIndex | null = null;

    constructor(root: HTMLElement) {
        this.elements = {
            panel: root,
            toggle: root.querySelector('#explorer-toggle') as HTMLButtonElement,
            toggleIcon: root.querySelector('.explorer-toggle-icon') as HTMLElement,
            bodyList: root.querySelector('#explorer-body-list') as HTMLElement,
            card: root.querySelector('#planet-info-card') as HTMLElement,
            cardTitle: root.querySelector('#planet-info-title') as HTMLElement,
            cardSummary: root.querySelector('#planet-info-summary') as HTMLElement,
            cardFacts: root.querySelector('#planet-info-facts') as HTMLElement,
            scaleSelect: root.querySelector('#orbit-scale-mode') as HTMLSelectElement,
            scaleHelp: root.querySelector('#orbit-scale-help') as HTMLElement,
            status: root.querySelector('#explorer-status') as HTMLElement,
        };

        this.elements.toggle.addEventListener('click', () => {
            this.setCollapsed(this.elements.panel.classList.contains('is-collapsed') === false);
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

    async init(bindings: PlanetExplorerBindings): Promise<void> {
        this.bindings = bindings;
        const factsUrl = new URL('planet_facts.json', import.meta.env.BASE_URL).toString();
        const response = await fetch(factsUrl);
        if (!response.ok) {
            throw new Error(`Failed to load planet facts (${response.status})`);
        }
        this.facts = await response.json() as PlanetFactsFile;
        this.renderBodyList();
        this.restoreState();
        this.syncScaleFromRuntime();
        this.elements.status.textContent = 'Explorer ready';
        this.startPolling();
        window.onPlanetFocused = (index: number) => {
            this.selectPlanet(index as PlanetIndex, { focusCamera: false });
        };
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
        const description = this.facts?.scaleModes[key]?.description ?? '';
        this.elements.scaleHelp.textContent = description;
    }

    private renderBodyList(): void {
        if (!this.facts) return;
        this.elements.bodyList.replaceChildren();
        for (const body of this.facts.bodies) {
            const button = document.createElement('button');
            button.type = 'button';
            button.className = 'explorer-body-btn';
            button.dataset.planetIndex = String(body.index);
            button.textContent = body.name;
            button.addEventListener('click', () => {
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
        this.elements.status.textContent = `Focused ${this.facts?.bodies.find((b) => b.index === index)?.name ?? 'body'}`;
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
        if (!body) {
            this.elements.card.hidden = true;
            return;
        }

        const sceneDistance = this.bindings.getPlanetSceneDistance?.(index) ?? 0;
        const scaleMode = this.bindings.getOrbitScaleMode?.() ?? 0;
        const scaleLabel = scaleMode === 1 ? 'Realistic spacing' : 'Compressed spacing';

        this.elements.card.hidden = false;
        this.elements.cardTitle.textContent = body.name;
        this.elements.cardSummary.textContent = body.summary;

        const factRows: Array<[string, string]> = [
            ['Type', body.type.replace(/_/g, ' ')],
            ['Diameter', formatDiameter(body.diameterKm)],
            ['Orbital period', formatOrbitalPeriod(body.orbitalPeriodDays)],
            ['Distance from Sun', body.distanceAu > 0 ? `${body.distanceAu} AU` : 'Center of system'],
            ['Scene distance', formatSceneDistance(sceneDistance)],
            ['Scale mode', scaleLabel],
        ];

        if (body.moons !== undefined) {
            factRows.push(['Known moons', String(body.moons)]);
        }
        if (body.massEarths !== undefined && body.index > 0) {
            factRows.push(['Mass (Earths)', body.massEarths.toLocaleString()]);
        }

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

    private restoreState(): void {
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
            if (parsed.scaleMode === 0 || parsed.scaleMode === 1) {
                this.elements.scaleSelect.value = String(parsed.scaleMode);
                this.bindings.setOrbitScaleMode?.(parsed.scaleMode);
                this.updateScaleHelp();
            }
            if (parsed.selectedIndex !== undefined) {
                this.selectPlanet(parsed.selectedIndex, { focusCamera: false });
            }
        } catch {
            // ignore corrupt storage
        }
    }
}