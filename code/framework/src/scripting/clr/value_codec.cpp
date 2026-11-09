/*
 * MafiaHub OSS license
 * Copyright (c) 2021-2026, MafiaHub. All rights reserved.
 *
 * This file comes from MafiaHub, hosted at https://github.com/MafiaHub/Framework.
 * See LICENSE file in the source repository for information regarding licensing.
 */

#include "value_codec.h"

#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>

#include <algorithm>
#include <cstring>
#include <limits>
#include <string_view>

namespace Framework::Scripting::Clr {
    namespace {
        enum Tag : uint8_t {
            kNull       = 0,
            kFalse      = 1,
            kTrue       = 2,
            kNumber     = 3,
            kString     = 4,
            kArray      = 5,
            kObject     = 6,
            kVector2    = 7,
            kVector3    = 8,
            kVector4    = 9,
            kQuaternion = 10,
            kColor      = 11,
            kHandle     = 12,
            kBytes      = 13,
        };

        using EncodeResult = Utils::Result<void, std::string>;

        template <typename T>
        void Put(std::vector<uint8_t> &out, T value) {
            const auto *raw = reinterpret_cast<const uint8_t *>(&value);
            out.insert(out.end(), raw, raw + sizeof(T));
        }

        void PutText(std::vector<uint8_t> &out, std::string_view text) {
            Put<uint32_t>(out, static_cast<uint32_t>(text.size()));
            out.insert(out.end(), text.begin(), text.end());
        }

        void PutFloats(std::vector<uint8_t> &out, Tag tag, std::initializer_list<float> values) {
            out.push_back(tag);
            for (const float value : values) {
                Put<float>(out, value);
            }
        }

        template <typename Glm>
        Glm ReadGlm(const std::vector<uint8_t> &bytes) {
            Glm value {};
            std::memcpy(&value, bytes.data(), std::min(bytes.size(), sizeof(Glm)));
            return value;
        }

        template <typename Glm>
        std::vector<uint8_t> WriteGlm(const Glm &value) {
            std::vector<uint8_t> bytes(sizeof(Glm));
            std::memcpy(bytes.data(), &value, sizeof(Glm));
            return bytes;
        }

        EncodeResult Fail(const std::string &where, const std::string &what) {
            return EncodeResult::Err((where.empty() ? "value" : where) + ": " + what);
        }

        EncodeResult EncodeAt(const TransferredValue &value, std::vector<uint8_t> &out, const std::string &where, size_t depth) {
            using Kind = TransferredValue::Kind;
            if (depth > ValueCodec::kMaxDepth) {
                return Fail(where, "nested too deeply");
            }

            switch (value.kind) {
            case Kind::Undefined:
            case Kind::Null: out.push_back(kNull); return EncodeResult::Ok();
            case Kind::Boolean: out.push_back(value.boolean ? kTrue : kFalse); return EncodeResult::Ok();
            case Kind::Number:
            case Kind::Date:
                out.push_back(kNumber);
                Put<double>(out, value.number);
                return EncodeResult::Ok();
            case Kind::String:
            case Kind::RegExp:
                out.push_back(kString);
                PutText(out, value.text);
                return EncodeResult::Ok();
            case Kind::Array:
            case Kind::Set: {
                out.push_back(kArray);
                Put<uint32_t>(out, static_cast<uint32_t>(value.items.size()));
                for (size_t i = 0; i < value.items.size(); ++i) {
                    if (auto result = EncodeAt(value.items[i], out, where + "[" + std::to_string(i) + "]", depth + 1); !result) {
                        return result;
                    }
                }
                return EncodeResult::Ok();
            }
            case Kind::Object:
            case Kind::Error: {
                out.push_back(kObject);
                Put<uint32_t>(out, static_cast<uint32_t>(value.properties.size()));
                for (const auto &[key, property] : value.properties) {
                    PutText(out, key);
                    if (auto result = EncodeAt(property, out, where.empty() ? key : where + "." + key, depth + 1); !result) {
                        return result;
                    }
                }
                return EncodeResult::Ok();
            }
            case Kind::Map: {
                for (size_t i = 0; i < value.items.size(); i += 2) {
                    if (value.items[i].kind != Kind::String) {
                        return Fail(where, "a Map crosses into C# only with string keys");
                    }
                }
                out.push_back(kObject);
                Put<uint32_t>(out, static_cast<uint32_t>(value.items.size() / 2));
                for (size_t i = 0; i + 1 < value.items.size(); i += 2) {
                    const std::string &key = value.items[i].text;
                    PutText(out, key);
                    if (auto result = EncodeAt(value.items[i + 1], out, where + ".get(" + key + ")", depth + 1); !result) {
                        return result;
                    }
                }
                return EncodeResult::Ok();
            }
            case Kind::ArrayBuffer:
            case Kind::TypedArray:
                out.push_back(kBytes);
                Put<uint32_t>(out, static_cast<uint32_t>(value.bytes.size()));
                out.insert(out.end(), value.bytes.begin(), value.bytes.end());
                return EncodeResult::Ok();
            case Kind::HostObject: {
                const std::string &type = value.text;
                if (type == "Vector2") {
                    const auto v = ReadGlm<glm::vec2>(value.bytes);
                    PutFloats(out, kVector2, {v.x, v.y});
                }
                else if (type == "Vector3") {
                    const auto v = ReadGlm<glm::vec3>(value.bytes);
                    PutFloats(out, kVector3, {v.x, v.y, v.z});
                }
                else if (type == "Vector4") {
                    const auto v = ReadGlm<glm::vec4>(value.bytes);
                    PutFloats(out, kVector4, {v.x, v.y, v.z, v.w});
                }
                else if (type == "Quaternion") {
                    const auto q = ReadGlm<glm::quat>(value.bytes);
                    PutFloats(out, kQuaternion, {q.w, q.x, q.y, q.z});
                }
                else if (type == "Color") {
                    const auto c = ReadGlm<glm::vec4>(value.bytes);
                    PutFloats(out, kColor, {c.r, c.g, c.b, c.a});
                }
                else if (value.bytes.size() == sizeof(uint64_t)) {
                    // Every handle type carries its network id and nothing else.
                    out.push_back(kHandle);
                    PutText(out, type);
                    Put<uint64_t>(out, ReadGlm<uint64_t>(value.bytes));
                }
                else {
                    return Fail(where, "a " + type + " cannot cross into C#");
                }
                return EncodeResult::Ok();
            }
            case Kind::BigInt: return Fail(where, "a BigInt cannot cross into C#; send it as a string or a number");
            case Kind::Function: return Fail(where, "a function cannot cross into C#; register it as an export instead");
            }
            return Fail(where, "unknown value kind");
        }

