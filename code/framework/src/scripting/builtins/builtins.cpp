/*
 * MafiaHub OSS license
 * Copyright (c) 2021-2026, MafiaHub. All rights reserved.
 *
 * This file comes from MafiaHub, hosted at https://github.com/MafiaHub/Framework.
 * See LICENSE file in the source repository for information regarding licensing.
 */

#include "builtins.h"

#include "console.h"
#include "entity.h"
#include "events.h"
#include "execution_environment.h"
#include "exports.h"
#include "imports.h"
#include "messages.h"
#include "player.h"
#include "state_bag.h"
#include "text_label.h"

#include "../bind/call.h"
#include "../bind/callable.h"
#include "../bind/v8_callable.h"
#include "../resource/resource_manager.h"
#include "../value_transfer.h"

#include <exception>

namespace Framework::Scripting::Builtins {
    void RegisterTransferTypes() {
        // Once: registering again would move these after a project's handles, and the newest is tried first.
        static bool registered = false;
        if (registered) {
            return;
        }
        registered = true;

        // The value types, with both transfers: each definition says what its class crosses as.
        Vector2::Definition().RegisterTransfer();
        Vector3::Definition().RegisterTransfer();
        Vector4::Definition().RegisterTransfer();
        Quaternion::Definition().RegisterTransfer();
        Color::Definition().RegisterTransfer();

        // The handles cross by their network id. Bases before the handles derived from them: the transfer tries
        // the newest registration first. Each says so in its definition, which registers it with both transfers.
        Entity::Definition().RegisterTransfer();
        Player::Definition().RegisterTransfer();
        TextLabel::Definition().RegisterTransfer();
        StateBag::Definition().RegisterTransfer();
    }

    void RegisterShared(Bind::Registrar &registrar, Bind::Languages languages, ResourceManager &manager, const std::string &modVersion) {
        // Every builtin is a global (new Vector3 / Vector3.new, not Core.Vector3).
        RegisterValueTypes(registrar, languages);

        manager.GetEvents().Register(registrar, languages, &manager);
        Messages::Register(registrar, languages, &manager);
        Imports::Register(registrar, languages, &manager);
        Exports::Register(registrar, languages, &manager);
        Console::Register(registrar, languages, &manager);
        ExecutionEnvironment::Register(registrar, languages, modVersion);

        // A game publishes these itself in JavaScript.
        const Bind::Languages lua = languages & Bind::Languages::Lua;
        Entity::Register(registrar, lua);
        Player::Register(registrar, lua);
        TextLabel::Register(registrar, lua);
    }

    std::vector<const Bind::ClassDesc *> SharedDefinitions(std::initializer_list<const Bind::ClassDesc *> more) {
        std::vector<const Bind::ClassDesc *> definitions = {
            &Vector2::Definition().Describe(),
            &Vector3::Definition().Describe(),
            &Vector4::Definition().Describe(),
            &Quaternion::Definition().Describe(),
            &Color::Definition().Describe(),
            &Events::Definition().Describe(),
            &Messages::Definition().Describe(),
            &Imports::Definition().Describe(),
            &Exports::Definition().Describe(),
            &Console::Definition().Describe(),
            &ExecutionEnvironment::Definition().Describe(),
            &StateBag::Definition().Describe(),
            &Entity::Definition().Describe(),
            &Player::Definition().Describe(),
            &TextLabel::Definition().Describe(),
        };
        definitions.insert(definitions.end(), more.begin(), more.end());
        return definitions;
    }

    void UnregisterAll(v8::Isolate *isolate) {
        // The functions the host kept of this isolate, and the results it still owes it: for an isolate no
        // disposing listener speaks for, and for what a listener that ran script left behind.
        Bind::V8RuntimeDisposing(isolate);

        // The handlers and the waiting emissions of this isolate, whose handles go with it.
        Events::UnregisterIsolate(isolate);

        // The message handlers, requests and replies of this isolate, when no manager was left to say so: a
        // resource that failed and was left running is not stopped with the others, and its runtime goes with
        // the engine.
        Messages::ForgetIsolate(isolate);

        // The address of the isolate may be the next one's, which is not to answer with this one's manager.
        Bind::SetCallResourceManager(isolate, nullptr);

        Vector2::UnregisterIsolate(isolate);
        Vector3::UnregisterIsolate(isolate);
        Vector4::UnregisterIsolate(isolate);
        Quaternion::UnregisterIsolate(isolate);
        Color::UnregisterIsolate(isolate);
        Entity::UnregisterIsolate(isolate);
        StateBag::UnregisterIsolate(isolate);
        Player::UnregisterIsolate(isolate);
        TextLabel::UnregisterIsolate(isolate);

        // v8pp keeps its own per-isolate state: every class registry, the C++ objects they wrap, and the data of every
        // bound function. Weak callbacks do not run on disposal, so without this a stopped resource leaked all of it.
        v8::Locker locker(isolate);
        v8::Isolate::Scope isolateScope(isolate);
        v8::HandleScope handleScope(isolate);
        v8pp::cleanup(isolate);
    }
} // namespace Framework::Scripting::Builtins
