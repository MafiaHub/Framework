declare namespace Core {
    class Vector2 {
        /** Create a vector from its X and Y components. */
        constructor(x: number, y: number);

        /** Mutable X component. */
        x: number;
        /** Mutable Y component. */
        y: number;
        /** Euclidean magnitude of this vector. */
        readonly length: number;
        /** Squared magnitude, avoiding a square-root calculation. */
        readonly lengthSquared: number;

        /** Add another vector component-wise, mutating this vector. */
        add(other: Vector2): this;
        /** Subtract another vector component-wise, mutating this vector. */
        sub(other: Vector2): this;
        /** Multiply both components by a scalar, mutating this vector. */
        mul(scalar: number): this;
        /** Divide both components by a non-zero scalar, mutating this vector. */
        div(scalar: number): this;
        /** Normalize this vector in place; a zero vector remains unchanged. */
        normalize(): this;
        /** Compute the scalar dot product without changing either vector. */
        dot(other: Vector2): number;
        /** Compute Euclidean distance to another vector. */
        distance(other: Vector2): number;
        /** Interpolate toward `target`; `t=0` keeps this value and `t=1` reaches the target. */
        lerp(target: Vector2, t: number): this;
        /** Replace both components and return this vector for chaining. */
        set(x: number, y: number): this;
        /** Create an independent copy. */
        clone(): Vector2;
        /** Format as `Vector2(x, y)` for logging. */
        toString(): string;
        /** Convert to a plain component object for `JSON.stringify`. */
        toJSON(): { x: number; y: number };

        /** Create `Vector2(0, 0)`. */
        static zero(): Vector2;
        /** Create `Vector2(1, 1)`. */
        static one(): Vector2;
    }
}
