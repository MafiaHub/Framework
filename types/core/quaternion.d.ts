declare namespace Core {
    /** Mutable quaternion used to compose and apply three-dimensional rotations. */
    class Quaternion {
        /** Create a quaternion in scalar-first `(w, x, y, z)` order. */
        constructor(w: number, x: number, y: number, z: number);

        /** Mutable scalar component. */
        w: number;
        /** Mutable X imaginary component. */
        x: number;
        /** Mutable Y imaginary component. */
        y: number;
        /** Mutable Z imaginary component. */
        z: number;

        /** Compose this rotation with `other`, mutating this quaternion. */
        multiply(other: Quaternion): this;
        /** Normalize this quaternion in place. */
        normalize(): this;
        /** Spherically interpolate toward `target`; `t=0` keeps this rotation and `t=1` reaches it. */
        slerp(target: Quaternion, t: number): this;
        /** Return a new conjugated quaternion without modifying this one. */
        conjugate(): Quaternion;
        /** Return a new inverse rotation without modifying this quaternion. */
        inverse(): Quaternion;
        /** Compute the scalar quaternion dot product. */
        dot(other: Quaternion): number;
        /** Apply this rotation and return a new rotated vector. */
        rotateVector(v: Vector3): Vector3;
        /** Euler angles in radians (x=pitch, y=yaw, z=roll). */
        toEuler(): Vector3;
        /** Replace every component and return this quaternion for chaining. */
        set(w: number, x: number, y: number, z: number): this;
        /** Create an independent copy. */
        clone(): Quaternion;
        /** Format as `Quaternion(w, x, y, z)` for logging. */
        toString(): string;
        /** Convert to a plain component object for `JSON.stringify`. */
        toJSON(): { w: number; x: number; y: number; z: number };

        /** Create the identity rotation `Quaternion(1, 0, 0, 0)`. */
        static identity(): Quaternion;
        /** From euler angles in radians. */
        static fromEuler(pitch: number, yaw: number, roll: number): Quaternion;
        /** Create a rotation of `angle` radians around an axis normalized internally. */
        static fromAxisAngle(axis: Vector3, angle: number): Quaternion;
    }
}
