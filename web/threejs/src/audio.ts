export interface MusicConfig {
  enabled: boolean;
  /** Path relative to the asset base, e.g. `resource/sounds/`. */
  directory: string;
  volume: number;
  tracks: string[];
}

type MusicState = 'idle' | 'playing' | 'unavailable';

/**
 * Optional background music using the same MP3 paths as the WASM build. Those
 * files are deploy artifacts (Git LFS, excluded from the web preload), so a 404
 * is an expected outcome: the toggle reports "unavailable" and the scene keeps
 * running. Playback starts from a click so autoplay policies are satisfied.
 */
export class CompanionMusic {
  private readonly config: MusicConfig;
  private readonly baseUrl: string;
  private readonly button: HTMLButtonElement;
  private audio: HTMLAudioElement | null = null;
  private trackIndex = 0;
  private state: MusicState = 'idle';

  constructor(config: MusicConfig, baseUrl: string, button: HTMLButtonElement) {
    this.config = config;
    this.baseUrl = baseUrl;
    this.button = button;

    if (!config.enabled || config.tracks.length === 0) {
      this.button.hidden = true;
      return;
    }

    this.button.addEventListener('click', () => void this.toggle());
    this.updateButton();
  }

  private async toggle(): Promise<void> {
    if (this.state === 'unavailable') return;

    if (this.state === 'playing') {
      this.audio?.pause();
      this.state = 'idle';
      this.updateButton();
      return;
    }

    try {
      await this.play();
      this.state = 'playing';
    } catch (error) {
      console.warn('[threejs-companion] background music unavailable', error);
      this.state = 'unavailable';
    }
    this.updateButton();
  }

  private async play(): Promise<void> {
    if (!this.audio) {
      this.audio = new Audio();
      this.audio.volume = this.config.volume;
      this.audio.preload = 'none';
      this.audio.addEventListener('ended', () => void this.advance());
      this.audio.addEventListener('error', () => {
        this.state = 'unavailable';
        this.updateButton();
      });
    }

    if (!this.audio.src) {
      this.audio.src = this.trackUrl(this.trackIndex);
    }
    await this.audio.play();
  }

  private async advance(): Promise<void> {
    if (!this.audio) return;
    this.trackIndex = (this.trackIndex + 1) % this.config.tracks.length;
    this.audio.src = this.trackUrl(this.trackIndex);
    try {
      await this.audio.play();
    } catch (error) {
      console.warn('[threejs-companion] music track advance failed', error);
      this.state = 'idle';
      this.updateButton();
    }
  }

  private trackUrl(index: number): string {
    const directory = this.config.directory.endsWith('/')
      ? this.config.directory
      : `${this.config.directory}/`;
    return new URL(
      `${directory}${encodeURIComponent(this.config.tracks[index])}`,
      this.baseUrl,
    ).toString();
  }

  private updateButton(): void {
    const label = {
      idle: '♪ Music off',
      playing: `♪ ${stripExtension(this.config.tracks[this.trackIndex])}`,
      unavailable: '♪ Music unavailable',
    }[this.state];

    this.button.textContent = label;
    this.button.disabled = this.state === 'unavailable';
    this.button.setAttribute('aria-pressed', String(this.state === 'playing'));
  }
}

function stripExtension(name: string): string {
  return name.replace(/\.[^.]+$/, '');
}
