/*
 * MafiaHub OSS license
 * Copyright (c) 2021-2026, MafiaHub. All rights reserved.
 *
 * This file comes from MafiaHub, hosted at https://github.com/MafiaHub/Framework.
 * See LICENSE file in the source repository for information regarding licensing.
 */

using System;
using System.Runtime.InteropServices;
using System.Text;

namespace MafiaHub.Framework.Scripting.Interop
{
    /// <summary>The type a <see cref="NativeValue"/> holds. Mirrors Api::NativeType (scripting/api/native_abi.h).</summary>
    public enum NativeType : uint
    {
        Void = 0,
        Bool = 1,
        Int32 = 2,
        UInt32 = 3,
        Int64 = 4,
        UInt64 = 5,
        Float = 6,
        Double = 7,
        String = 8,
        Vector2 = 9,
        Vector3 = 10,
        Vector4 = 11,
        Quaternion = 12,
        Color = 13,
        Handle = 14,
    }

    /// <summary>
    /// One argument or result of a binding call, laid out as Api::NativeValue (scripting/api/native_abi.h).
    /// Generated bindings build and read these; scripts never need to.
    /// </summary>
    [StructLayout(LayoutKind.Explicit, Size = 24)]
    public unsafe struct NativeValue
    {
        [FieldOffset(0)] public NativeType Type;
        [FieldOffset(4)] public uint Length;
        [FieldOffset(8)] public long I64;
        [FieldOffset(8)] public ulong U64;
        [FieldOffset(8)] public double F64;
        [FieldOffset(8)] public float F0;
        [FieldOffset(12)] public float F1;
        [FieldOffset(16)] public float F2;
        [FieldOffset(20)] public float F3;
        [FieldOffset(8)] public byte* Text;

        public static NativeValue From(bool value) => new NativeValue { Type = NativeType.Bool, U64 = value ? 1UL : 0UL };
        public static NativeValue From(int value) => new NativeValue { Type = NativeType.Int32, I64 = value };
        public static NativeValue From(uint value) => new NativeValue { Type = NativeType.UInt32, U64 = value };
        public static NativeValue From(long value) => new NativeValue { Type = NativeType.Int64, I64 = value };
        public static NativeValue From(ulong value) => new NativeValue { Type = NativeType.UInt64, U64 = value };
        public static NativeValue From(float value) => new NativeValue { Type = NativeType.Float, F64 = value };
        public static NativeValue From(double value) => new NativeValue { Type = NativeType.Double, F64 = value };
        public static NativeValue From(Vector2 value) => new NativeValue { Type = NativeType.Vector2, F0 = value.X, F1 = value.Y };
        public static NativeValue From(Vector3 value) => new NativeValue { Type = NativeType.Vector3, F0 = value.X, F1 = value.Y, F2 = value.Z };
        public static NativeValue From(Vector4 value) => new NativeValue { Type = NativeType.Vector4, F0 = value.X, F1 = value.Y, F2 = value.Z, F3 = value.W };
        public static NativeValue From(Quaternion value) => new NativeValue { Type = NativeType.Quaternion, F0 = value.W, F1 = value.X, F2 = value.Y, F3 = value.Z };
        public static NativeValue From(Color value) => new NativeValue { Type = NativeType.Color, F0 = value.R, F1 = value.G, F2 = value.B, F3 = value.A };

        public static NativeValue From(Entity? value)
        {
            if (value is null)
            {
                throw new ArgumentNullException(nameof(value));
            }
            return new NativeValue { Type = NativeType.Handle, U64 = value.Id };
        }

        // A stale receiver answers with Void: every reader returns its type's default for it.
        public bool AsBool() => Type == NativeType.Bool && U64 != 0;
        public int AsInt32() => Type == NativeType.Int32 ? (int)I64 : 0;
        public uint AsUInt32() => Type == NativeType.UInt32 ? (uint)U64 : 0;
        public long AsInt64() => Type == NativeType.Int64 ? I64 : 0;
        public ulong AsUInt64() => Type == NativeType.UInt64 ? U64 : 0;
        public float AsFloat() => Type == NativeType.Float ? (float)F64 : 0;
        public double AsDouble() => Type == NativeType.Double ? F64 : 0;
        public string AsString() => Type == NativeType.String && Length > 0 ? Encoding.UTF8.GetString(Text, (int)Length) : string.Empty;
        public Vector2 AsVector2() => Type == NativeType.Vector2 ? new Vector2(F0, F1) : default;
        public Vector3 AsVector3() => Type == NativeType.Vector3 ? new Vector3(F0, F1, F2) : default;
        public Vector4 AsVector4() => Type == NativeType.Vector4 ? new Vector4(F0, F1, F2, F3) : default;
        public Quaternion AsQuaternion() => Type == NativeType.Quaternion ? new Quaternion(F0, F1, F2, F3) : Quaternion.Identity;
        public Color AsColor() => Type == NativeType.Color ? new Color(F0, F1, F2, F3) : default;
        public ulong AsHandle() => Type == NativeType.Handle ? U64 : 0;
    }

    /// <summary>A UTF-8 copy of a string argument, freed when the call is done.</summary>
    public unsafe struct NativeString : IDisposable
    {
        private IntPtr _buffer;

        public static NativeString Create(string? value, out NativeValue argument)
        {
            value ??= string.Empty;
            int length = Encoding.UTF8.GetByteCount(value);
            IntPtr buffer = Marshal.AllocHGlobal(Math.Max(length, 1));
            fixed (char* chars = value)
            {
                Encoding.UTF8.GetBytes(chars, value.Length, (byte*)buffer, length);
            }
            argument = new NativeValue { Type = NativeType.String, Length = (uint)length, Text = (byte*)buffer };
            return new NativeString { _buffer = buffer };
        }

        public void Dispose()
        {
            if (_buffer != IntPtr.Zero)
            {
                Marshal.FreeHGlobal(_buffer);
                _buffer = IntPtr.Zero;
            }
        }
    }
}
