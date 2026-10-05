/*
 * MafiaHub OSS license
 * Copyright (c) 2021-2026, MafiaHub. All rights reserved.
 *
 * This file comes from MafiaHub, hosted at https://github.com/MafiaHub/Framework.
 * See LICENSE file in the source repository for information regarding licensing.
 */

#include "builtins.h"

#include "entity.h"
#include "player.h"
#include "state_bag.h"
#include "text_label.h"

#include "../value_transfer.h"

#include <cstring>
#include <exception>
#include <type_traits>

namespace Framework::Scripting::Builtins {
    namespace {
        // A value type crosses as its glm value, byte for byte; both sides are this process.
        template <typename T, typename Glm, const Glm &(T::*Get)() const>
        void RegisterValueTransfer(const char *name) {
            static_assert(std::is_trivially_copyable_v<Glm>);
            ValueTransfer::RegisterHostType(
                name,
                [](v8::Isolate *isolate, v8::Local<v8::Object> object, std::vector<uint8_t> &bytes) {
                    T::GetClass(isolate);
                    const auto value = v8pp::class_<T>::unwrap_object(isolate, object);
                    if (value == nullptr) {
                        return false;
                    }
                    const Glm &raw = ((*value).*Get)();
                    bytes.resize(sizeof(Glm));
                    std::memcpy(bytes.data(), &raw, sizeof(Glm));
                    return true;
                },
                [](v8::Isolate *isolate, v8::Local<v8::Context>, const std::vector<uint8_t> &bytes) -> v8::MaybeLocal<v8::Value> {
                    Glm raw {};
                    std::memcpy(&raw, bytes.data(), std::min(bytes.size(), sizeof(Glm)));
                    return T::NewInstance(isolate, raw);
                });
        }

        // An entity handle crosses as its network id and is wrapped again on the other side, as the same class.
        template <typename T>
        void RegisterEntityTransfer(const char *name) {
            ValueTransfer::RegisterHostType(
                name,
                [](v8::Isolate *isolate, v8::Local<v8::Object> object, std::vector<uint8_t> &bytes) {
                    T::GetClass(isolate);
                    const auto handle = v8pp::class_<T>::unwrap_object(isolate, object);
                    if (handle == nullptr) {
                        return false;
                    }
                    const uint64_t id = handle->GetId();
                    bytes.resize(sizeof(id));
                    std::memcpy(bytes.data(), &id, sizeof(id));
                    return true;
                },
                [](v8::Isolate *isolate, v8::Local<v8::Context>, const std::vector<uint8_t> &bytes) -> v8::MaybeLocal<v8::Value> {
                    uint64_t id = 0;
                    std::memcpy(&id, bytes.data(), std::min(bytes.size(), sizeof(id)));
                    T::GetClass(isolate);
                    // A handle constructor throws when its entity is gone by the time the value arrives.
                    try {
                        return v8pp::class_<T>::create_object(isolate, id);
                    }
                    catch (const std::exception &ex) {
                        isolate->ThrowException(v8::Exception::Error(v8::String::NewFromUtf8(isolate, ex.what()).ToLocalChecked()));
                        return {};
                    }
                });
        }
    } // namespace

    void RegisterTransferTypes() {
        // Once: registering again would move these after a project's handles, and the newest is tried first.
        static bool registered = false;
        if (registered) {
            return;
        }
        registered = true;

        RegisterValueTransfer<Vector2, glm::vec2, &Vector2::vec>("Vector2");
        RegisterValueTransfer<Vector3, glm::vec3, &Vector3::vec>("Vector3");
        RegisterValueTransfer<Vector4, glm::vec4, &Vector4::vec>("Vector4");
        RegisterValueTransfer<Quaternion, glm::quat, &Quaternion::quat>("Quaternion");
        RegisterValueTransfer<Color, glm::vec4, &Color::vec>("Color");

        // Bases before the handles derived from them: the transfer tries the newest registration first.
        RegisterEntityTransfer<Entity>("Entity");
        RegisterEntityTransfer<Player>("Player");
        RegisterEntityTransfer<TextLabel>("TextLabel");
        RegisterEntityTransfer<StateBag>("StateBag");
    }

    void UnregisterAll(v8::Isolate *isolate) {
        Vector2::UnregisterIsolate(isolate);
        Vector3::UnregisterIsolate(isolate);
        Vector4::UnregisterIsolate(isolate);
        Quaternion::UnregisterIsolate(isolate);
        Color::UnregisterIsolate(isolate);
        Entity::UnregisterIsolate(isolate);
        StateBag::UnregisterIsolate(isolate);
        Player::UnregisterIsolate(isolate);
        TextLabel::UnregisterIsolate(isolate);
    }
} // namespace Framework::Scripting::Builtins
