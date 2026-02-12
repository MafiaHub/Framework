declare namespace Core {
    /**
     * RGBA color. Components are stored as floats in [0, 1] range.
     * Use `Color.fromRGB()` to construct from 0-255 values.
     */
    class Color {
        constructor(r: number, g: number, b: number, a?: number);

        r: number;
        g: number;
        b: number;
        a: number;

        lerp(target: Color, t: number): this;
        set(r: number, g: number, b: number, a?: number): this;
        clone(): Color;
        toHex(includeAlpha?: boolean): string;
        toString(): string;
        toJSON(): { r: number; g: number; b: number; a: number };

        static fromHex(hex: string): Color;
        /** Construct from 0-255 component values. */
        static fromRGB(r: number, g: number, b: number, a?: number): Color;
        static white(): Color;
        static black(): Color;
        static red(): Color;
        static green(): Color;
        static blue(): Color;
        static yellow(): Color;
        static cyan(): Color;
        static magenta(): Color;
        static transparent(): Color;
    }
}
