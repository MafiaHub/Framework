declare namespace Core {
    class Vector3 {
        /** Create a vector from its X, Y, and Z components. */
        constructor(x: number, y: number, z: number);

        /** Mutable X component. */
        x: number;
        /** Mutable Y component. */
        y: number;
        /** Mutable Z component. */
        z: number;
        /** Euclidean magnitude of this vector. */
        readonly length: number;
        /** Squared magnitude, avoiding a square-root calculation. */
        readonly lengthSquared: number;

        /** Add another vector component-wise, mutating this vector. */
        add(other: Vector3): this;
        /** Subtract another vector component-wise, mutating this vector. */
        sub(other: Vector3): this;
        /** Multiply every component by a scalar, mutating this vector. */
        mul(scalar: number): this;
        /** Divide every component by a non-zero scalar, mutating this vector. */
        div(scalar: number): this;
        /** Normalize this vector in place; a zero vector remains unchanged. */
        normalize(): this;
        /** Compute the scalar dot product without changing either vector. */
        dot(other: Vector3): number;
        /** Replace this vector with its cross product against `other`. */
        cross(other: Vector3): this;
        /** Compute Euclidean distance to another vector. */
        distance(other: Vector3): number;
        /** Interpolate toward `target`; `t=0` keeps this value and `t=1` reaches the target. */
        lerp(target: Vector3, t: number): this;
        /** Replace all components and return this vector for chaining. */
        set(x: number, y: number, z: number): this;
        /** Create an independent copy. */
        clone(): Vector3;
        /** Format as `Vector3(x, y, z)` for logging. */
        toString(): string;
        /** Convert to a plain component object for `JSON.stringify`. */
        toJSON(): { x: number; y: number; z: number };

        /** Create `Vector3(0, 0, 0)`. */
        static zero(): Vector3;
        /** Create `Vector3(1, 1, 1)`. */
        static one(): Vector3;
        /** Create the framework's positive-Y unit direction. */
        static up(): Vector3;
        /** Create the framework's positive-Z unit direction. */
        static forward(): Vector3;
        /** Create the framework's positive-X unit direction. */
        static right(): Vector3;
    }
}
