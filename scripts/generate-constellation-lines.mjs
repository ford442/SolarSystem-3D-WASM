#!/usr/bin/env node
/**
 * Build resource/sky/constellation_lines.json — the stick figures Observe mode draws over the
 * star field — as index pairs into resource/sky/bright_stars.json.
 *
 * The IAU defines constellation boundaries, not figures, so the figures below are authored here:
 * the familiar shapes (Big Dipper, Orion's belt and shoulders, the Teapot, ...) written as chains
 * of Bayer/Flamsteed designations. They are joined to stars we already ship through the Yale
 * Bright Star Catalogue (public domain; the same CDS V/50 file generate-star-catalog.mjs reads):
 * designation → HR record → the row of bright_stars.json with that record's position. Nothing
 * else is copied from any other star chart.
 *
 *   curl -L -o catalog.gz https://cdsarc.cds.unistra.fr/ftp/V/50/catalog.gz && gunzip catalog.gz
 *   node scripts/generate-constellation-lines.mjs --input catalog
 *
 * Usage:
 *   node scripts/generate-constellation-lines.mjs --input <catalog>
 *       Regenerate the committed JSON.
 *   node scripts/generate-constellation-lines.mjs --check [--input <catalog>]
 *       With --input: exit 1 if regenerating would differ from the committed file.
 *       Without it: validate the committed file against bright_stars.json (what CI can do).
 *
 * Output: {"v":1,"stars":"bright_stars.json","starCount":N,"constellations":[{"id":"Ori",
 * "name":"Orion","core":true,"lines":[a,b,c,d,...]}]} where each (a,b) is a segment between
 * star rows a and b. `core` marks the 20-figure subset the Low preset draws.
 */
import { readFileSync, writeFileSync } from 'node:fs';
import { dirname, join } from 'node:path';
import { fileURLToPath } from 'node:url';

const root = join(dirname(fileURLToPath(import.meta.url)), '..');
const starsPath = join(root, 'resource/sky/bright_stars.json');
const outPath = join(root, 'resource/sky/constellation_lines.json');

// No segment of a stick figure spans more than this; a longer one is a typo in a designation.
const MAX_SEGMENT_DEG = 32;

/**
 * Figures as chains of designations: "Alp Ori" (Bayer, BSC5 three-letter Greek), "Alp1 Cen" (with
 * the superscript), or "58 Ori" (Flamsteed). A designation without its superscript resolves to
 * the brightest component. A chain [a, b, c] draws a-b and b-c.
 */
