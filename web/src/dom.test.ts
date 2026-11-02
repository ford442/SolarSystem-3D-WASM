import { describe, expect, it } from 'vitest';
import { MissingElementError, queryRequired, queryRequiredIn, required } from './dom.js';

// Vitest runs in Node, so stand in for the DOM classes and the two lookup methods.
class FakeElement {
    readonly tagName: string = 'DIV';
}
class FakeButton extends FakeElement {
    override readonly tagName = 'BUTTON';
}

const root = (elements: Record<string, FakeElement>) => ({
    getElementById: (id: string) => (elements[id] ?? null) as unknown as Element | null,
});

describe('queryRequired', () => {
    it('returns the element when it exists and has the requested type', () => {
        const button = new FakeButton();
        const found = queryRequired('go', FakeButton as unknown as typeof HTMLButtonElement, root({ go: button }));
        expect(found).toBe(button);
    });

    it('names the id and the expected type when the element is missing', () => {
        const lookup = () => queryRequired('go', FakeButton as unknown as typeof HTMLButtonElement, root({}));
        expect(lookup).toThrow(MissingElementError);
        expect(lookup).toThrow('Required element #go (FakeButton) is missing from index.html');
        try {
            lookup();
        } catch (error) {
            expect(error).toMatchObject({
                name: 'MissingElementError',
                selector: '#go',
                expected: 'FakeButton',
                found: null,
            });
        }
    });

    it('names both types when the element is something else', () => {
        const lookup = () =>
            queryRequired('go', FakeButton as unknown as typeof HTMLButtonElement, root({ go: new FakeElement() }));
        expect(lookup).toThrow('Required element #go should be a FakeButton but is a FakeElement');
    });
});

describe('queryRequiredIn', () => {
    it('uses querySelector on the given root and checks the type', () => {
        const icon = new FakeElement();
        const parent = { querySelector: (selector: string) => (selector === '.icon' ? icon : null) };
        const type = FakeElement as unknown as typeof HTMLElement;
        expect(queryRequiredIn(parent as unknown as ParentNode, '.icon', type)).toBe(icon);
        expect(() => queryRequiredIn(parent as unknown as ParentNode, '.nope', type)).toThrow(MissingElementError);
    });
});

describe('required', () => {
    it('passes through a value, including falsy ones', () => {
        expect(required(0, 'zero')).toBe(0);
        expect(required('', 'empty')).toBe('');
        expect(required(false, 'false')).toBe(false);
    });

    it('throws naming what was expected for null and undefined', () => {
        expect(() => required(undefined, 'the site')).toThrow('Expected the site to exist');
        expect(() => required(null, 'the site')).toThrow('Expected the site to exist');
    });
});
