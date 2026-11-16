/*
 * MafiaHub OSS license
 * Copyright (c) 2021-2026, MafiaHub. All rights reserved.
 *
 * This file comes from MafiaHub, hosted at https://github.com/MafiaHub/Framework.
 * See LICENSE file in the source repository for information regarding licensing.
 */

#pragma once

#include <atomic>
#include <cstdint>

// Heap allocations made by this thread while a scope is armed. framework_ut.cpp replaces the global
// operator new to feed it; code in other modules (libnode, CEF) keeps its own allocator and is not
// counted, which is fine: these scopes wrap Framework code only.
namespace FrameworkTests {
    inline thread_local bool g_countAllocations = false;
    inline thread_local uint64_t g_allocations  = 0;

    class AllocationScope final {
      public:
        AllocationScope() {
            g_allocations      = 0;
            g_countAllocations = true;
        }
        ~AllocationScope() {
            g_countAllocations = false;
        }
        AllocationScope(const AllocationScope &)            = delete;
        AllocationScope &operator=(const AllocationScope &) = delete;

        uint64_t Count() const {
            return g_allocations;
        }
    };
} // namespace FrameworkTests
