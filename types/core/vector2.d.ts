declare namespace Core {
    class Vector2 {
        constructor(x: number, y: number);

        x: number;
        y: number;
        readonly length: number;
        readonly lengthSquared: number;

        add(other: Vector2): this;
        sub(other: Vector2): this;
        mul(scalar: number): this;
        div(scalar: number): this;
        normalize(): this;
        dot(other: Vector2): number;
        distance(other: Vector2): number;
        lerp(target: Vector2, t: number): this;
        set(x: number, y: number): this;
        clone(): Vector2;
        toString(): string;
        toJSON(): { x: number; y: number };

        static zero(): Vector2;
        static one(): Vector2;
    }
}
