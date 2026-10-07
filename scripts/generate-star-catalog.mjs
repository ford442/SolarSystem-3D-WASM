#!/usr/bin/env node
/**
 * Build resource/sky/bright_stars.json — the fixed-star field Observe mode draws — from the
 * Yale Bright Star Catalogue, 5th revised edition (Hoffleit & Warren 1991, CDS catalogue V/50).
 * The BSC is US-government/academic public domain, so the derived JSON can be committed.
 * The raw catalogue is not committed; fetch it and pass it in:
 *
 *   curl -L -o catalog.gz https://cdsarc.cds.unistra.fr/ftp/V/50/catalog.gz && gunzip catalog.gz
 *   node scripts/generate-star-catalog.mjs --input catalog
 *
 * Usage:
 *   node scripts/generate-star-catalog.mjs --input <catalog> [--count 5000]
 *       Regenerate the committed JSON from the raw catalogue.
 *   node scripts/generate-star-catalog.mjs --check [--input <catalog>]
 *       With --input: exit 1 if regenerating would differ from the committed file.
 *       Without it: validate the committed file's shape (what CI can do without the source).
 *
 * Output is columnar and sorted by visual magnitude (brightest first) so a quality tier can
 * draw a prefix of it: {"fields":["raDeg","decDeg","vmag","bv"],"data":[ra,dec,v,bv,...]}.
 * Positions are J2000 equatorial degrees; proper motion is dropped (Arcturus, the worst case
 * among naked-eye stars, moves ~2.3 arcsec/yr, under a tenth of a degree per century).
 */
import { readFileSync, writeFileSync } from 'node:fs';
import { dirname, join } from 'node:path';
import { fileURLToPath } from 'node:url';

const root = join(dirname(fileURLToPath(import.meta.url)), '..');
const outPath = join(root, 'resource/sky/bright_stars.json');

const DEFAULT_COUNT = 5000;
const DEFAULT_BV = 0.6; // roughly solar; BSC5 omits B-V for a few hundred faint entries

/** Common names for the brightest stars, keyed by Harvard Revised (HR) number. */
const NAMES = new Map([
    [2491, 'Sirius'], [2326, 'Canopus'], [5340, 'Arcturus'], [5459, 'Rigil Kentaurus'],
    [7001, 'Vega'], [1708, 'Capella'], [1713, 'Rigel'], [2943, 'Procyon'],
    [2061, 'Betelgeuse'], [472, 'Achernar'], [5267, 'Hadar'], [7557, 'Altair'],
    [4730, 'Acrux'], [1457, 'Aldebaran'], [6134, 'Antares'], [5056, 'Spica'],
    [2990, 'Pollux'], [8728, 'Fomalhaut'], [7924, 'Deneb'], [4853, 'Mimosa'],
    [3982, 'Regulus'], [2618, 'Adhara'], [2891, 'Castor'], [6527, 'Shaula'],
    [1790, 'Bellatrix'], [1791, 'Elnath'], [3685, 'Miaplacidus'], [1903, 'Alnilam'],
    [8425, 'Alnair'], [1948, 'Alnitak'], [4905, 'Alioth'], [4301, 'Dubhe'],
    [1017, 'Mirfak'], [2693, 'Wezen'], [6553, 'Sargas'], [6879, 'Kaus Australis'],
    [3307, 'Avior'], [5191, 'Alkaid'], [2088, 'Menkalinan'], [6217, 'Atria'],
    [2421, 'Alhena'], [7790, 'Peacock'], [424, 'Polaris'], [2294, 'Mirzam'],
    [3748, 'Alphard'], [617, 'Hamal'], [4057, 'Algieba'], [188, 'Diphda'],
    [5054, 'Mizar'], [7121, 'Nunki'], [2004, 'Saiph'], [1852, 'Mintaka'],
    [6556, 'Rasalhague'], [4534, 'Denebola'], [936, 'Algol'], [4295, 'Merak'],
    [15, 'Alpheratz'], [168, 'Schedar'], [337, 'Mirach'], [5291, 'Thuban'],
    [7417, 'Albireo'],
]);

function parseArgs(argv) {
    const args = { check: false, input: null, count: DEFAULT_COUNT };
    for (let i = 0; i < argv.length; i++) {
        if (argv[i] === '--check') args.check = true;
        else if (argv[i] === '--input') args.input = argv[++i];
        else if (argv[i] === '--count') args.count = Number(argv[++i]);
        else throw new Error(`Unknown argument: ${argv[i]}`);
    }
    if (!Number.isInteger(args.count) || args.count < 1) throw new Error('--count must be a positive integer');
    return args;
}

