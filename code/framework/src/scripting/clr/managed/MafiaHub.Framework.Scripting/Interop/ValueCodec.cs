/*
 * MafiaHub OSS license
 * Copyright (c) 2021-2026, MafiaHub. All rights reserved.
 *
 * This file comes from MafiaHub, hosted at https://github.com/MafiaHub/Framework.
 * See LICENSE file in the source repository for information regarding licensing.
 */

using System;
using System.Buffers.Binary;
using System.Collections;
using System.Collections.Generic;
using System.IO;
using System.Text;
using MafiaHub.Framework.Scripting.Runtime;

namespace MafiaHub.Framework.Scripting.Interop
{
    /// <summary>
    /// Values exchanged with JavaScript resources. Mirrors Clr::ValueCodec (scripting/clr/value_codec.h), which
    /// documents the format; change the two together.
    /// </summary>
    /// <remarks>
    /// What arrives: null, <see cref="bool"/>, <see cref="double"/>, <see cref="string"/>, <c>object?[]</c>,
    /// <c>Dictionary&lt;string, object?&gt;</c>, the value structs, entity handles and <c>byte[]</c>.
    /// What can be sent: those, any other number type, any <see cref="IDictionary"/> with string keys, any other
    /// <see cref="IEnumerable"/>, a delegate and a <see cref="ScriptFunction"/>. Functions arrive as
    /// <see cref="ScriptFunction"/>, or as the delegate itself when it was C#'s to begin with.
    /// </remarks>
    internal static class ValueCodec
    {
        private const int MaxDepth = 64;

        private const byte Null = 0, False = 1, True = 2, Number = 3, String = 4, Array = 5, Object = 6;
        private const byte Vec2 = 7, Vec3 = 8, Vec4 = 9, Quat = 10, Rgba = 11, Handle = 12, Bytes = 13;
        private const byte ScriptFn = 14, ManagedFn = 15;

        // `owner` is the resource a delegate among the values belongs to; by default, the one running now.
        public static byte[] EncodeList(IReadOnlyList<object?> values, ResourceContext? owner = null)
        {
            using var stream = new MemoryStream();
            using var writer = new BinaryWriter(stream, Encoding.UTF8);
            writer.Write(Array);
            writer.Write((uint)values.Count);
            for (int i = 0; i < values.Count; ++i)
            {
                Write(writer, values[i], $"argument {i + 1}", 1, owner);
            }
            writer.Flush();
            return stream.ToArray();
        }

        public static byte[] Encode(object? value, ResourceContext? owner = null)
        {
            using var stream = new MemoryStream();
            using var writer = new BinaryWriter(stream, Encoding.UTF8);
            Write(writer, value, "value", 0, owner);
            writer.Flush();
            return stream.ToArray();
        }

        public static object?[] DecodeList(ReadOnlySpan<byte> data)
        {
            object? value = Decode(data);
            return value as object?[] ?? throw new InvalidDataException("expected a list of values");
        }

        public static object? Decode(ReadOnlySpan<byte> data)
        {
            int offset = 0;
            object? value = Read(data, ref offset, 0);
            if (offset != data.Length)
            {
                throw new InvalidDataException("value data has trailing bytes");
            }
            return value;
        }

        private static void WriteText(BinaryWriter writer, string text)
        {
            byte[] bytes = Encoding.UTF8.GetBytes(text);
            writer.Write((uint)bytes.Length);
            writer.Write(bytes);
        }

        private static void Write(BinaryWriter writer, object? value, string where, int depth, ResourceContext? owner)
        {
            if (depth > MaxDepth)
            {
                throw new ArgumentException($"{where}: nested too deeply");
            }

            switch (value)
            {
                case null:
                    writer.Write(Null);
                    return;
                case bool b:
                    writer.Write(b ? True : False);
                    return;
                case string s:
                    writer.Write(String);
                    WriteText(writer, s);
                    return;
                case byte or sbyte or short or ushort or int or uint or long or ulong or float or double or decimal:
                    writer.Write(Number);
                    writer.Write(Convert.ToDouble(value, System.Globalization.CultureInfo.InvariantCulture));
                    return;
                case Enum e:
                    writer.Write(Number);
                    writer.Write(Convert.ToDouble(e, System.Globalization.CultureInfo.InvariantCulture));
                    return;
                case Vector2 v2:
                    writer.Write(Vec2);
                    writer.Write(v2.X);
                    writer.Write(v2.Y);
                    return;
                case Vector3 v3:
                    writer.Write(Vec3);
                    writer.Write(v3.X);
                    writer.Write(v3.Y);
                    writer.Write(v3.Z);
                    return;
                case Vector4 v4:
                    writer.Write(Vec4);
                    writer.Write(v4.X);
                    writer.Write(v4.Y);
                    writer.Write(v4.Z);
                    writer.Write(v4.W);
                    return;
                case Quaternion q:
                    writer.Write(Quat);
                    writer.Write(q.W);
                    writer.Write(q.X);
                    writer.Write(q.Y);
                    writer.Write(q.Z);
                    return;
                case Color c:
                    writer.Write(Rgba);
                    writer.Write(c.R);
                    writer.Write(c.G);
                    writer.Write(c.B);
                    writer.Write(c.A);
                    return;
                case Entity entity:
                    writer.Write(Handle);
                    WriteText(writer, entity.GetType().Name);
                    writer.Write(entity.Id);
                    return;
                case ScriptFunction function:
                    if (!function.IsCurrent)
                    {
                        throw new ArgumentException($"{where}: this JavaScript function belongs to a C# scripting run that has stopped");
                    }
                    writer.Write(ScriptFn);
                    writer.Write(function.Handle);
                    return;
                case Delegate function:
                {
                    ResourceContext resource = owner ?? ScriptHost.Current ?? throw new ArgumentException($"{where}: a function can only be sent from a resource's script");
                    writer.Write(ManagedFn);
                    writer.Write(DelegateTable.Register(function, resource));
                    return;
                }
                case byte[] bytes:
                    writer.Write(Bytes);
                    writer.Write((uint)bytes.Length);
                    writer.Write(bytes);
                    return;
                case IDictionary dictionary:
                {
                    writer.Write(Object);
                    writer.Write((uint)dictionary.Count);
                    foreach (DictionaryEntry entry in dictionary)
                    {
                        if (entry.Key is not string key)
                        {
                            throw new ArgumentException($"{where}: only dictionaries with string keys can be sent");
                        }
                        WriteText(writer, key);
                        Write(writer, entry.Value, $"{where}.{key}", depth + 1, owner);
                    }
                    return;
                }
                case IEnumerable sequence:
                {
                    var items = new List<object?>();
                    foreach (object? item in sequence)
                    {
                        items.Add(item);
                    }
                    writer.Write(Array);
                    writer.Write((uint)items.Count);
                    for (int i = 0; i < items.Count; ++i)
                    {
                        Write(writer, items[i], $"{where}[{i}]", depth + 1, owner);
                    }
                    return;
                }
                default:
                    throw new ArgumentException($"{where}: a {value.GetType().Name} cannot be sent to another resource; send a dictionary, a list or a value");
            }
        }

