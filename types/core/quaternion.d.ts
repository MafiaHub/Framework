declare namespace Core {
    class Quaternion {
        constructor(w: number, x: number, y: number, z: number);

        w: number;
        x: number;
        y: number;
        z: number;

        multiply(other: Quaternion): this;
        normalize(): this;
        slerp(target: Quaternion, t: number): this;
        conjugate(): Quaternion;
        inverse(): Quaternion;
        dot(other: Quaternion): number;
        rotateVector(v: Vector3): Vector3;
        /** Euler angles in radians (x=pitch, y=yaw, z=roll). */
        toEuler(): Vector3;
        set(w: number, x: number, y: number, z: number): this;
        clone(): Quaternion;
        toString(): string;
        toJSON(): { w: number; x: number; y: number; z: number };

        static identity(): Quaternion;
        /** From euler angles in radians. */
        static fromEuler(pitch: number, yaw: number, roll: number): Quaternion;
        static fromAxisAngle(axis: Vector3, angle: number): Quaternion;
    }
}