/** Fixed-width BSC5 record → star, or null for entries without a usable position/magnitude. */
function parseRecord(line) {
    if (line.length < 114) return null;
    const hr = Number(line.slice(0, 4));
    const raH = line.slice(75, 77).trim();
    const raM = line.slice(77, 79).trim();
    const raS = line.slice(79, 83).trim();
    const decSign = line.slice(83, 84);
    const decD = line.slice(84, 86).trim();
    const decM = line.slice(86, 88).trim();
    const decS = line.slice(88, 90).trim();
    const vText = line.slice(102, 107).trim();
    const bvText = line.slice(109, 114).trim();
    // Novae and non-stellar objects that were dropped from the catalogue have blank coordinates.
    if (!raH || !decD || !vText || !Number.isInteger(hr)) return null;

    const raDeg = (Number(raH) + Number(raM) / 60 + Number(raS) / 3600) * 15;
    const decAbs = Number(decD) + Number(decM) / 60 + Number(decS) / 3600;
    const decDeg = decSign === '-' ? -decAbs : decAbs;
    const vmag = Number(vText);
    const bv = bvText === '' ? DEFAULT_BV : Number(bvText);
    if (![raDeg, decDeg, vmag, bv].every(Number.isFinite)) return null;
    return { hr, raDeg, decDeg, vmag, bv };
}

function buildCatalog(rawText, count) {
    const stars = rawText
        .split('\n')
        .map(parseRecord)
        .filter((star) => star !== null)
        // Brightest first; HR number breaks ties so the output is deterministic.
        .sort((a, b) => a.vmag - b.vmag || a.hr - b.hr)
        .slice(0, count);

    const names = {};
    stars.forEach((star, index) => {
        const name = NAMES.get(star.hr);
        if (name) names[index] = name;
    });

    const rows = stars.map((star) => [
        star.raDeg.toFixed(3),
        star.decDeg.toFixed(3),
        star.vmag.toFixed(2),
        star.bv.toFixed(2),
    ].join(','));

    return [
        '{',
        '"v":1,',
        '"source":"Yale Bright Star Catalogue, 5th rev. ed. (Hoffleit & Warren 1991), CDS V/50; public domain",',
        '"epoch":"J2000",',
        `"count":${stars.length},`,
        '"fields":["raDeg","decDeg","vmag","bv"],',
        `"names":${JSON.stringify(names)},`,
        '"data":[',
        rows.join(',\n'),
        ']',
        '}',
        '',
    ].join('\n');
}

/** Shape checks that need no source catalogue; returns a list of problems. */
function validate(text) {
    const problems = [];
    let json;
    try {
        json = JSON.parse(text);
    } catch (error) {
        return [`not valid JSON: ${error.message}`];
    }
    if (json.v !== 1) problems.push('v must be 1');
    if (json.epoch !== 'J2000') problems.push('epoch must be J2000');
    if (JSON.stringify(json.fields) !== '["raDeg","decDeg","vmag","bv"]') problems.push('unexpected fields');
    if (!Array.isArray(json.data) || json.data.length % 4 !== 0) {
        problems.push('data must be a flat array of 4-tuples');
        return problems;
    }
    const count = json.data.length / 4;
    if (json.count !== count) problems.push(`count ${json.count} does not match ${count} rows`);
    let previous = -Infinity;
    for (let i = 0; i < count; i++) {
        const [ra, dec, vmag, bv] = json.data.slice(i * 4, i * 4 + 4);
        if (!(ra >= 0 && ra < 360)) problems.push(`row ${i}: RA ${ra} out of range`);
        if (!(dec >= -90 && dec <= 90)) problems.push(`row ${i}: Dec ${dec} out of range`);
        if (!Number.isFinite(vmag) || !Number.isFinite(bv)) problems.push(`row ${i}: non-finite magnitude/colour`);
        if (vmag < previous) problems.push(`row ${i}: not sorted by magnitude`);
        previous = vmag;
        if (problems.length > 10) break;
    }
    for (const index of Object.keys(json.names ?? {})) {
        if (!(Number(index) >= 0 && Number(index) < count)) problems.push(`name index ${index} out of range`);
    }
    return problems;
}

const args = parseArgs(process.argv.slice(2));

if (args.input) {
    const generated = buildCatalog(readFileSync(args.input, 'latin1'), args.count);
    const problems = validate(generated);
    if (problems.length > 0) {
        console.error(`Generated catalogue is invalid:\n  ${problems.join('\n  ')}`);
        process.exit(1);
    }
    if (args.check) {
        let committed = '';
        try {
            committed = readFileSync(outPath, 'utf8');
        } catch {
            // Missing counts as a difference below.
        }
        if (committed !== generated) {
            console.error('resource/sky/bright_stars.json is out of date; rerun without --check.');
            process.exit(1);
        }
        console.log('OK: bright_stars.json matches the source catalogue.');
    } else {
        writeFileSync(outPath, generated);
        const rows = JSON.parse(generated).count;
        console.log(`Wrote ${rows} stars → ${outPath} (${Math.round(generated.length / 1024)} KiB)`);
    }
} else if (args.check) {
    const problems = validate(readFileSync(outPath, 'utf8'));
    if (problems.length > 0) {
        console.error(`bright_stars.json is invalid:\n  ${problems.join('\n  ')}`);
        process.exit(1);
    }
    console.log('OK: bright_stars.json is well-formed (pass --input to compare against the source).');
} else {
    console.error('Pass --input <catalog> to generate, or --check to validate. See the header comment.');
    process.exit(2);
}
