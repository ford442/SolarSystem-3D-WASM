import { queryRequiredIn } from './dom.js';
import { registerWasmCallbacks } from './wasmCallbacks.js';

/**
 * The star-system picker (docs/ARCHITECTURE.md §12). It reads public/systems/index.json, which
 * scripts/generate-planet-metadata.mjs writes from resource/systems/, lists the systems by
 * distance with Sol first, and asks C++ to jump (Application::RequestSystemJump). Everything a
 * system *is* — its bodies, star, orbits — lives on the C++ side; this file only names them.
 */

export const SOL_SYSTEM_ID = 'sol';

export interface SystemIndexEntry {
    id: string;
    name: { en: string; ru?: string };
    /** Parsecs from the Sun; 0 for Sol. */
    distancePc: number;
    starClass: string;
    starTemperatureK: number;
    /** CSS colour of the star's light (StarPhysics::RelativeTint, for the swatch). */
    starColor: string;
    bodyCount: number;
    thumbnail?: string;
    /** Observe mode needs Earth: true only for Sol. */
    observe: boolean;
    /** planet_facts.json for this system, relative to the page base. */
    facts: string;
    /** Catalog body ids by focus index (0 = the star). */
    bodies: string[];
}

export interface SystemIndex {
    /** Systems in the NASA archive's composite table that the generator could build. */
    systemsAvailable?: number;
    systems: SystemIndexEntry[];
}

const SYSTEM_ID_PATTERN = /^[a-z0-9-]{1,48}$/;

/** True for ids the C++ loader accepts (StarSystemCatalog::IsValidId). */
export function isValidSystemId(id: string): boolean {
    return SYSTEM_ID_PATTERN.test(id);
}

function asRecord(value: unknown): Record<string, unknown> | null {
    return typeof value === 'object' && value !== null && !Array.isArray(value)
        ? (value as Record<string, unknown>)
        : null;
}

function parseEntry(raw: unknown): SystemIndexEntry | null {
    const record = asRecord(raw);
    if (!record) return null;
    const id = typeof record.id === 'string' ? record.id : '';
    if (!isValidSystemId(id)) return null;
    const nameRecord = asRecord(record.name);
    const nameEn = typeof nameRecord?.en === 'string' ? nameRecord.en : id;
    const nameRu = typeof nameRecord?.ru === 'string' ? nameRecord.ru : undefined;
    const distancePc =
        typeof record.distancePc === 'number' && Number.isFinite(record.distancePc) ? record.distancePc : 0;
    const bodies = Array.isArray(record.bodies)
        ? record.bodies.filter((body): body is string => typeof body === 'string')
        : [];
    return {
        id,
        name: { en: nameEn, ru: nameRu },
        distancePc: Math.max(0, distancePc),
        starClass: typeof record.starClass === 'string' ? record.starClass : '',
        starTemperatureK: typeof record.starTemperatureK === 'number' ? record.starTemperatureK : 0,
        starColor:
            typeof record.starColor === 'string' && /^#[0-9a-f]{6}$/i.test(record.starColor)
                ? record.starColor
                : '#ffffff',
        bodyCount: typeof record.bodyCount === 'number' ? record.bodyCount : Math.max(0, bodies.length - 1),
        thumbnail: typeof record.thumbnail === 'string' ? record.thumbnail : undefined,
        observe: record.observe === true,
        facts: typeof record.facts === 'string' ? record.facts : systemFactsPath(id),
        bodies,
    };
}

/** Validate index.json; malformed rows are dropped, and the result is sorted (sortSystems). */
export function parseSystemIndex(raw: unknown): SystemIndex {
    const record = asRecord(raw);
    const archive = asRecord(record?.archive);
    const systems = Array.isArray(record?.systems) ? record.systems : [];
    const seen = new Set<string>();
    const parsed: SystemIndexEntry[] = [];
    for (const entry of systems) {
        const system = parseEntry(entry);
        if (system && !seen.has(system.id)) {
            seen.add(system.id);
            parsed.push(system);
        }
    }
    const available = archive?.systemsAvailable;
    return {
        systemsAvailable: typeof available === 'number' && Number.isFinite(available) ? available : undefined,
        systems: sortSystems(parsed),
    };
}

