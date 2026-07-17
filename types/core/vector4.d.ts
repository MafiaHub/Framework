declare namespace Core {
    class Vector4 {
        /** Create a vector from its X, Y, Z, and W components. */
        constructor(x: number, y: number, z: number, w: number);

        /** Mutable X component. */
        x: number;
        /** Mutable Y component. */
        y: number;
        /** Mutable Z component. */
        z: number;
        /** Mutable W component. */
        w: number;
        /** Euclidean magnitude of this vector. */
        readonly length: number;
        /** Squared magnitude, avoiding a square-root calculation. */
        readonly lengthSquared: number;

        /** Add another vector component-wise, mutating this vector. */
        add(other: Vector4): this;
        /** Subtract another vector component-wise, mutating this vector. */
        sub(other: Vector4): this;
        /** Multiply every component by a scalar, mutating this vector. */
        mul(scalar: number): this;
        /** Divide every component by a non-zero scalar, mutating this vector. */
        div(scalar: number): this;
        /** Normalize this vector in place; a zero vector remains unchanged. */
        normalize(): this;
        /** Compute the scalar dot product without changing either vector. */
        dot(other: Vector4): number;
        /** Compute Euclidean distance to another vector. */
        distance(other: Vector4): number;
        /** Interpolate toward `target`; `t=0` keeps this value and `t=1` reaches the target. */
        lerp(target: Vector4, t: number): this;
        /** Replace all components and return this vector for chaining. */
        set(x: number, y: number, z: number, w: number): this;
        /** Create an independent copy. */
        clone(): Vector4;
        /** Format as `Vector4(x, y, z, w)` for logging. */
        toString(): string;
        /** Convert to a plain component object for `JSON.stringify`. */
        toJSON(): { x: number; y: number; z: number; w: number };

        /** Create `Vector4(0, 0, 0, 0)`. */
        static zero(): Vector4;
        /** Create `Vector4(1, 1, 1, 1)`. */
        static one(): Vector4;
    }
}
