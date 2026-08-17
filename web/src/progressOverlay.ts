export interface ProgressOverlayElements {
    loadingContainer: HTMLElement;
    progressBar: HTMLElement;
    progressText: HTMLElement;
    streamingProgress: HTMLElement;
    streamingText: HTMLElement | null;
    streamingBar: HTMLElement | null;
}

export function initProgressOverlay(elements: ProgressOverlayElements): void {
    const {
        loadingContainer,
        progressBar,
        progressText,
        streamingProgress,
        streamingText,
        streamingBar,
    } = elements;

    function updateProgress(loaded: number, total: number): void {
        const percentage = total > 0 ? Math.round((loaded / total) * 100) : 0;

        if (progressBar && progressText) {
            progressBar.style.width = `${percentage}%`;
            progressText.textContent = `${percentage}%`;
        }

        console.log(`Loading progress: ${loaded}/${total} (${percentage}%)`);

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

    window.updateLoadingProgress = updateProgress;
    window.updateStreamingProgress = updateStreamingProgress;
}
