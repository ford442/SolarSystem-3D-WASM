/**
 * Startup DOM lookups. index.html is the contract: if an element the code needs is gone (or
 * has become a different tag), fail at startup with a name that says which one, instead of
 * a `null` dereference three modules later.
 */

/** A DOM element constructor such as `HTMLButtonElement`; `instanceof` narrows with it. */
export type ElementClass<T extends Element> = abstract new (...args: never[]) => T;

/** The slice of `document` queryRequired reads, so tests can pass a fake. */
export interface ElementLookup {
    getElementById(id: string): Element | null;
}

export class MissingElementError extends Error {
    readonly selector: string;
    readonly expected: string;
    /** The constructor name of what was found instead, or null when nothing matched. */
    readonly found: string | null;

    constructor(selector: string, expected: string, found: string | null) {
        super(
            found === null
                ? `Required element ${selector} (${expected}) is missing from index.html`
                : `Required element ${selector} should be a ${expected} but is a ${found}`,
        );
        this.name = 'MissingElementError';
        this.selector = selector;
        this.expected = expected;
        this.found = found;
    }
}

function describe(element: Element): string {
    return element.constructor.name || element.tagName.toLowerCase();
}

function check<T extends Element>(element: Element | null, selector: string, type: ElementClass<T>): T {
    if (element === null) {
        throw new MissingElementError(selector, type.name, null);
    }
    if (!(element instanceof type)) {
        throw new MissingElementError(selector, type.name, describe(element));
    }
    return element;
}

/**
 * `document.getElementById(id)` that must exist and must be a `type` (default: any
 * HTMLElement). Throws MissingElementError otherwise.
 */
export function queryRequired(id: string, root?: ElementLookup): HTMLElement;
export function queryRequired<T extends Element>(id: string, type: ElementClass<T>, root?: ElementLookup): T;
export function queryRequired(
    id: string,
    typeOrRoot?: ElementClass<Element> | ElementLookup,
    maybeRoot?: ElementLookup,
): Element {
    const type = typeof typeOrRoot === 'function' ? typeOrRoot : HTMLElement;
    const root = (typeof typeOrRoot === 'function' ? maybeRoot : typeOrRoot) ?? document;
    return check(root.getElementById(id), `#${id}`, type);
}

/** `root.querySelector(selector)` that must exist and must be a `type`. */
export function queryRequiredIn<T extends Element>(root: ParentNode, selector: string, type: ElementClass<T>): T {
    return check(root.querySelector(selector), selector, type);
}

/**
 * `navigator.clipboard`, typed as what it really is: undefined outside secure contexts
 * (plain http, some embedded webviews), which lib.dom.d.ts does not admit.
 */
export function getClipboard(): Clipboard | undefined {
    return (navigator as { clipboard?: Clipboard }).clipboard;
}

/** Unwrap a value the surrounding code guarantees, with a message that names what broke. */
export function required<T>(value: T | null | undefined, what: string): T {
    if (value === null || value === undefined) {
        throw new Error(`Expected ${what} to exist`);
    }
    return value;
}
