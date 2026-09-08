/**
 * Loading overlay for the companion, mirroring the WASM app's
 * `updateLoadingProgress()` pattern: a denominator is declared up front so the
 * bar advances smoothly, and the overlay only hides once every expected asset
 * has settled (loaded *or* failed — a missing KTX2 must never wedge the UI).
 */
export interface AssetProgressSink {
  start(key: string, label: string): void;
  settle(key: string, loaded: boolean): void;
}

interface OverlayElements {
  root: HTMLElement;
  bar: HTMLElement;
  text: HTMLElement;
  detail: HTMLElement;
}

const HIDE_DELAY_MS = 360;
const SAFETY_TIMEOUT_MS = 20_000;

export class LoadingOverlay implements AssetProgressSink {
  private readonly elements: OverlayElements;
  private readonly expected = new Set<string>();
  private readonly settled = new Set<string>();
  private readonly inFlight = new Map<string, string>();
  private failures = 0;
  private hidden = false;
  private safetyTimer: number | undefined;

  constructor(elements: OverlayElements) {
    this.elements = elements;
  }

  /** Declare the assets that gate first paint. Extra keys are ignored later. */
  expect(keys: string[]): void {
    for (const key of keys) {
      this.expected.add(key);
    }
    this.safetyTimer ??= window.setTimeout(() => {
      if (!this.hidden) {
        console.warn('[threejs-companion] loading overlay safety timeout; showing scene anyway');
        this.hide();
      }
    }, SAFETY_TIMEOUT_MS);
    this.render();
  }

  start(key: string, label: string): void {
    this.inFlight.set(key, label);
    this.render();
  }

  settle(key: string, loaded: boolean): void {
    this.inFlight.delete(key);
    if (!loaded) this.failures++;
    if (this.expected.has(key)) {
      this.settled.add(key);
    }
    this.render();

    if (this.settled.size >= this.expected.size) {
      window.setTimeout(() => this.hide(), HIDE_DELAY_MS);
    }
  }

  /** Progress across the declared set, 0..1. */
  get progress(): number {
    if (this.expected.size === 0) return 1;
    return this.settled.size / this.expected.size;
  }

  get failureCount(): number {
    return this.failures;
  }

  hide(): void {
    if (this.hidden) return;
    this.hidden = true;
    if (this.safetyTimer !== undefined) {
      window.clearTimeout(this.safetyTimer);
      this.safetyTimer = undefined;
    }
    this.elements.root.classList.add('hidden');
    // Keep it out of the a11y tree and off the hit-test path once hidden.
    this.elements.root.setAttribute('aria-hidden', 'true');
    window.setTimeout(() => {
      this.elements.root.hidden = true;
    }, 400);
  }

  setStatus(message: string): void {
    this.elements.detail.textContent = message;
  }

  private render(): void {
    if (this.hidden) return;

    const percent = Math.round(this.progress * 100);
    this.elements.bar.style.width = `${percent}%`;
    this.elements.text.textContent = `${percent}%`;
    this.elements.root.setAttribute('aria-valuenow', String(percent));

    const pending = [...this.inFlight.values()];
    if (pending.length > 0) {
      const head = pending.slice(0, 3).join(', ');
      const rest = pending.length > 3 ? ` +${pending.length - 3} more` : '';
      this.elements.detail.textContent = `Fetching ${head}${rest}`;
    } else if (this.settled.size < this.expected.size) {
      this.elements.detail.textContent =
        `Queued ${this.expected.size - this.settled.size} asset(s)…`;
    } else {
      this.elements.detail.textContent = 'Compiling scene…';
    }
  }
}

export function createLoadingOverlay(): LoadingOverlay {
  return new LoadingOverlay({
    root: requiredElement('loading-overlay'),
    bar: requiredElement('loading-bar'),
    text: requiredElement('loading-percent'),
    detail: requiredElement('loading-detail'),
  });
}

function requiredElement(id: string): HTMLElement {
  const element = document.getElementById(id);
  if (!element) throw new Error(`Missing #${id}`);
  return element;
}
