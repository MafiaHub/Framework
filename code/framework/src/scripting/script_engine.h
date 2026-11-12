/*
 * MafiaHub OSS license
 * Copyright (c) 2026, MafiaHub. All rights reserved.
 *
 * This file comes from MafiaHub, hosted at https://github.com/MafiaHub/Framework.
 * See LICENSE file in the source repository for information regarding licensing.
 */

#pragma once

#include <atomic>
#include <cstdint>
#include <string>
#include <string_view>

#include <utils/lifecycle.h>

namespace Framework::Scripting {

    class ResourceManager; // Forward declaration for resource context tracking

    enum class Language : uint8_t {
        JavaScript,
        Lua
    };

    // What the log files a line of a script's output under.
    enum class OutputLevel : uint8_t {
        Debug,
        Info,
        Warning,
        Error
    };

    /**
     * Whether script of another language is on the stack of this thread.
     *
     * A runtime that is destroyed while script runs must outlive that script. Each engine sees to that for its own
     * runtimes: it puts the destruction of one that is executing off to its next tick. But script of one language
     * holds things of a runtime of the other across a call, through the binding layer: a Lua emitter holds the
     * JavaScript handlers it picked, the caller of an export the function it called. So an engine also puts a
     * destruction off while script of another language is running, which is what it asks here.
     *
     * An engine that enters its runtimes in one place says so there (Running), as the Lua engine does. V8 is
     * entered from many places, by the engines, the builtins and the projects, so the JavaScript engines answer
     * through a probe instead: an isolate is entered on this thread. Neither engine needs to know the other.
     */
    class ScriptActivity final {
      public:
        // For as long as it lives, script of `language` is running on this thread.
        class Running final {
          public:
            explicit Running(Language language): _depth(Depth(language)) {
                ++_depth;
            }
            ~Running() {
                --_depth;
            }
            Running(const Running &)            = delete;
            Running &operator=(const Running &) = delete;

          private:
            int &_depth;
        };

        using Probe = bool (*)();

        // Whether script of a language other than `language` is on the stack of this thread.
        static bool OfAnother(Language language) {
            if (language != Language::Lua && Depth(Language::Lua) > 0) {
                return true;
            }
            if (language == Language::JavaScript) {
                return false;
            }
            const Probe probe = IsolateProbe().load(std::memory_order_relaxed);
            return Depth(Language::JavaScript) > 0 || (probe != nullptr && probe());
        }

        // Set by the JavaScript engines as they initialise: says whether an isolate is entered on this thread,
        // which stands for JavaScript being on the stack.
        static void SetIsolateProbe(Probe probe) {
            IsolateProbe().store(probe, std::memory_order_relaxed);
        }

      private:
        static int &Depth(Language language) {
            thread_local int depths[2] = {0, 0};
            return depths[language == Language::Lua ? 1 : 0];
        }

        static std::atomic<Probe> &IsolateProbe() {
            static std::atomic<Probe> probe {nullptr};
            return probe;
        }
    };

    /**
     * What ResourceManager needs of a scripting engine, whatever language its scripts are written in.
     * Engine is the JavaScript one, LuaEngine the Lua one.
     */
    class ScriptEngine: public Framework::Lifecycle {
      public:
        virtual Language GetLanguage() const = 0;

        // Whether this engine runs the script file at `path`. Every file belongs to exactly one engine.
        virtual bool HandlesFile(std::string_view path) const = 0;

        // Advance timers and whatever else waits, without blocking.
        virtual void Tick() = 0;

        /**
         * Create the runtime a resource's scripts run in.
         * @return false with the reason in GetLastError().
         */
        virtual bool CreateResourceRuntime(const std::string &resourceName) = 0;

        // Destroy it, and with it everything the resource left running there. A runtime that is executing is destroyed
        // at the next tick instead.
        virtual void DestroyResourceRuntime(const std::string &resourceName) = 0;

        /**
         * Run one of a resource's script files in its runtime.
         * @return false with the reason in GetLastError().
         */
        virtual bool ExecuteResourceFile(const std::string &resourceName, std::string_view filepath) = 0;

        /**
         * Get the last error message.
         */
        const std::string &GetLastError() const {
            return _lastError;
        }

        /**
         * Set the owning ResourceManager for resource context tracking.
         * Called by ResourceManager during construction.
         */
        void SetResourceManager(ResourceManager *manager) {
            _resourceManager = manager;
        }

        /**
         * Get the owning ResourceManager.
         */
        ResourceManager *GetResourceManager() const {
            return _resourceManager;
        }

      protected:
        std::string _lastError;
        ResourceManager *_resourceManager = nullptr;
    };

} // namespace Framework::Scripting