const FIGURES = [
    { id: 'Ori', name: 'Orion', core: true, chains: [
        ['Lam Ori', 'Alp Ori', 'Zet Ori', 'Eps Ori', 'Del Ori', 'Gam Ori', 'Lam Ori'],
        ['Zet Ori', 'Kap Ori'], ['Del Ori', 'Bet Ori']] },
    { id: 'UMa', name: 'Ursa Major', core: true, chains: [
        ['Eta UMa', 'Zet UMa', 'Eps UMa', 'Del UMa', 'Gam UMa', 'Bet UMa', 'Alp UMa', 'Del UMa'],
        ['Gam UMa', 'Chi UMa', 'Nu UMa'], ['Chi UMa', 'Psi UMa', 'Mu UMa']] },
    { id: 'UMi', name: 'Ursa Minor', core: true, chains: [
        ['Alp UMi', 'Del UMi', 'Eps UMi', 'Zet UMi', 'Bet UMi', 'Gam UMi', 'Eta UMi', 'Zet UMi']] },
    { id: 'Cas', name: 'Cassiopeia', core: true, chains: [
        ['Eps Cas', 'Del Cas', 'Gam Cas', 'Alp Cas', 'Bet Cas']] },
    { id: 'Cep', name: 'Cepheus', core: false, chains: [
        ['Alp Cep', 'Bet Cep', 'Gam Cep', 'Iot Cep', 'Zet Cep', 'Alp Cep'], ['Bet Cep', 'Iot Cep']] },
    { id: 'Cyg', name: 'Cygnus', core: true, chains: [
        ['Alp Cyg', 'Gam Cyg', 'Eta Cyg', 'Bet Cyg'],
        ['Zet Cyg', 'Eps Cyg', 'Gam Cyg', 'Del Cyg', 'Iot Cyg', 'Kap Cyg']] },
    { id: 'Lyr', name: 'Lyra', core: true, chains: [
        ['Alp Lyr', 'Zet Lyr', 'Bet Lyr', 'Gam Lyr', 'Del Lyr', 'Zet Lyr'], ['Alp Lyr', 'Eps Lyr']] },
    { id: 'Aql', name: 'Aquila', core: true, chains: [
        ['Bet Aql', 'Alp Aql', 'Gam Aql'], ['Alp Aql', 'Del Aql', 'Lam Aql'],
        ['Del Aql', 'Zet Aql'], ['Del Aql', 'Eta Aql', 'The Aql']] },
    { id: 'Sge', name: 'Sagitta', core: false, chains: [['Gam Sge', 'Del Sge', 'Alp Sge'], ['Del Sge', 'Bet Sge']] },
    { id: 'Del', name: 'Delphinus', core: false, chains: [
        ['Alp Del', 'Bet Del', 'Del Del', 'Gam Del', 'Alp Del'], ['Bet Del', 'Eps Del']] },
    { id: 'Sco', name: 'Scorpius', core: true, chains: [
        ['Bet Sco', 'Del Sco', 'Pi Sco'],
        ['Del Sco', 'Sig Sco', 'Alp Sco', 'Tau Sco', 'Eps Sco', 'Mu Sco', 'Zet Sco', 'Eta Sco', 'The Sco',
         'Iot Sco', 'Kap Sco', 'Lam Sco', 'Ups Sco']] },
    { id: 'Sgr', name: 'Sagittarius', core: true, chains: [
        ['Gam Sgr', 'Eps Sgr', 'Del Sgr', 'Gam Sgr'], ['Del Sgr', 'Phi Sgr', 'Zet Sgr', 'Eps Sgr'],
        ['Del Sgr', 'Lam Sgr', 'Phi Sgr'], ['Phi Sgr', 'Sig Sgr', 'Tau Sgr', 'Zet Sgr']] },
    { id: 'Leo', name: 'Leo', core: true, chains: [
        ['Eps Leo', 'Mu Leo', 'Zet Leo', 'Gam Leo', 'Eta Leo', 'Alp Leo'],
        ['Alp Leo', 'The Leo', 'Bet Leo', 'Del Leo', 'Gam Leo']] },
    { id: 'Gem', name: 'Gemini', core: true, chains: [
        ['Alp Gem', 'Tau Gem', 'Eps Gem', 'Mu Gem', 'Eta Gem'],
        ['Bet Gem', 'Ups Gem', 'Del Gem', 'Zet Gem', 'Gam Gem'], ['Tau Gem', 'Iot Gem', 'Ups Gem']] },
    { id: 'Tau', name: 'Taurus', core: true, chains: [
        ['Zet Tau', 'Alp Tau', 'Gam Tau', 'Del Tau', 'Eps Tau', 'Bet Tau'], ['Gam Tau', 'Lam Tau']] },
    { id: 'Aur', name: 'Auriga', core: true, chains: [['Alp Aur', 'Bet Aur', 'The Aur', 'Iot Aur', 'Alp Aur']] },
    { id: 'CMa', name: 'Canis Major', core: true, chains: [
        ['Bet CMa', 'Alp CMa', 'Del CMa', 'Eta CMa'], ['Del CMa', 'Eps CMa'], ['Alp CMa', 'Gam CMa']] },
    { id: 'CMi', name: 'Canis Minor', core: true, chains: [['Alp CMi', 'Bet CMi']] },
    { id: 'Cnc', name: 'Cancer', core: false, chains: [['Bet Cnc', 'Del Cnc', 'Gam Cnc', 'Iot Cnc'], ['Del Cnc', 'Alp Cnc']] },
    { id: 'Per', name: 'Perseus', core: true, chains: [
        ['Alp Per', 'Del Per', 'Eps Per', 'Zet Per'], ['Alp Per', 'Gam Per', 'Eta Per'], ['Alp Per', 'Bet Per', 'Rho Per']] },
    { id: 'And', name: 'Andromeda', core: false, chains: [['Alp And', 'Del And', 'Bet And', 'Gam And'], ['Bet And', 'Mu And', 'Nu And']] },
    { id: 'Peg', name: 'Pegasus', core: true, chains: [
        ['Alp Peg', 'Bet Peg', 'Alp And', 'Gam Peg', 'Alp Peg'], ['Alp Peg', 'Zet Peg', 'The Peg', 'Eps Peg'],
        ['Bet Peg', 'Mu Peg', 'Lam Peg'], ['Bet Peg', 'Eta Peg']] },
    { id: 'Ari', name: 'Aries', core: false, chains: [['Alp Ari', 'Bet Ari', 'Gam Ari']] },
    { id: 'Tri', name: 'Triangulum', core: false, chains: [['Alp Tri', 'Bet Tri', 'Gam Tri', 'Alp Tri']] },
    { id: 'Boo', name: 'Bootes', core: true, chains: [
        ['Alp Boo', 'Eps Boo', 'Del Boo', 'Bet Boo', 'Gam Boo', 'Rho Boo', 'Alp Boo'], ['Alp Boo', 'Zet Boo'], ['Alp Boo', 'Eta Boo']] },
    { id: 'CrB', name: 'Corona Borealis', core: false, chains: [['The CrB', 'Bet CrB', 'Alp CrB', 'Gam CrB', 'Del CrB', 'Eps CrB', 'Iot CrB']] },
    { id: 'Her', name: 'Hercules', core: false, chains: [
        ['Eps Her', 'Zet Her', 'Eta Her', 'Pi Her', 'Eps Her'], ['Zet Her', 'Bet Her', 'Gam Her'], ['Eps Her', 'Del Her', 'Alp Her'], ['Eta Her', 'Sig Her']] },
    { id: 'Vir', name: 'Virgo', core: false, chains: [['Bet Vir', 'Eta Vir', 'Gam Vir', 'Del Vir', 'Eps Vir'], ['Gam Vir', 'The Vir', 'Alp Vir'], ['Del Vir', 'Zet Vir']] },
    { id: 'Lib', name: 'Libra', core: false, chains: [['Sig Lib', 'Alp Lib', 'Bet Lib', 'Gam Lib']] },
    { id: 'Oph', name: 'Ophiuchus', core: false, chains: [['Alp Oph', 'Kap Oph', 'Del Oph', 'Zet Oph', 'Eta Oph', 'Bet Oph', 'Alp Oph'], ['Del Oph', 'Eps Oph']] },
    { id: 'Dra', name: 'Draco', core: false, chains: [
        ['Gam Dra', 'Bet Dra', 'Nu Dra', 'Xi Dra', 'Gam Dra'], ['Xi Dra', 'Del Dra', 'Zet Dra', 'Eta Dra', 'The Dra', 'Iot Dra', 'Alp Dra', 'Kap Dra', 'Lam Dra']] },
    { id: 'CVn', name: 'Canes Venatici', core: false, chains: [['Alp CVn', 'Bet CVn']] },
    { id: 'Com', name: 'Coma Berenices', core: false, chains: [['Alp Com', 'Bet Com', 'Gam Com']] },
    { id: 'Crv', name: 'Corvus', core: false, chains: [['Gam Crv', 'Del Crv', 'Bet Crv', 'Eps Crv', 'Gam Crv'], ['Eps Crv', 'Alp Crv']] },
    { id: 'Cap', name: 'Capricornus', core: false, chains: [['Alp Cap', 'Bet Cap', 'Psi Cap', 'Ome Cap', 'Zet Cap', 'Del Cap', 'Gam Cap', 'The Cap', 'Alp Cap']] },
    { id: 'Cen', name: 'Centaurus', core: true, chains: [['Alp Cen', 'Bet Cen', 'Eps Cen', 'Gam Cen', 'Del Cen'], ['Eps Cen', 'Zet Cen', 'Eta Cen']] },
    { id: 'Cru', name: 'Crux', core: true, chains: [['Alp Cru', 'Gam Cru'], ['Bet Cru', 'Del Cru']] },
    { id: 'TrA', name: 'Triangulum Australe', core: false, chains: [['Alp TrA', 'Bet TrA', 'Gam TrA', 'Alp TrA']] },
    { id: 'Hyi', name: 'Hydrus', core: false, chains: [['Alp Hyi', 'Bet Hyi', 'Gam Hyi', 'Alp Hyi']] },
];