        private static ReadOnlySpan<byte> Take(ReadOnlySpan<byte> data, ref int offset, int count)
        {
            if (count < 0 || offset + count > data.Length)
            {
                throw new InvalidDataException("value data ends early");
            }
            ReadOnlySpan<byte> slice = data.Slice(offset, count);
            offset += count;
            return slice;
        }

        private static uint ReadUInt32(ReadOnlySpan<byte> data, ref int offset) => BinaryPrimitives.ReadUInt32LittleEndian(Take(data, ref offset, 4));
        private static float ReadFloat(ReadOnlySpan<byte> data, ref int offset) => BinaryPrimitives.ReadSingleLittleEndian(Take(data, ref offset, 4));

        private static string ReadText(ReadOnlySpan<byte> data, ref int offset)
        {
            int length = checked((int)ReadUInt32(data, ref offset));
            return Encoding.UTF8.GetString(Take(data, ref offset, length));
        }

        private static object? Read(ReadOnlySpan<byte> data, ref int offset, int depth)
        {
            if (depth > MaxDepth)
            {
                throw new InvalidDataException("value nested too deeply");
            }
            byte tag = Take(data, ref offset, 1)[0];
            switch (tag)
            {
                case Null:
                    return null;
                case False:
                    return false;
                case True:
                    return true;
                case Number:
                    return BinaryPrimitives.ReadDoubleLittleEndian(Take(data, ref offset, 8));
                case String:
                    return ReadText(data, ref offset);
                case Array:
                {
                    int count = checked((int)ReadUInt32(data, ref offset));
                    if (count > data.Length - offset)
                    {
                        throw new InvalidDataException("value data ends early");
                    }
                    var items = new object?[count];
                    for (int i = 0; i < count; ++i)
                    {
                        items[i] = Read(data, ref offset, depth + 1);
                    }
                    return items;
                }
                case Object:
                {
                    int count = checked((int)ReadUInt32(data, ref offset));
                    if (count > data.Length - offset)
                    {
                        throw new InvalidDataException("value data ends early");
                    }
                    var properties = new Dictionary<string, object?>(count);
                    for (int i = 0; i < count; ++i)
                    {
                        string key = ReadText(data, ref offset);
                        properties[key] = Read(data, ref offset, depth + 1);
                    }
                    return properties;
                }
                case Vec2:
                    return new Vector2(ReadFloat(data, ref offset), ReadFloat(data, ref offset));
                case Vec3:
                    return new Vector3(ReadFloat(data, ref offset), ReadFloat(data, ref offset), ReadFloat(data, ref offset));
                case Vec4:
                    return new Vector4(ReadFloat(data, ref offset), ReadFloat(data, ref offset), ReadFloat(data, ref offset), ReadFloat(data, ref offset));
                case Quat:
                    return new Quaternion(ReadFloat(data, ref offset), ReadFloat(data, ref offset), ReadFloat(data, ref offset), ReadFloat(data, ref offset));
                case Rgba:
                    return new Color(ReadFloat(data, ref offset), ReadFloat(data, ref offset), ReadFloat(data, ref offset), ReadFloat(data, ref offset));
                case Handle:
                {
                    string type = ReadText(data, ref offset);
                    ulong id = BinaryPrimitives.ReadUInt64LittleEndian(Take(data, ref offset, 8));
                    return Handles.Wrap(type, id);
                }
                case Bytes:
                {
                    int length = checked((int)ReadUInt32(data, ref offset));
                    return Take(data, ref offset, length).ToArray();
                }
                case ScriptFn:
                    return new ScriptFunction(BinaryPrimitives.ReadUInt64LittleEndian(Take(data, ref offset, 8)));
                case ManagedFn:
                    return DelegateTable.Get(BinaryPrimitives.ReadUInt64LittleEndian(Take(data, ref offset, 8))).Function;
                default:
                    throw new InvalidDataException($"unknown value tag {tag}");
            }
        }
    }
}
