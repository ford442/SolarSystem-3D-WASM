export type SetTouchMovement = (forward: number, right: number, vertical: number) => void;
export type AddTouchLook = (deltaX: number, deltaY: number) => void;
export type AddTouchZoom = (delta: number) => void;

export interface TouchControlBindings {
    setTouchMovement: SetTouchMovement;
    addTouchLook: AddTouchLook;
    addTouchZoom: AddTouchZoom;
}

export function isMobileLikeDevice(): boolean {
    const ua = navigator.userAgent || '';
    const mobileUa = /Android|webOS|iPhone|iPad|iPod|BlackBerry|IEMobile|Opera Mini/i.test(ua);
    const coarsePointer = window.matchMedia('(pointer: coarse)').matches;
    const smallScreen = Math.min(window.screen.width, window.screen.height) <= 768;
    return mobileUa || (coarsePointer && smallScreen);
}

interface TouchControlsOptions {
    canvas: HTMLCanvasElement;
    settingsPanel: HTMLElement;
    explorerPanel?: HTMLElement;
    loadingContainer: HTMLElement;
    bindings: TouchControlBindings;
}

const LOOK_SENSITIVITY = 0.65;
const ZOOM_SENSITIVITY = 0.004;
const JOYSTICK_MAX_RADIUS = 58;
const JOYSTICK_DEAD_ZONE = 0.12;

function touchDistance(touches: TouchList): number {
    const dx = touches[0].clientX - touches[1].clientX;
    const dy = touches[0].clientY - touches[1].clientY;
    return Math.hypot(dx, dy);
}

function isUiTarget(target: EventTarget | null, panels: HTMLElement[]): boolean {
    if (!(target instanceof Element)) return false;
    return panels.some((panel) => panel.contains(target));
}

