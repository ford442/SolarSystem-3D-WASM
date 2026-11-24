/**
 * The flight planner panel (docs/ARCHITECTURE.md §14): pick a departure and target body and a
 * date window, compute the porkchop in C++ (GetPorkchopJson) and draw it as a heat-map, click a
 * cell to plan that transfer (PlanTransfer), launch it — the clock jumps to the departure, runs
 * at a rate that flies it in under a minute, and the chase camera rides along — or turn it into
 * a timeline the cinematic dock can record (capture.ts). Real missions (Voyager, Cassini, New
 * Horizons) are presets C++ rebuilds from their encounter dates. `?flight=` links land here.
 */
import { getClipboard, queryRequired } from './dom.js';
import { isoDateFromJulianDate, julianDateFromIsoDate } from './ephemeris.js';
import {
    FLIGHT_BODIES,
    cellAt,
    cellValue,
    colorRange,
    departureJdFromLink,
    flightBodyById,
    flightBodyByIndex,
    flightLinkQuery,
    flightTimeline,
    porkchopColor,
    timeScaleForFlight,
    type FlightLink,
    type FlightPlanReport,
    type PorkchopGrid,
    type PorkchopMetric,
} from './flight.js';
import type { TimelineEditorHandle } from './timelineEditor.js';
import type { SolarSystemRuntime } from './wasmBridge.js';

export interface FlightPlannerController {
    open(): void;
    /** Apply a `?flight=` link: plan and launch it, or open the porkchop for a bare body pair. */
    applyLink(link: FlightLink): void;
}

const GRID_DEPARTURES = 72;
const GRID_TOFS = 48;
const STATE_POLL_MS = 500;

function formatKm(km: number): string {
    if (km >= 1e6) return `${(km / 1e6).toFixed(2)} M km`;
    return `${Math.round(km).toLocaleString('en-US')} km`;
}