        class Reader final {
          public:
            Reader(const uint8_t *data, size_t length): _data(data), _length(length) {}

            bool AtEnd() const {
                return _offset == _length;
            }

            Utils::Result<TransferredValue, std::string> Value(size_t depth) {
                using Result = Utils::Result<TransferredValue, std::string>;
                using Kind   = TransferredValue::Kind;
                if (depth > ValueCodec::kMaxDepth) {
                    return Result::Err("value nested too deeply");
                }
                uint8_t tag = 0;
                if (!Read(tag)) {
                    return Truncated();
                }

                TransferredValue value;
                switch (tag) {
                case kNull: value.kind = Kind::Null; return Result::Ok(std::move(value));
                case kFalse:
                case kTrue:
                    value.kind    = Kind::Boolean;
                    value.boolean = tag == kTrue;
                    return Result::Ok(std::move(value));
                case kNumber:
                    value.kind = Kind::Number;
                    if (!Read(value.number)) {
                        return Truncated();
                    }
                    return Result::Ok(std::move(value));
                case kString:
                    value.kind = Kind::String;
                    if (!Text(value.text)) {
                        return Truncated();
                    }
                    return Result::Ok(std::move(value));
                case kArray: {
                    value.kind     = Kind::Array;
                    uint32_t count = 0;
                    if (!Read(count) || count > Remaining()) {
                        return Truncated();
                    }
                    value.items.reserve(count);
                    for (uint32_t i = 0; i < count; ++i) {
                        auto item = Value(depth + 1);
                        if (!item) {
                            return item;
                        }
                        value.items.push_back(std::move(item).GetValue());
                    }
                    return Result::Ok(std::move(value));
                }
                case kObject: {
                    value.kind     = Kind::Object;
                    uint32_t count = 0;
                    if (!Read(count) || count > Remaining()) {
                        return Truncated();
                    }
                    value.properties.reserve(count);
                    for (uint32_t i = 0; i < count; ++i) {
                        std::string key;
                        if (!Text(key)) {
                            return Truncated();
                        }
                        auto property = Value(depth + 1);
                        if (!property) {
                            return property;
                        }
                        value.properties.emplace_back(std::move(key), std::move(property).GetValue());
                    }
                    return Result::Ok(std::move(value));
                }
                case kVector2:
                    return Floats<glm::vec2, 2>("Vector2", [](const float *f) {
                        return glm::vec2(f[0], f[1]);
                    });
                case kVector3:
                    return Floats<glm::vec3, 3>("Vector3", [](const float *f) {
                        return glm::vec3(f[0], f[1], f[2]);
                    });
                case kVector4:
                    return Floats<glm::vec4, 4>("Vector4", [](const float *f) {
                        return glm::vec4(f[0], f[1], f[2], f[3]);
                    });
                case kQuaternion:
                    return Floats<glm::quat, 4>("Quaternion", [](const float *f) {
                        return glm::quat(f[0], f[1], f[2], f[3]);
                    });
                case kColor:
                    return Floats<glm::vec4, 4>("Color", [](const float *f) {
                        return glm::vec4(f[0], f[1], f[2], f[3]);
                    });
                case kHandle: {
                    value.kind  = Kind::HostObject;
                    uint64_t id = 0;
                    if (!Text(value.text) || !Read(id)) {
                        return Truncated();
                    }
                    value.bytes = WriteGlm(id);
                    return Result::Ok(std::move(value));
                }
                case kBytes: {
                    value.kind      = Kind::ArrayBuffer;
                    uint32_t length = 0;
                    if (!Read(length) || length > Remaining()) {
                        return Truncated();
                    }
                    value.bytes.assign(_data + _offset, _data + _offset + length);
                    _offset += length;
                    return Result::Ok(std::move(value));
                }
                default: return Result::Err("unknown value tag " + std::to_string(tag));
                }
            }