const GREEK = new Set(['Alp', 'Bet', 'Gam', 'Del', 'Eps', 'Zet', 'Eta', 'The', 'Iot', 'Kap', 'Lam', 'Mu', 'Nu',
    'Xi', 'Omi', 'Pi', 'Rho', 'Sig', 'Tau', 'Ups', 'Phi', 'Chi', 'Psi', 'Ome']);

function parseArgs(argv) {
    const args = { check: false, input: null };
    for (let i = 0; i < argv.length; i++) {
        if (argv[i] === '--check') args.check = true;
        else if (argv[i] === '--input') args.input = argv[++i];
        else throw new Error(`Unknown argument: ${argv[i]}`);
    }
    return args;
}

/** BSC5 record → designation fields and the position/magnitude strings bright_stars.json holds. */
function parseRecord(line) {
    if (line.length < 114) return null;
    const raH = line.slice(75, 77).trim();
    const decD = line.slice(84, 86).trim();
    const vText = line.slice(102, 107).trim();
    if (!raH || !decD || !vText) return null;
    const raDeg = (Number(raH) + Number(line.slice(77, 79)) / 60 + Number(line.slice(79, 83)) / 3600) * 15;
    const decAbs = Number(decD) + Number(line.slice(86, 88)) / 60 + Number(line.slice(88, 90)) / 3600;
    const decDeg = line.slice(83, 84) === '-' ? -decAbs : decAbs;
    const vmag = Number(vText);
    return {
        hr: Number(line.slice(0, 4)),
        flamsteed: line.slice(4, 7).trim(),
        bayer: line.slice(7, 10).trim(),
        superscript: line.slice(10, 11).trim(),
        constellation: line.slice(11, 14).trim(),
        vmag,
        // Same rounding as generate-star-catalog.mjs, so the key finds the shipped row.
        key: [raDeg.toFixed(3), decDeg.toFixed(3), vmag.toFixed(2)].join(','),
    };
}

