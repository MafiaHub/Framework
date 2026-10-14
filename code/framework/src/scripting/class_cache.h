/*
 * MafiaHub OSS license
 * Copyright (c) 2021-2024, MafiaHub. All rights reserved.
 *
 * This file comes from MafiaHub, hosted at https://github.com/MafiaHub/Framework.
 * See LICENSE file in the source repository for information regarding licensing.
 */

#pragma once

#include <v8.h>
#include <v8pp/class.hpp>

#include <memory>
#include <unordered_map>
#include <utility>

namespace Framework::Scripting {
    // The v8pp class a handle type binds, built once per isolate. A class_ holds its isolate's
    // templates, so each isolate gets its own, and it must be dropped (Unregister) before its
    // isolate is disposed.
    template <typename H>
    class ClassCache final {
      public:
        // The isolate's class, built on first use by `build()`, which returns the
        // std::unique_ptr<v8pp::class_<H>> it made and configured.
        template <typename Build>
        v8pp::class_<H> &Get(v8::Isolate *isolate, Build &&build) {
            auto it = _classes.find(isolate);
            if (it != _classes.end()) {
                return *it->second;
            }
            std::unique_ptr<v8pp::class_<H>> cls = build();
            return *_classes.insert_or_assign(isolate, std::move(cls)).first->second;
        }

        void Unregister(v8::Isolate *isolate) {
            _classes.erase(isolate);
        }

      private:
        std::unordered_map<v8::Isolate *, std::unique_ptr<v8pp::class_<H>>> _classes;
    };
} // namespace Framework::Scripting