/** Sol first, then nearest first; ties by name so the order is stable. */
export function sortSystems(systems: readonly SystemIndexEntry[]): SystemIndexEntry[] {
    return [...systems].sort((a, b) => {
        if (a.id === SOL_SYSTEM_ID) return -1;
        if (b.id === SOL_SYSTEM_ID) return 1;
        return a.distancePc - b.distancePc || a.name.en.localeCompare(b.name.en);
    });
}

const LIGHT_YEARS_PER_PARSEC = 3.261563777;

export function formatSystemDistance(distancePc: number, locale: 'en' | 'ru' = 'en'): string {
    if (distancePc <= 0) return locale === 'ru' ? 'здесь' : 'home';
    const ly = distancePc * LIGHT_YEARS_PER_PARSEC;
    const rounded = ly < 10 ? ly.toFixed(1) : Math.round(ly).toLocaleString(locale === 'ru' ? 'ru-RU' : 'en-US');
    return locale === 'ru' ? `${rounded} св. лет` : `${rounded} ly`;
}

/**
 * The `planet=` value a shared link uses for a body: the letter after the host's name
 * ("trappist_1_e" → "e"), or the full id for the star. C++ resolves it back
 * (StarSystemCatalog::FocusIndexForName).
 */
export function systemBodyLinkName(system: SystemIndexEntry, index: number): string | undefined {
    const id = system.bodies[index];
    if (!id) return undefined;
    if (index === 0) return id;
    const underscore = id.lastIndexOf('_');
    return underscore >= 0 && underscore < id.length - 1 ? id.slice(underscore + 1) : id;
}

// The index the picker was built from, for share links (deepLink.ts runtimeLinkReaders).
let activeIndex: SystemIndex | null = null;

/** systemBodyLinkName for `systemId` in the loaded index; undefined before it loads. */
export function systemBodyLinkNameInActiveIndex(systemId: string, index: number): string | undefined {
    const system = activeIndex ? findSystem(activeIndex, systemId) : undefined;
    return system ? systemBodyLinkName(system, index) : undefined;
}

export function findSystem(index: SystemIndex, id: string): SystemIndexEntry | undefined {
    return index.systems.find((system) => system.id === id);
}

/** Where a system's planet_facts.json lives (the generator's layout; index.json repeats it). */
export function systemFactsPath(id: string): string {
    return id === SOL_SYSTEM_ID ? 'planet_facts.json' : `systems/${id}/planet_facts.json`;
}

/** A system's facts URL, resolved against the deployed base. */
export function systemFactsUrl(id: string, baseUrl: string): string {
    return new URL(systemFactsPath(id), baseUrl).toString();
}

export async function loadSystemIndex(baseUrl: string): Promise<SystemIndex> {
    const response = await fetch(new URL('systems/index.json', baseUrl).toString());
    if (!response.ok) {
        throw new Error(`Failed to load the system index (${response.status})`);
    }
    return parseSystemIndex(await response.json());
}

export interface SystemPickerRuntime {
    loadStarSystem(id: string, focusIndex?: number): boolean;
    getActiveSystemId(): string;
    isSystemJumpActive(): boolean;
}

export interface SystemPickerOptions {
    root: HTMLElement;
    runtime: SystemPickerRuntime;
    index: SystemIndex;
    status?: HTMLElement;
}

export interface SystemPicker {
    readonly activeSystem: () => SystemIndexEntry | undefined;
    jumpTo(id: string): boolean;
}