export function initTouchControls(options: TouchControlsOptions): () => void {
    const { canvas, settingsPanel, explorerPanel, loadingContainer, bindings } = options;
    const uiPanels = explorerPanel ? [settingsPanel, explorerPanel] : [settingsPanel];

    if (!isMobileLikeDevice()) {
        return () => undefined;
    }

    const root = document.getElementById('touch-controls');
    const joystickZone = document.getElementById('touch-joystick-zone');
    const joystickBase = document.getElementById('touch-joystick-base');
    const joystickKnob = document.getElementById('touch-joystick-knob');
    if (!root || !joystickZone || !joystickBase || !joystickKnob) {
        console.warn('[Touch] Touch overlay elements missing; controls disabled');
        return () => undefined;
    }

    root.hidden = false;

    let joystickTouchId: number | null = null;
    let joystickCenterX = 0;
    let joystickCenterY = 0;
    let lookTouchId: number | null = null;
    let lastLookX = 0;
    let lastLookY = 0;
    let pinchDistance = 0;
    let pinchActive = false;

    const resetJoystickVisual = (): void => {
        joystickKnob.style.transform = 'translate(-50%, -50%)';
        joystickBase.style.opacity = '0.45';
    };

    const updateJoystickVisual = (dx: number, dy: number): void => {
        joystickKnob.style.transform = `translate(calc(-50% + ${dx}px), calc(-50% + ${dy}px))`;
        joystickBase.style.opacity = '0.75';
    };

    const applyJoystickVector = (normX: number, normY: number): void => {
        const magnitude = Math.hypot(normX, normY);
        if (magnitude < JOYSTICK_DEAD_ZONE) {
            bindings.setTouchMovement(0, 0, 0);
            return;
        }
        const scale = Math.min(1, magnitude);
        bindings.setTouchMovement(-normY * scale, normX * scale, 0);
    };

    const isInteractionBlocked = (): boolean => !loadingContainer.classList.contains('hidden');

    const isJoystickZone = (clientX: number, clientY: number): boolean => {
        const rect = joystickZone.getBoundingClientRect();
        return clientX >= rect.left && clientX <= rect.right && clientY >= rect.top && clientY <= rect.bottom;
    };

    const onTouchStart = (event: TouchEvent): void => {
        if (isInteractionBlocked() || isUiTarget(event.target, uiPanels)) return;

        if (event.touches.length >= 2) {
            pinchActive = true;
            pinchDistance = touchDistance(event.touches);
            lookTouchId = null;
            joystickTouchId = null;
            bindings.setTouchMovement(0, 0, 0);
            resetJoystickVisual();
            event.preventDefault();
            return;
        }

        const touch = event.changedTouches[0];
        if (joystickTouchId === null && isJoystickZone(touch.clientX, touch.clientY)) {
            joystickTouchId = touch.identifier;
            joystickCenterX = touch.clientX;
            joystickCenterY = touch.clientY;
            joystickBase.style.left = `${touch.clientX}px`;
            joystickBase.style.top = `${touch.clientY}px`;
            joystickKnob.style.left = `${touch.clientX}px`;
            joystickKnob.style.top = `${touch.clientY}px`;
            updateJoystickVisual(0, 0);
            event.preventDefault();
            return;
        }

        if (lookTouchId === null && !isJoystickZone(touch.clientX, touch.clientY)) {
            lookTouchId = touch.identifier;
            lastLookX = touch.clientX;
            lastLookY = touch.clientY;
            event.preventDefault();
        }
    };

    const onTouchMove = (event: TouchEvent): void => {
        if (isInteractionBlocked() || isUiTarget(event.target, uiPanels)) return;

        if (event.touches.length >= 2) {
            const distance = touchDistance(event.touches);
            if (pinchActive && pinchDistance > 0) {
                const delta = (distance - pinchDistance) * ZOOM_SENSITIVITY;
                bindings.addTouchZoom(delta);
            }
            pinchDistance = distance;
            pinchActive = true;
            event.preventDefault();
            return;
        }

        pinchActive = false;

        for (let i = 0; i < event.changedTouches.length; i += 1) {
            const touch = event.changedTouches[i];

            if (touch.identifier === joystickTouchId) {
                const dx = touch.clientX - joystickCenterX;
                const dy = touch.clientY - joystickCenterY;
                const distance = Math.hypot(dx, dy);
                const clampedDistance = Math.min(distance, JOYSTICK_MAX_RADIUS);
                const angle = distance > 0 ? Math.atan2(dy, dx) : 0;
                const visualDx = Math.cos(angle) * clampedDistance;
                const visualDy = Math.sin(angle) * clampedDistance;
                updateJoystickVisual(visualDx, visualDy);
                applyJoystickVector(
                    visualDx / JOYSTICK_MAX_RADIUS,
                    visualDy / JOYSTICK_MAX_RADIUS,
                );
                event.preventDefault();
                continue;
            }

            if (touch.identifier === lookTouchId) {
                const deltaX = (touch.clientX - lastLookX) * LOOK_SENSITIVITY;
                const deltaY = (lastLookY - touch.clientY) * LOOK_SENSITIVITY;
                bindings.addTouchLook(deltaX, deltaY);
                lastLookX = touch.clientX;
                lastLookY = touch.clientY;
                event.preventDefault();
            }
        }
    };

    const releaseJoystick = (): void => {
        joystickTouchId = null;
        bindings.setTouchMovement(0, 0, 0);
        resetJoystickVisual();
    };

    const onTouchEnd = (event: TouchEvent): void => {
        if (event.touches.length < 2) {
            pinchActive = false;
            pinchDistance = 0;
        }

        for (let i = 0; i < event.changedTouches.length; i += 1) {
            const touch = event.changedTouches[i];
            if (touch.identifier === joystickTouchId) {
                releaseJoystick();
            }
            if (touch.identifier === lookTouchId) {
                lookTouchId = null;
            }
        }
    };

    const onTouchCancel = (event: TouchEvent): void => {
        onTouchEnd(event);
        releaseJoystick();
        lookTouchId = null;
        pinchActive = false;
        pinchDistance = 0;
    };

    const touchOptions: AddEventListenerOptions = { passive: false };
    canvas.addEventListener('touchstart', onTouchStart, touchOptions);
    canvas.addEventListener('touchmove', onTouchMove, touchOptions);
    canvas.addEventListener('touchend', onTouchEnd, touchOptions);
    canvas.addEventListener('touchcancel', onTouchCancel, touchOptions);

    document.addEventListener('touchmove', (event) => {
        if (event.target === canvas || root.contains(event.target as Node)) {
            event.preventDefault();
        }
    }, touchOptions);

    return () => {
        canvas.removeEventListener('touchstart', onTouchStart);
        canvas.removeEventListener('touchmove', onTouchMove);
        canvas.removeEventListener('touchend', onTouchEnd);
        canvas.removeEventListener('touchcancel', onTouchCancel);
        releaseJoystick();
        root.hidden = true;
    };
}
