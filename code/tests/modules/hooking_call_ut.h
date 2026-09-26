/*
 * MafiaHub OSS license
 * Copyright (c) 2026, MafiaHub. All rights reserved.
 *
 * This file comes from MafiaHub, hosted at https://github.com/MafiaHub/Framework.
 * See LICENSE file in the source repository for information regarding licensing.
 */

#pragma once

// The hooking layer is Windows-only; framework_ut.cpp registers this module under the
// same guard.
#include <utils/safe_win32.h>

#include "utils/hooking/hooking.h"

#include <cstdint>
#include <cstring>
#include <type_traits>

// MODULE() is a macro, so the natives these tests call and patch live at file scope.
namespace FwHookingCallUT {
    struct Out {
        uint64_t value;
    };

    using Callback = void (*)();

    inline Callback gStored = nullptr;

    inline Out *FillOut(Out *out) {
        out->value = 0x1234;
        return out;
    }

    inline uint64_t Twice(uint64_t value) {
        return value * 2;
    }

    inline Callback Echo(Callback callback) {
        return callback;
    }

    inline void Store(Callback callback) {
        gStored = callback;
    }

    inline void Touch(Out *out) {
        out->value = 0x5678;
    }

    inline void Target() {}

    struct Probe {
        void Method() {}
    };

    // Inside the image, so a rel32 written here always reaches the targets. Never executed.
    alignas(16) inline uint8_t gPatchSite[16] = {};

    template <typename F>
    uintptr_t Address(F function) {
        return reinterpret_cast<uintptr_t>(function);
    }

    // False on x86, whose call() casts its target to an integer and so cannot take a member.
    inline bool PatchMember() {
#ifdef _M_AMD64
        hook::call(Address(gPatchSite), &Probe::Method);
        return true;
#else
        return false;
#endif
    }

    // The patcher returns void, so a typed result proves the invoke overload was chosen.
    static_assert(std::is_same_v<decltype(hook::call<Out *>(uintptr_t {}, static_cast<Out *>(nullptr))), Out *>);
    static_assert(std::is_same_v<decltype(hook::call<uint64_t>(uintptr_t {}, uint64_t {})), uint64_t>);
    static_assert(std::is_same_v<decltype(hook::call<Callback>(uintptr_t {}, Callback {})), Callback>);

    static_assert(hook::patch_target<Callback> && hook::patch_target<void (Probe::*)()>);
    static_assert(!hook::patch_target<void *> && !hook::patch_target<uintptr_t> && !hook::patch_target<Out *>);
} // namespace FwHookingCallUT

// hook::call both invokes a native and patches a call instruction. The patcher used to win
// whenever the one argument matched the explicit return type, overwriting the native's code.
MODULE(hooking_call, {
    using namespace FwHookingCallUT;

    IT("invokes an sret native whose one argument has the explicit return type", {
        Out out {};
        Out *returned = hook::call<Out *>(Address(&FillOut), &out);
        UEQUALS(Address(returned), Address(&out));
        UEQUALS(out.value, uint64_t {0x1234});
    });

    IT("invokes a native taking and returning the same integer type", {
        const uint64_t doubled = hook::call<uint64_t>(Address(&Twice), uint64_t {21});
        UEQUALS(doubled, uint64_t {42});
    });

    IT("invokes a native taking and returning the same function-pointer type", {
        const Callback echoed = hook::call<Callback>(Address(&Echo), &Target);
        UEQUALS(Address(echoed), Address(&Target));
    });

    IT("invokes a void native taking a function pointer when the return type is explicit", {
        gStored = nullptr;
        hook::call<void>(Address(&Store), &Target);
        UEQUALS(Address(gStored), Address(&Target));
    });

    IT("invokes a void native with a deduced data argument", {
        Out out {};
        hook::call(Address(&Touch), &out);
        UEQUALS(out.value, uint64_t {0x5678});
    });

    IT("patches a call to a deduced function pointer", {
        memset(gPatchSite, 0x90, sizeof(gPatchSite));
        hook::call(Address(gPatchSite), &Target);
        UEQUALS(uint64_t {gPatchSite[0]}, uint64_t {0xE8});
        UEQUALS(hook::get_call(Address(gPatchSite)), Address(&Target));
    });

    IT("patches a call to a deduced member function pointer", {
        memset(gPatchSite, 0x90, sizeof(gPatchSite));
        if (!PatchMember()) {
            SKIP();
        }
        UEQUALS(uint64_t {gPatchSite[0]}, uint64_t {0xE8});
        UEQUALS(hook::get_call(Address(gPatchSite)), hook::get_member(&Probe::Method));
    });
});
