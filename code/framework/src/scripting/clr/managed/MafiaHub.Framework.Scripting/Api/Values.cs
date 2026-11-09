/*
 * MafiaHub OSS license
 * Copyright (c) 2021-2026, MafiaHub. All rights reserved.
 *
 * This file comes from MafiaHub, hosted at https://github.com/MafiaHub/Framework.
 * See LICENSE file in the source repository for information regarding licensing.
 */

using System;
using System.Globalization;

namespace MafiaHub.Framework.Scripting
{
    // The value types of the scripting API. They cross to and from the server by value; their maths lives in
    // System.Numerics, which each converts to and from.

    /// <summary>Two-dimensional vector.</summary>
    public readonly record struct Vector2(float X, float Y)
    {
        public static implicit operator System.Numerics.Vector2(Vector2 v) => new System.Numerics.Vector2(v.X, v.Y);
        public static implicit operator Vector2(System.Numerics.Vector2 v) => new Vector2(v.X, v.Y);

        public override string ToString() => string.Create(CultureInfo.InvariantCulture, $"Vector2({X}, {Y})");
    }

    /// <summary>Three-dimensional vector for positions, directions and Euler angles.</summary>
    public readonly record struct Vector3(float X, float Y, float Z)
    {
        public static Vector3 Zero => default;

        public float Length => MathF.Sqrt(X * X + Y * Y + Z * Z);

        public float DistanceTo(Vector3 other) => System.Numerics.Vector3.Distance(this, other);

        public static Vector3 operator +(Vector3 a, Vector3 b) => new Vector3(a.X + b.X, a.Y + b.Y, a.Z + b.Z);
        public static Vector3 operator -(Vector3 a, Vector3 b) => new Vector3(a.X - b.X, a.Y - b.Y, a.Z - b.Z);
        public static Vector3 operator *(Vector3 a, float s) => new Vector3(a.X * s, a.Y * s, a.Z * s);

        public static implicit operator System.Numerics.Vector3(Vector3 v) => new System.Numerics.Vector3(v.X, v.Y, v.Z);
        public static implicit operator Vector3(System.Numerics.Vector3 v) => new Vector3(v.X, v.Y, v.Z);

        public override string ToString() => string.Create(CultureInfo.InvariantCulture, $"Vector3({X}, {Y}, {Z})");
    }

    /// <summary>Four-dimensional vector.</summary>
    public readonly record struct Vector4(float X, float Y, float Z, float W)
    {
        public static implicit operator System.Numerics.Vector4(Vector4 v) => new System.Numerics.Vector4(v.X, v.Y, v.Z, v.W);
        public static implicit operator Vector4(System.Numerics.Vector4 v) => new Vector4(v.X, v.Y, v.Z, v.W);

        public override string ToString() => string.Create(CultureInfo.InvariantCulture, $"Vector4({X}, {Y}, {Z}, {W})");
    }

    /// <summary>Rotation quaternion. Components are scalar-first (W, X, Y, Z), as in the JavaScript API.</summary>
    public readonly record struct Quaternion(float W, float X, float Y, float Z)
    {
        public static Quaternion Identity => new Quaternion(1, 0, 0, 0);

        public static implicit operator System.Numerics.Quaternion(Quaternion q) => new System.Numerics.Quaternion(q.X, q.Y, q.Z, q.W);
        public static implicit operator Quaternion(System.Numerics.Quaternion q) => new Quaternion(q.W, q.X, q.Y, q.Z);

        public override string ToString() => string.Create(CultureInfo.InvariantCulture, $"Quaternion({W}, {X}, {Y}, {Z})");
    }

    /// <summary>RGBA color with components in [0, 1].</summary>
    public readonly record struct Color(float R, float G, float B, float A = 1f)
    {
        /// <summary>A color from byte components (0-255).</summary>
        public static Color FromRgb(byte r, byte g, byte b, byte a = 255) => new Color(r / 255f, g / 255f, b / 255f, a / 255f);

        public override string ToString() => string.Create(CultureInfo.InvariantCulture, $"Color({R}, {G}, {B}, {A})");
    }

    public partial class Entity : IEquatable<Entity>
    {
        public bool Equals(Entity? other) => other is not null && other.Id == Id;
        public override bool Equals(object? obj) => obj is Entity other && Equals(other);
        public override int GetHashCode() => Id.GetHashCode();
    }

    internal static partial class Handles
    {
        /// <summary>The handle class registered under <paramref name="typeName"/>, or the nearest the API knows.</summary>
        internal static Entity Wrap(string typeName, ulong id) => Create(typeName, id) as Entity ?? new Entity(id);

        internal static T Wrap<T>(ulong id) where T : Entity => (T)Wrap(typeof(T).Name, id);
    }
}