          private:
            static Utils::Result<TransferredValue, std::string> Truncated() {
                return Utils::Result<TransferredValue, std::string>::Err("value data ends early");
            }

            size_t Remaining() const {
                return _length - _offset;
            }

            template <typename T>
            bool Read(T &out) {
                if (Remaining() < sizeof(T)) {
                    return false;
                }
                std::memcpy(&out, _data + _offset, sizeof(T));
                _offset += sizeof(T);
                return true;
            }

            bool Text(std::string &out) {
                uint32_t length = 0;
                if (!Read(length) || length > Remaining()) {
                    return false;
                }
                out.assign(reinterpret_cast<const char *>(_data + _offset), length);
                _offset += length;
                return true;
            }

            template <typename Glm, size_t N, typename Make>
            Utils::Result<TransferredValue, std::string> Floats(const char *type, Make make) {
                float values[N] {};
                for (size_t i = 0; i < N; ++i) {
                    if (!Read(values[i])) {
                        return Truncated();
                    }
                }
                TransferredValue value;
                value.kind  = TransferredValue::Kind::HostObject;
                value.text  = type;
                value.bytes = WriteGlm<Glm>(make(values));
                return Utils::Result<TransferredValue, std::string>::Ok(std::move(value));
            }

            const uint8_t *_data;
            size_t _length;
            size_t _offset = 0;
        };
    } // namespace

    Utils::Result<void, std::string> ValueCodec::Encode(const TransferredValue &value, std::vector<uint8_t> &out) {
        return EncodeAt(value, out, "", 0);
    }

    Utils::Result<std::vector<uint8_t>, std::string> ValueCodec::EncodeList(const std::vector<TransferredValue> &values) {
        using Result = Utils::Result<std::vector<uint8_t>, std::string>;
        std::vector<uint8_t> out;
        out.push_back(kArray);
        Put<uint32_t>(out, static_cast<uint32_t>(values.size()));
        for (size_t i = 0; i < values.size(); ++i) {
            if (auto result = EncodeAt(values[i], out, "argument " + std::to_string(i + 1), 1); !result) {
                return Result::Err(result.GetError());
            }
        }
        return Result::Ok(std::move(out));
    }

    Utils::Result<TransferredValue, std::string> ValueCodec::Decode(const uint8_t *data, size_t length) {
        using Result = Utils::Result<TransferredValue, std::string>;
        if (data == nullptr || length == 0) {
            return Result::Err("no value data");
        }
        Reader reader(data, length);
        auto value = reader.Value(0);
        if (value && !reader.AtEnd()) {
            return Result::Err("value data has trailing bytes");
        }
        return value;
    }

    Utils::Result<std::vector<TransferredValue>, std::string> ValueCodec::DecodeList(const uint8_t *data, size_t length) {
        using Result = Utils::Result<std::vector<TransferredValue>, std::string>;
        auto value   = Decode(data, length);
        if (!value) {
            return Result::Err(value.GetError());
        }
        if (value.GetValue().kind != TransferredValue::Kind::Array) {
            return Result::Err("expected a list of values");
        }
        return Result::Ok(std::move(value).GetValue().items);
    }
} // namespace Framework::Scripting::Clr