/** Render the picker into `root` (index.html #system-picker) and keep it in step with C++. */
export function initSystemPicker(options: SystemPickerOptions): SystemPicker {
    const { root, runtime, index, status } = options;
    activeIndex = index;
    const locale: 'en' | 'ru' = (navigator.language || 'en').toLowerCase().startsWith('ru') ? 'ru' : 'en';
    const current = queryRequiredIn(root, '#system-current', HTMLElement);
    const home = queryRequiredIn(root, '#system-home', HTMLButtonElement);
    const list = queryRequiredIn(root, '#system-list', HTMLElement);
    const note = queryRequiredIn(root, '#system-note', HTMLElement);

    let activeId = runtime.getActiveSystemId();
    const buttons = new Map<string, HTMLButtonElement>();

    function label(system: SystemIndexEntry): string {
        return locale === 'ru' ? (system.name.ru ?? system.name.en) : system.name.en;
    }

    function setBusy(busy: boolean): void {
        root.classList.toggle('is-jumping', busy);
        for (const button of buttons.values()) {
            button.disabled = busy;
        }
        home.disabled = busy;
    }

    function render(): void {
        const active = findSystem(index, activeId);
        current.textContent = active ? label(active) : activeId;
        home.hidden = activeId === SOL_SYSTEM_ID;
        for (const [id, button] of buttons) {
            const selected = id === activeId;
            button.classList.toggle('is-selected', selected);
            button.setAttribute('aria-selected', String(selected));
        }
        const parts: string[] = [];
        if (active && active.id !== SOL_SYSTEM_ID) {
            parts.push(
                locale === 'ru'
                    ? `${active.starClass}, ${Math.round(active.starTemperatureK).toLocaleString('ru-RU')} K. Поверхности процедурные; вспышки звезды — иллюстрация.`
                    : `${active.starClass}, ${Math.round(active.starTemperatureK).toLocaleString()} K. Surfaces are procedural; the star's flaring is a visual, not data.`,
            );
        }
        if (index.systemsAvailable !== undefined) {
            parts.push(
                locale === 'ru'
                    ? `${index.systems.length} из ${index.systemsAvailable.toLocaleString('ru-RU')} систем архива NASA.`
                    : `${index.systems.length} of the ${index.systemsAvailable.toLocaleString()} systems in the NASA archive.`,
            );
        }
        note.textContent = parts.join(' ');
    }

    for (const system of index.systems) {
        const button = document.createElement('button');
        button.type = 'button';
        button.className = 'explorer-body-btn system-btn';
        button.dataset.systemId = system.id;
        button.setAttribute('role', 'option');
        const swatch = document.createElement('span');
        swatch.className = 'system-swatch';
        swatch.style.backgroundColor = system.starColor;
        swatch.setAttribute('aria-hidden', 'true');
        const name = document.createElement('span');
        name.className = 'system-name';
        name.textContent = label(system);
        const distance = document.createElement('span');
        distance.className = 'system-distance';
        distance.textContent = formatSystemDistance(system.distancePc, locale);
        button.append(swatch, name, distance);
        button.title = `${system.starClass} · ${Math.round(system.starTemperatureK)} K · ${String(system.bodyCount)} ${
            locale === 'ru' ? 'тел' : system.bodyCount === 1 ? 'body' : 'bodies'
        }`;
        button.addEventListener('click', () => {
            jumpTo(system.id);
        });
        buttons.set(system.id, button);
        list.appendChild(button);
    }

    home.addEventListener('click', () => {
        jumpTo(SOL_SYSTEM_ID);
    });

    function jumpTo(id: string): boolean {
        if (!isValidSystemId(id) || runtime.isSystemJumpActive()) return false;
        if (id === activeId) {
            // Same system: C++ flies back to its landing planet without a fade.
            return runtime.loadStarSystem(id);
        }
        const ok = runtime.loadStarSystem(id);
        if (!ok && status) {
            status.textContent = locale === 'ru' ? 'Система недоступна' : 'That system could not be loaded';
        }
        return ok;
    }

    registerWasmCallbacks({
        onSystemChanged: (id: string, phase: number) => {
            if (phase === 0) {
                setBusy(true);
                const target = findSystem(index, id);
                if (status) {
                    status.textContent = `${locale === 'ru' ? 'Перелёт' : 'Travelling'} → ${target ? label(target) : id}`;
                }
            } else if (phase === 1) {
                activeId = id;
                render();
            } else {
                setBusy(false);
                activeId = runtime.getActiveSystemId();
                render();
            }
        },
    });

    render();
    return {
        activeSystem: () => findSystem(index, activeId),
        jumpTo,
    };
}
