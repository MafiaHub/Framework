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

#include "../bind/call.h"
#include "../bind/v8_callable.h"
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
        // the newest registration first. Entity and StateBag say so in their definitions, which registers them
        // with both transfers.
        Entity::Definition().RegisterTransfer();
        RegisterHandleTransfer<Player>("Player");
        RegisterHandleTransfer<TextLabel>("TextLabel");
        StateBag::Definition().RegisterTransfer();
    }

    void UnregisterAll(v8::Isolate *isolate) {
        // The functions the host kept of this isolate, and the results it still owes it: for an isolate no
        // disposing listener speaks for, and for what a listener that ran script left behind.
        Bind::V8RuntimeDisposing(isolate);

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
