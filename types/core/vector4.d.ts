declare namespace Core {
    class Vector4 {
        constructor(x: number, y: number, z: number, w: number);

        x: number;
        y: number;
        z: number;
        w: number;
        readonly length: number;
        readonly lengthSquared: number;

        add(other: Vector4): this;
        sub(other: Vector4): this;
        mul(scalar: number): this;
        div(scalar: number): this;
        normalize(): this;
        dot(other: Vector4): number;
        distance(other: Vector4): number;
        lerp(target: Vector4, t: number): this;
        set(x: number, y: number, z: number, w: number): this;
        clone(): Vector4;
        toString(): string;
        toJSON(): { x: number; y: number; z: number; w: number };

        static zero(): Vector4;
        static one(): Vector4;
    }
}