export function initFlightPlanner(options: {
    runtime: SolarSystemRuntime;
    editor: TimelineEditorHandle;
    status: HTMLElement;
}): FlightPlannerController {
    const { runtime, editor } = options;
    const panel = queryRequired('flight-panel');
    const openButton = queryRequired('flight-open', HTMLButtonElement);
    const closeButton = queryRequired('flight-close', HTMLButtonElement);
    const fromSelect = queryRequired('flight-from', HTMLSelectElement);
    const toSelect = queryRequired('flight-to', HTMLSelectElement);
    const depStart = queryRequired('flight-dep-start', HTMLInputElement);
    const depSpan = queryRequired('flight-dep-span', HTMLInputElement);
    const tofMin = queryRequired('flight-tof-min', HTMLInputElement);
    const tofMax = queryRequired('flight-tof-max', HTMLInputElement);
    const metricSelect = queryRequired('flight-metric', HTMLSelectElement);
    const computeButton = queryRequired('flight-compute', HTMLButtonElement);
    const canvas = queryRequired('flight-porkchop', HTMLCanvasElement);
    const axes = queryRequired('flight-axes');
    const hover = queryRequired('flight-hover');
    const presetSelect = queryRequired('flight-preset', HTMLSelectElement);
    const launchButton = queryRequired('flight-launch', HTMLButtonElement);
    const chaseInput = queryRequired('flight-chase', HTMLInputElement);
    const videoButton = queryRequired('flight-video', HTMLButtonElement);
    const copyLinkButton = queryRequired('flight-copy-link', HTMLButtonElement);
    const clearButton = queryRequired('flight-clear', HTMLButtonElement);
    const report = queryRequired('flight-report', HTMLDListElement);
    const status = queryRequired('flight-status');

    let grid: PorkchopGrid | null = null;
    let selected: number | null = null; // grid cell index
    let plan: FlightPlanReport = { ok: false };
    let link: FlightLink | null = null;
    let poll = 0;

    const setStatus = (text: string): void => {
        status.textContent = text;
    };

    for (const body of FLIGHT_BODIES) {
        fromSelect.add(new Option(body.name, String(body.index)));
        toSelect.add(new Option(body.name, String(body.index)));
    }
    fromSelect.value = '3';
    toSelect.value = '4';
    for (const preset of runtime.getFlightPresets()) {
        presetSelect.add(new Option(`${preset.name} (${preset.sequence})`, preset.id));
    }

    const metric = (): PorkchopMetric => metricSelect.value as PorkchopMetric;

    const draw = (): void => {
        const context = canvas.getContext('2d');
        if (!context) return;
        context.clearRect(0, 0, canvas.width, canvas.height);
        if (!grid) return;
        // One pixel per cell on an offscreen canvas, scaled up unsmoothed: the cells stay crisp.
        const cells = document.createElement('canvas');
        cells.width = grid.departureSteps;
        cells.height = grid.tofSteps;
        const cellContext = cells.getContext('2d');
        if (!cellContext) return;
        const image = cellContext.createImageData(grid.departureSteps, grid.tofSteps);
        const { min, max } = colorRange(grid, metric());
        for (let j = 0; j < grid.tofSteps; ++j) {
            for (let i = 0; i < grid.departureSteps; ++i) {
                const index = j * grid.departureSteps + i;
                const rgba = porkchopColor(cellValue(grid, metric(), index), min, max);
                // Flight time runs up the plot: row 0 of the grid is the bottom row of the image.
                image.data.set(rgba, ((grid.tofSteps - 1 - j) * grid.departureSteps + i) * 4);
            }
        }
        cellContext.putImageData(image, 0, 0);
        context.imageSmoothingEnabled = false;
        context.drawImage(cells, 0, 0, canvas.width, canvas.height);

        const cellW = canvas.width / grid.departureSteps;
        const cellH = canvas.height / grid.tofSteps;
        const mark = (index: number, color: string, radius: number): void => {
            if (!grid) return;
            const i = index % grid.departureSteps;
            const j = Math.floor(index / grid.departureSteps);
            context.strokeStyle = color;
            context.lineWidth = 2;
            context.beginPath();
            context.arc((i + 0.5) * cellW, canvas.height - (j + 0.5) * cellH, radius, 0, Math.PI * 2);
            context.stroke();
        };
        if (grid.best) {
            const i = Math.round((grid.best.departureJd - grid.departureStartJd) / grid.departureStepDays);
            const j = Math.round((grid.best.tofDays - grid.tofMinDays) / grid.tofStepDays);
            mark(j * grid.departureSteps + i, '#ffffff', 6);
        }
        if (selected !== null) {
            mark(selected, '#ff4fa3', 9);
        }
        const unit = metric() === 'vinf' ? 'km/s' : 'km²/s²';
        axes.textContent =
            `Departure ${isoDateFromJulianDate(grid.departureStartJd)} → ` +
            `${isoDateFromJulianDate(grid.departureStartJd + (grid.departureSteps - 1) * grid.departureStepDays)} · ` +
            `flight ${String(Math.round(grid.tofMinDays))}–` +
            `${String(Math.round(grid.tofMinDays + (grid.tofSteps - 1) * grid.tofStepDays))} d (up) · ` +
            `bright = cheap, ${min.toFixed(1)}–${max.toFixed(1)} ${unit} · ○ optimum`;
    };

    const describeCell = (index: number): string => {
        if (!grid) return '';
        const c3 = grid.c3.at(index) ?? null;
        const vInf = grid.vInfArrive.at(index) ?? null;
        const i = index % grid.departureSteps;
        const j = Math.floor(index / grid.departureSteps);
        const dep = grid.departureStartJd + i * grid.departureStepDays;
        const tof = grid.tofMinDays + j * grid.tofStepDays;
        const where = `Depart ${isoDateFromJulianDate(dep)} · ${String(Math.round(tof))} d → ${isoDateFromJulianDate(dep + tof)}`;
        if (c3 === null || vInf === null) return `${where} · no transfer`;
        return `${where} · C3 ${c3.toFixed(2)} km²/s² (v∞ ${Math.sqrt(c3).toFixed(2)} km/s) · arrival v∞ ${vInf.toFixed(2)} km/s`;
    };

    const renderReport = (): void => {
        report.replaceChildren();
        const row = (term: string, value: string): void => {
            const dt = document.createElement('dt');
            dt.textContent = term;
            const dd = document.createElement('dd');
            dd.textContent = value;
            report.append(dt, dd);
        };
        if (!plan.ok) {
            if (plan.error) row('No flight', plan.error);
            return;
        }
        row('Flight', plan.name ?? '');
        row('Launch C3', `${(plan.c3 ?? 0).toFixed(2)} km²/s²`);
        for (const e of plan.encounters ?? []) {
            let text = `${e.date} · `;
            if (e.kind === 'launch') {
                text += `v∞ ${e.vInfOut.toFixed(2)} km/s, Δv ${e.deltaV.toFixed(2)} km/s from a 200 km orbit`;
            } else if (e.kind === 'arrival') {
                text += `v∞ ${e.vInfIn.toFixed(2)} km/s, capture Δv ${e.deltaV.toFixed(2)} km/s (500 km orbit)`;
            } else {
                text +=
                    `v∞ ${e.vInfIn.toFixed(2)}→${e.vInfOut.toFixed(2)} km/s, turn ${(e.turnDeg ?? 0).toFixed(1)}°, ` +
                    `periapsis ${formatKm(e.altitudeKm ?? 0)} up` +
                    (e.historicAltitudeKm !== undefined ? ` (flown: ${formatKm(e.historicAltitudeKm)})` : '') +
                    `, B·T ${formatKm(e.bDotT ?? 0)}, B·R ${formatKm(e.bDotR ?? 0)}` +
                    (e.deltaV > 0.005 ? `, powered Δv ${e.deltaV.toFixed(3)} km/s` : '') +
                    (e.feasible === false ? ' — below the safe altitude' : '');
            }
            row(`${e.bodyName} ${e.kind}`, text);
        }
        row('Total Δv', `${(plan.totalDeltaV ?? 0).toFixed(2)} km/s`);
    };

    const refreshButtons = (): void => {
        launchButton.disabled = !plan.ok;
        videoButton.disabled = !plan.ok;
        copyLinkButton.disabled = !plan.ok || !link;
        clearButton.disabled = !plan.ok;
    };

    const adopt = (ok: boolean, newLink: FlightLink | null): boolean => {
        plan = runtime.getFlightPlan();
        link = ok ? newLink : null;
        renderReport();
        refreshButtons();
        return ok && plan.ok;
    };

    const planCell = (index: number): boolean => {
        if (!grid) return false;
        selected = index;
        const i = index % grid.departureSteps;
        const j = Math.floor(index / grid.departureSteps);
        const dep = grid.departureStartJd + i * grid.departureStepDays;
        const tof = grid.tofMinDays + j * grid.tofStepDays;
        const from = flightBodyByIndex(grid.departureBody);
        const to = flightBodyByIndex(grid.arrivalBody);
        const ok = runtime.planTransfer(grid.departureBody, grid.arrivalBody, dep, tof);
        draw();
        setStatus(ok ? `Planned: ${describeCell(index)}. Launch to fly it.` : 'No transfer from that cell.');
        return adopt(
            ok,
            from && to
                ? { kind: 'transfer', from: from.id, to: to.id, departure: isoDateFromJulianDate(dep), tofDays: tof }
                : null,
        );
    };

    const compute = (): void => {
        const from = Number(fromSelect.value);
        const to = Number(toSelect.value);
        if (from === to) {
            setStatus('Pick two different bodies.');
            return;
        }
        const start = julianDateFromIsoDate(depStart.value) ?? runtime.getSimulationEpoch();
        const lo = Math.max(5, Number(tofMin.value) || 100);
        const hi = Math.max(lo + 10, Number(tofMax.value) || 400);
        const started = performance.now();
        grid = runtime.computePorkchop({
            from,
            to,
            departureStartJd: start,
            departureSpanDays: Math.max(10, Number(depSpan.value) || 180),
            tofMinDays: lo,
            tofMaxDays: hi,
            departureSteps: GRID_DEPARTURES,
            tofSteps: GRID_TOFS,
        });
        selected = null;
        draw();
        if (!grid?.best) {
            setStatus('No transfers in that window.');
            return;
        }
        const ms = performance.now() - started;
        setStatus(
            `${String(GRID_DEPARTURES * GRID_TOFS)} Lambert solves in ${ms.toFixed(0)} ms (${String(grid.tiles)} job tiles). ` +
                'Click a cell to plan it; the ring is the cheapest.',
        );
    };

    const launch = (): void => {
        if (!plan.ok || plan.startJd === undefined || plan.endJd === undefined) return;
        runtime.setSimulationEpoch(plan.startJd);
        runtime.setTimeScale(timeScaleForFlight(plan.endJd - plan.startJd));
        runtime.setPaused(false);
        runtime.setFlightChase(chaseInput.checked);
        setStatus(`Launched ${plan.name ?? ''} on ${isoDateFromJulianDate(plan.startJd)}.`);
    };

    const pollState = (): void => {
        const state = runtime.getFlightState();
        if (!state.valid || panel.hidden) return;
        const target = flightBodyByIndex(state.toBody ?? -1)?.name ?? 'target';
        const phase =
            state.phase === 'before-launch' ? 'Waiting for launch' : state.phase === 'arrived' ? 'Arrived' : 'Cruise';
        setStatus(
            `${phase} · ${isoDateFromJulianDate(state.jd ?? 0)} · ${((state.progress ?? 0) * 100).toFixed(0)}% · ` +
                `${(state.speedKmS ?? 0).toFixed(1)} km/s · ${(state.sunDistanceAu ?? 0).toFixed(2)} AU from the Sun · ` +
                `${formatKm(state.targetDistanceKm ?? 0)} to ${target}`,
        );
    };

    const open = (): void => {
        panel.hidden = false;
        if (!depStart.value) {
            depStart.value = isoDateFromJulianDate(runtime.getSimulationEpoch());
        }
        if (!poll) poll = window.setInterval(pollState, STATE_POLL_MS);
    };

    const close = (): void => {
        panel.hidden = true;
        window.clearInterval(poll);
        poll = 0;
    };

    for (const eventName of ['pointerdown', 'pointerup', 'click', 'keydown', 'keyup', 'wheel']) {
        panel.addEventListener(eventName, (event) => event.stopPropagation());
    }
    openButton.addEventListener('click', open);
    closeButton.addEventListener('click', close);
    computeButton.addEventListener('click', compute);
    metricSelect.addEventListener('change', draw);
    canvas.addEventListener('pointermove', (event) => {
        if (!grid) return;
        const rect = canvas.getBoundingClientRect();
        const cell = cellAt(grid, event.clientX - rect.left, event.clientY - rect.top, rect.width, rect.height);
        hover.textContent = cell ? describeCell(cell.index) : '';
    });
    canvas.addEventListener('click', (event) => {
        if (!grid) return;
        const rect = canvas.getBoundingClientRect();
        const cell = cellAt(grid, event.clientX - rect.left, event.clientY - rect.top, rect.width, rect.height);
        if (cell) planCell(cell.index);
    });
    presetSelect.addEventListener('change', () => {
        const id = presetSelect.value;
        if (!id) return;
        const ok = runtime.planFlightPreset(id);
        setStatus(
            ok
                ? `${runtime.getFlightPlan().name ?? id} rebuilt from its encounter dates. Launch to fly it.`
                : 'Not available here.',
        );
        adopt(ok, { kind: 'preset', id });
    });
    launchButton.addEventListener('click', launch);
    chaseInput.addEventListener('change', () => {
        runtime.setFlightChase(chaseInput.checked);
    });
    videoButton.addEventListener('click', () => {
        const timeline = flightTimeline(plan);
        if (timeline) {
            editor.edit(timeline, `${timeline.title}: play it, or record it with Capture.`);
        }
    });
    copyLinkButton.addEventListener('click', () => {
        if (!link) return;
        const url = `${window.location.origin}${window.location.pathname}?${flightLinkQuery(link)}`;
        void getClipboard()
            ?.writeText(url)
            .then(() => {
                setStatus('Flight link copied.');
            })
            .catch(() => {
                setStatus(url);
            });
    });
    clearButton.addEventListener('click', () => {
        runtime.clearFlight();
        selected = null;
        presetSelect.value = '';
        adopt(false, null);
        draw();
        setStatus('Flight cleared.');
    });

    const applyLink = (target: FlightLink): void => {
        open();
        if (target.kind === 'preset') {
            presetSelect.value = target.id;
            if (adopt(runtime.planFlightPreset(target.id), target)) {
                launch();
            } else {
                setStatus(`No mission preset "${target.id}".`);
            }
            return;
        }
        const from = flightBodyById(target.from);
        const to = flightBodyById(target.to);
        if (!from || !to) return;
        fromSelect.value = String(from.index);
        toSelect.value = String(to.index);
        const departureJd = departureJdFromLink(target);
        if (departureJd !== undefined && target.tofDays !== undefined) {
            if (target.departure) depStart.value = target.departure;
            if (adopt(runtime.planTransfer(from.index, to.index, departureJd, target.tofDays), target)) {
                launch();
            } else {
                setStatus('That transfer has no solution.');
            }
            return;
        }
        if (target.departure) depStart.value = target.departure;
        compute();
    };

    return { open, applyLink };
}
