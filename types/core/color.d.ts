declare namespace Core {
    /**
     * RGBA color. Components are stored as floats in [0, 1] range.
     * Use `Color.fromRGB()` to construct from 0-255 values.
     */
    class Color {
        /** Create a color from normalized components; alpha defaults to 1. */
        constructor(r: number, g: number, b: number, a?: number);

        /** Mutable normalized red component. */
        r: number;
        /** Mutable normalized green component. */
        g: number;
        /** Mutable normalized blue component. */
        b: number;
        /** Mutable normalized alpha component. */
        a: number;

        /** Interpolate every component toward `target`, mutating this color. */
        lerp(target: Color, t: number): this;
        /** Replace normalized components; alpha defaults to 1 when omitted. */
        set(r: number, g: number, b: number, a?: number): this;
        /** Create an independent copy. */
        clone(): Color;
        /** Return uppercase `#RRGGBB`, or `#RRGGBBAA` when alpha is requested. */
        toHex(includeAlpha?: boolean): string;
        /** Format as `Color(r, g, b, a)` for logging. */
        toString(): string;
        /** Convert to a plain normalized component object for `JSON.stringify`. */
        toJSON(): { r: number; g: number; b: number; a: number };

        /** Parse `#RGB`, `#RGBA`, `#RRGGBB`, or `#RRGGBBAA`; invalid input returns opaque white. */
        static fromHex(hex: string): Color;
        /** Construct from 0-255 component values; alpha defaults to 255. */
        static fromRGB(r: number, g: number, b: number, a?: number): Color;
        /** Create opaque white. */
        static white(): Color;
        /** Create opaque black. */
        static black(): Color;
        /** Create opaque red. */
        static red(): Color;
        /** Create opaque green. */
        static green(): Color;
        /** Create opaque blue. */
        static blue(): Color;
        /** Create opaque yellow. */
        static yellow(): Color;
        /** Create opaque cyan. */
        static cyan(): Color;
        /** Create opaque magenta. */
        static magenta(): Color;
        /** Create fully transparent black. */
        static transparent(): Color;
    }
}
