/*
 * MafiaHub OSS license
 * Copyright (c) 2026, MafiaHub. All rights reserved.
 *
 * This file comes from MafiaHub, hosted at https://github.com/MafiaHub/Framework.
 * See LICENSE file in the source repository for information regarding licensing.
 */

#pragma once

#include "button_state.h"
#include "input.h"
#include "physical_keys.h"

namespace Framework::Input::PhysicalKeyState {
    inline const IInput *Source(const IInput *input) {
        return input && input->ProvidesPhysicalKeyState() ? input : nullptr;
    }

    inline bool IsForeground() {
        DWORD pid = 0;
        ::GetWindowThreadProcessId(::GetForegroundWindow(), &pid);
        return pid == ::GetCurrentProcessId();
    }

    // Key names denote US physical positions. Legacy device adapters accept
    // layout VKs; adapters with physical codes explicitly declare that space.
    inline bool IsDown(int key, const IInput *input = nullptr) {
        if (key < 0 || key > 255) {
            return false;
        }
        if (const auto *source = Source(input)) {
            if (source->IsStateStale()) {
                return false;
            }
            const UINT code = source->GetKeyCodeSpace() == KeyCodeSpace::PhysicalPosition ? static_cast<UINT>(key) : PhysicalKeys::ToLayoutVirtualKey(static_cast<UINT>(key), ::GetKeyboardLayout(0));
            return code != 0 && source->IsKeyDown(static_cast<int>(code));
        }
        DWORD pid          = 0;
        const DWORD thread = ::GetWindowThreadProcessId(::GetForegroundWindow(), &pid);
        if (pid != ::GetCurrentProcessId()) {
            return false;
        }
        const UINT code = PhysicalKeys::ToLayoutVirtualKey(static_cast<UINT>(key), ::GetKeyboardLayout(thread));
        return code != 0 && (::GetAsyncKeyState(static_cast<int>(code)) & 0x8000) != 0;
    }

    // Passing no provider keeps UI hotkeys live while native game devices are
    // paused. Calling code chooses its UI/gameplay gate separately.
    inline void Update(KeySnapshot &snapshot, const IInput *input = nullptr) {
        const auto *source = Source(input);
        snapshot.Update(
            [input](int key) {
                return IsDown(key, input);
            },
            IsForeground(), source && source->IsStateStale());
    }
} // namespace Framework::Input::PhysicalKeyState