/** "Alp1 Cen" / "Alp Cen" / "58 Ori" → matching BSC records (brightest first). */
function lookup(records, designation) {
    const match = /^([A-Za-z]{2,3}|\d{1,3})(\d?) ([A-Za-z]{3})$/.exec(designation);
    if (!match) throw new Error(`Malformed designation "${designation}"`);
    const [, name, superscript, constellation] = match;
    const isBayer = GREEK.has(name);
    if (!isBayer && !/^\d+$/.test(name)) throw new Error(`Unknown Bayer letter in "${designation}"`);
    return records
        .filter((r) => r.constellation === constellation &&
            (isBayer ? r.bayer === name && (!superscript || r.superscript === superscript) : r.flamsteed === name))
        .sort((a, b) => a.vmag - b.vmag);
}

function loadShippedStars() {
    const json = JSON.parse(readFileSync(starsPath, 'utf8'));
    const rows = [];
    for (let i = 0; i < json.count; i++) {
        const [ra, dec, vmag] = json.data.slice(i * 4, i * 4 + 3);
        rows.push({ ra, dec, vmag, key: [ra.toFixed(3), dec.toFixed(3), vmag.toFixed(2)].join(',') });
    }
    return rows;
}

function separationDeg(a, b) {
    const rad = Math.PI / 180;
    const cos = Math.sin(a.dec * rad) * Math.sin(b.dec * rad) +
        Math.cos(a.dec * rad) * Math.cos(b.dec * rad) * Math.cos((a.ra - b.ra) * rad);
    return Math.acos(Math.min(1, Math.max(-1, cos))) / rad;
}

