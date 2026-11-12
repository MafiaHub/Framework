/*
 * MafiaHub OSS license
 * Copyright (c) 2021-2024, MafiaHub. All rights reserved.
 *
 * This file comes from MafiaHub, hosted at https://github.com/MafiaHub/Framework.
 * See LICENSE file in the source repository for information regarding licensing.
 */

#pragma once

#include "scripting_catalog.h"

#include <v8.h>
#include <v8pp/class.hpp>

#include <memory>
#include <mutex>
#include <unordered_map>
#include <utility>

namespace Framework::Scripting {
    // The v8pp class a handle type binds, built once per isolate. A class_ holds its isolate's
    // templates, so each isolate gets its own, and it must be dropped (Unregister) before its
    // isolate is disposed.
    //
    // One cache serves every isolate, and isolates are used from several threads at once (each
    // resource's on the script thread, the tick thread's and the packet thread's own), so the table
    // has a mutex. It is held for the lookup only: a class is built and dropped outside it, and the
    // reference handed out stays good until that isolate is unregistered, whatever is added meanwhile.
    template <typename H>
    class ClassCache final {
      public:
        // The isolate's class, built on first use by `build()`, which returns the
        // std::unique_ptr<v8pp::class_<H>> it made and configured.
        template <typename Build>
        v8pp::class_<H> &Get(v8::Isolate *isolate, Build &&build) {
            if (v8pp::class_<H> *found = Find(isolate)) {
                return *found;
            }
            // Building declares the class in a catalog other isolates share: see CatalogWriteLock.
            const CatalogWriteLock building;
            return Add(isolate, build());
        }

        // The two halves of Get, for a builder written out in its caller, which holds a
        // CatalogWriteLock from Find's miss to Add. Null until Add.
        v8pp::class_<H> *Find(v8::Isolate *isolate) {
            const std::lock_guard guard(_mutex);
            const auto it = _classes.find(isolate);
            return it != _classes.end() ? it->second.get() : nullptr;
        }
        // An isolate is one thread's at a time, so nobody else adds its class meanwhile.
        v8pp::class_<H> &Add(v8::Isolate *isolate, std::unique_ptr<v8pp::class_<H>> cls) {
            const std::lock_guard guard(_mutex);
            return *_classes.insert_or_assign(isolate, std::move(cls)).first->second;
        }

        void Unregister(v8::Isolate *isolate) {
            std::unique_ptr<v8pp::class_<H>> dropped;
            {
                const std::lock_guard guard(_mutex);
                const auto it = _classes.find(isolate);
                if (it == _classes.end()) {
                    return;
                }
                dropped = std::move(it->second);
                _classes.erase(it);
            }
        }

      private:
        std::mutex _mutex;
        std::unordered_map<v8::Isolate *, std::unique_ptr<v8pp::class_<H>>> _classes;
    };
} // namespace Framework::Scripting
