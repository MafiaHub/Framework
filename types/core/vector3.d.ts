declare namespace Core {
    class Vector3 {
        constructor(x: number, y: number, z: number);

        x: number;
        y: number;
        z: number;
        readonly length: number;
        readonly lengthSquared: number;

        add(other: Vector3): this;
        sub(other: Vector3): this;
        mul(scalar: number): this;
        div(scalar: number): this;
        normalize(): this;
        dot(other: Vector3): number;
        cross(other: Vector3): this;
        distance(other: Vector3): number;
        lerp(target: Vector3, t: number): this;
        set(x: number, y: number, z: number): this;
        clone(): Vector3;
        toString(): string;
        toJSON(): { x: number; y: number; z: number };

        static zero(): Vector3;
        static one(): Vector3;
        static up(): Vector3;
        static forward(): Vector3;
        static right(): Vector3;
    }
}
