import { describe, expect, it } from 'vitest';
import {
    findSystem,
    formatSystemDistance,
    isValidSystemId,
    parseSystemIndex,
    SOL_SYSTEM_ID,
    sortSystems,
    systemBodyLinkName,
    systemFactsPath,
    systemFactsUrl,
    type SystemIndexEntry,
} from './systemPicker.js';

const BASE = 'https://example.test/solar-system/';

function entry(id: string, distancePc: number, bodies: string[] = []): SystemIndexEntry {
    return {
        id,
        name: { en: id.toUpperCase() },
        distancePc,
        starClass: 'G2V',
        starTemperatureK: 5772,
        starColor: '#ffffff',
        bodyCount: Math.max(0, bodies.length - 1),
        observe: id === SOL_SYSTEM_ID,
        facts: `systems/${id}/planet_facts.json`,
        bodies,
    };
}

describe('the shipped systems/index.json', () => {
    const shipped = import.meta.glob<unknown>('../public/systems/index.json', { eager: true, import: 'default' });
    const index = parseSystemIndex(shipped['../public/systems/index.json']);

    it('lists Sol first, then the rest nearest first', () => {
        expect(index.systems[0]?.id).toBe(SOL_SYSTEM_ID);
        const distances = index.systems.slice(1).map((system) => system.distancePc);
        expect(distances).toEqual([...distances].sort((a, b) => a - b));
        expect(index.systems.length).toBeGreaterThanOrEqual(9);
        expect(index.systemsAvailable).toBeGreaterThan(index.systems.length);
    });

    it('lets only Sol use Observe', () => {
        expect(index.systems.filter((system) => system.observe).map((system) => system.id)).toEqual([SOL_SYSTEM_ID]);
    });

    it('describes TRAPPIST-1 as seven planets round a red dwarf, with link names b–h', () => {
        const trappist = findSystem(index, 'trappist-1');
        expect(trappist).toBeDefined();
        if (!trappist) return;
        expect(trappist.bodyCount).toBe(7);
        expect(trappist.starTemperatureK).toBeLessThan(3000);
        expect(trappist.bodies.slice(1).map((_, i) => systemBodyLinkName(trappist, i + 1))).toEqual([
            'b',
            'c',
            'd',
            'e',
            'f',
            'g',
            'h',
        ]);
    });

    it('has a valid id and a facts file per system', () => {
        for (const system of index.systems) {
            expect(isValidSystemId(system.id), system.id).toBe(true);
            expect(system.facts, system.id).toBe(systemFactsPath(system.id));
        }
    });
});

describe('parseSystemIndex', () => {
    it('drops malformed rows and duplicate ids, and fills defaults', () => {
        const index = parseSystemIndex({
            archive: { systemsAvailable: 4000 },
            systems: [
                { id: 'tau-cet', distancePc: 3.6, bodies: ['tau_cet', 'tau_cet_e'] },
                { id: 'Bad Id' },
                null,
                'sol',
                { id: 'tau-cet', distancePc: 99 },
                { id: 'sol', name: { en: 'Solar System' }, observe: true, starColor: 'red' },
            ],
        });
        expect(index.systems.map((system) => system.id)).toEqual(['sol', 'tau-cet']);
        expect(index.systemsAvailable).toBe(4000);
        const [sol, tau] = index.systems;
        expect(sol.starColor).toBe('#ffffff');
        expect(sol.observe).toBe(true);
        expect(tau.name.en).toBe('tau-cet');
        expect(tau.bodyCount).toBe(1);
        expect(tau.observe).toBe(false);
        expect(tau.facts).toBe('systems/tau-cet/planet_facts.json');
    });

    it('is empty for anything that is not an index', () => {
        for (const raw of [null, 7, 'x', [], { systems: 'no' }]) {
            expect(parseSystemIndex(raw).systems).toEqual([]);
        }
    });
});

describe('sortSystems', () => {
    it('puts Sol first whatever its distance, and breaks ties by name', () => {
        const sorted = sortSystems([entry('b-star', 5), entry('a-star', 5), entry('near', 1.3), entry('sol', 0)]);
        expect(sorted.map((system) => system.id)).toEqual(['sol', 'near', 'a-star', 'b-star']);
    });
});

describe('systemBodyLinkName', () => {
    const proxima = entry('proxima-cen', 1.3, ['proxima_cen', 'proxima_cen_b', 'proxima_cen_d']);

    it('uses the letter for planets and the id for the star', () => {
        expect(systemBodyLinkName(proxima, 0)).toBe('proxima_cen');
        expect(systemBodyLinkName(proxima, 1)).toBe('b');
        expect(systemBodyLinkName(proxima, 2)).toBe('d');
        expect(systemBodyLinkName(proxima, 3)).toBeUndefined();
    });
});

describe('formatting and URLs', () => {
    it('gives distances in light years, and Sol as home', () => {
        expect(formatSystemDistance(0)).toBe('home');
        expect(formatSystemDistance(1.301)).toBe('4.2 ly');
        expect(formatSystemDistance(12.43)).toBe('41 ly');
        expect(formatSystemDistance(1.301, 'ru')).toBe('4.2 св. лет');
    });

    it("resolves a system's facts against the deployed base, as index.json lists them", () => {
        expect(systemFactsUrl('tau-cet', BASE)).toBe(`${BASE}systems/tau-cet/planet_facts.json`);
        expect(systemFactsUrl(SOL_SYSTEM_ID, BASE)).toBe(`${BASE}planet_facts.json`);
    });

    it('accepts the same ids as StarSystemCatalog::IsValidId', () => {
        for (const good of ['sol', 'trappist-1', '55-cnc', 'a'.repeat(48)]) {
            expect(isValidSystemId(good), good).toBe(true);
        }
        for (const bad of ['', 'TRAPPIST-1', 'tau_cet', 'a'.repeat(49), '../x']) {
            expect(isValidSystemId(bad), bad).toBe(false);
        }
    });
});
