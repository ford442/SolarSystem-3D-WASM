import type { WasmModuleCallbacks } from './wasmCallbacks.js';

export interface ProgressOverlayElements {
    loadingContainer: HTMLElement;
    progressBar: HTMLElement;
    progressText: HTMLElement;
    streamingProgress: HTMLElement;
    streamingText: HTMLElement | null;
    streamingBar: HTMLElement | null;
}

export function createProgressCallbacks(elements: ProgressOverlayElements): WasmModuleCallbacks {
    const {
        loadingContainer,
        progressBar,
        progressText,
        streamingProgress,
        streamingText,
        streamingBar,
    } = elements;

    // `failed` is only passed by the core-resource loader; those resources are already
    // counted in `loaded`, so the bar still completes and the overlay still hides.
    function updateProgress(loaded: number, total: number, failed = 0): void {
        const percentage = total > 0 ? Math.round((loaded / total) * 100) : 0;
        const failedSuffix = failed > 0 ? ` (${failed} failed)` : '';

        if (progressBar && progressText) {
            progressBar.style.width = `${percentage}%`;
            progressText.textContent = failed > 0 ? `${loaded}/${total}${failedSuffix}` : `${percentage}%`;
        }

        console.log(`Loading progress: ${loaded}/${total}${failedSuffix} (${percentage}%)`);

        if (loaded >= total && total > 0) {
            setTimeout(() => {
                loadingContainer.classList.add('hidden');
                loadingContainer.style.pointerEvents = 'none';
                loadingContainer.style.zIndex = '-1';
            }, 500);
        }
    }

    function updateStreamingProgress(completed: number, total: number, active = 0, tierCode = 0): void {
        if (total <= 0) {
            streamingProgress.style.display = 'none';
            return;
        }
        streamingProgress.style.display = 'block';
        const pct = total > 0 ? Math.round((completed / total) * 100) : 0;
        const activeSuffix = active > 0 ? `, ${active} active` : '';
        const tierLabel =
            tierCode === 1 ? 'Mid-res upgrade' : tierCode === 2 ? 'High-res upgrade' : 'Texture upgrade';
        if (streamingText) {
            streamingText.textContent = `${tierLabel}: ${completed}/${total} (${pct}%${activeSuffix})`;
        } else {
            streamingProgress.textContent = `${tierLabel}: ${completed}/${total}`;
        }
        if (streamingBar) {
            streamingBar.style.width = `${pct}%`;
        }
        if (completed >= total) {
            setTimeout(() => {
                streamingProgress.style.display = 'none';
            }, 2500);
        }
    }

    return {
        updateLoadingProgress: updateProgress,
        updateStreamingProgress,
    };
}