function build(rawText) {
    const records = rawText.split('\n').map(parseRecord).filter((r) => r !== null);
    const stars = loadShippedStars();
    const rowByKey = new Map(stars.map((star, index) => [star.key, index]));

    const constellations = FIGURES.map((figure) => {
        const resolve = (designation) => {
            const found = lookup(records, designation);
            if (found.length === 0) throw new Error(`${figure.id}: no BSC5 record for "${designation}"`);
            const row = rowByKey.get(found[0].key);
            if (row === undefined) {
                throw new Error(`${figure.id}: "${designation}" (HR ${found[0].hr}, V ${found[0].vmag}) is not in bright_stars.json`);
            }
            return row;
        };
        const lines = [];
        const seen = new Set();
        for (const chain of figure.chains) {
            for (let i = 0; i + 1 < chain.length; i++) {
                const a = resolve(chain[i]);
                const b = resolve(chain[i + 1]);
                if (a === b) throw new Error(`${figure.id}: ${chain[i]}-${chain[i + 1]} is one star`);
                const key = a < b ? `${a}-${b}` : `${b}-${a}`;
                if (seen.has(key)) throw new Error(`${figure.id}: ${chain[i]}-${chain[i + 1]} drawn twice`);
                seen.add(key);
                const length = separationDeg(stars[a], stars[b]);
                if (length > MAX_SEGMENT_DEG) {
                    throw new Error(`${figure.id}: ${chain[i]}-${chain[i + 1]} spans ${length.toFixed(1)} deg`);
                }
                lines.push(a, b);
            }
        }
        return { id: figure.id, name: figure.name, core: figure.core, lines };
    });

    const body = constellations.map((c) => `  ${JSON.stringify(c)}`).join(',\n');
    return [
        '{',
        '"v":1,',
        '"source":"Stick figures authored in scripts/generate-constellation-lines.mjs, joined to the Yale Bright Star Catalogue (CDS V/50, public domain)",',
        '"stars":"bright_stars.json",',
        `"starCount":${stars.length},`,
        '"constellations":[',
        body,
        ']',
        '}',
        '',
    ].join('\n');
}

/** Shape checks against the shipped star file; returns a list of problems. */
function validate(text) {
    const problems = [];
    let json;
    try {
        json = JSON.parse(text);
    } catch (error) {
        return [`not valid JSON: ${error.message}`];
    }
    const stars = loadShippedStars();
    if (json.v !== 1) problems.push('v must be 1');
    if (json.starCount !== stars.length) problems.push(`starCount ${json.starCount} != bright_stars.json count ${stars.length}`);
    if (!Array.isArray(json.constellations) || json.constellations.length === 0) {
        problems.push('constellations must be a non-empty array');
        return problems;
    }
    const core = json.constellations.filter((c) => c.core).length;
    if (core !== 20) problems.push(`expected 20 core figures for the Low preset, found ${core}`);
    let segments = 0;
    for (const c of json.constellations) {
        if (!Array.isArray(c.lines) || c.lines.length === 0 || c.lines.length % 2 !== 0) {
            problems.push(`${c.id}: lines must be a non-empty flat array of pairs`);
            continue;
        }
        for (let i = 0; i < c.lines.length; i += 2) {
            const [a, b] = [c.lines[i], c.lines[i + 1]];
            if (!(Number.isInteger(a) && Number.isInteger(b) && a >= 0 && b >= 0 && a < stars.length && b < stars.length)) {
                problems.push(`${c.id}: segment ${i / 2} indexes outside bright_stars.json`);
            } else if (separationDeg(stars[a], stars[b]) > MAX_SEGMENT_DEG) {
                problems.push(`${c.id}: segment ${i / 2} is longer than ${MAX_SEGMENT_DEG} deg`);
            }
        }
        segments += c.lines.length / 2;
    }
    if (segments < 150) problems.push(`only ${segments} segments`);
    return problems;
}

function main() {
    const args = parseArgs(process.argv.slice(2));
    if (args.check) {
        const committed = readFileSync(outPath, 'utf8');
        const problems = validate(committed);
        if (args.input && problems.length === 0 && build(readFileSync(args.input, 'latin1')) !== committed) {
            problems.push('constellation_lines.json is stale: rerun with --input');
        }
        if (problems.length) {
            for (const problem of problems) console.error(`constellation_lines.json: ${problem}`);
            process.exit(1);
        }
        console.log(`OK: constellation_lines.json ${args.input ? 'matches the source catalogue' : 'is well-formed'}.`);
        return;
    }
    if (!args.input) throw new Error('--input <catalog> is required (see the header comment)');
    const text = build(readFileSync(args.input, 'latin1'));
    writeFileSync(outPath, text);
    const json = JSON.parse(text);
    const segments = json.constellations.reduce((n, c) => n + c.lines.length / 2, 0);
    console.log(`Wrote ${outPath}: ${json.constellations.length} figures, ${segments} segments.`);
}

main();
