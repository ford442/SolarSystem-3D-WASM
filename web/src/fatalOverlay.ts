export interface FatalPanelOptions {
    title: string;
    message: string;
    /** What the user can try; one line per entry. */
    hints?: string[];
    /** Called just before the Reload button reloads; returns the URL to load (defaults to the current one). */
    reloadUrl?: () => string;
}

/**
 * Full-screen error panel used instead of a loading overlay that would otherwise sit there
 * forever (module boot failure, no WebGL2) or a canvas that stays black (context loss).
 * Only one panel is shown; a later call replaces the earlier one.
 */
export function showFatalPanel(options: FatalPanelOptions): HTMLElement {
    document.getElementById('fatal-panel')?.remove();
    document.getElementById('loading-container')?.classList.add('hidden');

    const panel = document.createElement('div');
    panel.id = 'fatal-panel';
    panel.setAttribute('role', 'alertdialog');
    panel.setAttribute('aria-labelledby', 'fatal-panel-title');

    const title = document.createElement('h2');
    title.id = 'fatal-panel-title';
    title.textContent = options.title;

    const message = document.createElement('p');
    message.className = 'fatal-panel-message';
    message.textContent = options.message;
    panel.append(title, message);

    if (options.hints && options.hints.length > 0) {
        const list = document.createElement('ul');
        list.className = 'fatal-panel-hints';
        for (const hint of options.hints) {
            const item = document.createElement('li');
            item.textContent = hint;
            list.appendChild(item);
        }
        panel.appendChild(list);
    }

    const reload = document.createElement('button');
    reload.type = 'button';
    reload.id = 'fatal-panel-reload';
    reload.textContent = 'Reload';
    reload.addEventListener('click', () => {
        let target = window.location.href;
        try {
            target = options.reloadUrl?.() ?? target;
        } catch (error) {
            console.warn('[Fatal] could not capture the current view for reload:', error);
        }
        if (target === window.location.href) {
            window.location.reload();
        } else {
            window.location.assign(target);
        }
    });
    panel.appendChild(reload);

    document.body.appendChild(panel);
    reload.focus();
    return panel;
}
