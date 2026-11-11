/*
 * MafiaHub OSS license
 * Copyright (c) 2026, MafiaHub. All rights reserved.
 *
 * This file comes from MafiaHub, hosted at https://github.com/MafiaHub/Framework.
 * See LICENSE file in the source repository for information regarding licensing.
 */

#include "call.h"

#include "../module.h"
#include "../value_transfer.h"

#include <core_modules.h>

#include <atomic>
#include <mutex>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

namespace Framework::Scripting::Bind {
    namespace {
        // Runtimes may be set up on different threads.
        std::mutex &ManagersMutex() {
            static std::mutex mutex;
            return mutex;
        }

        std::unordered_map<const void *, ResourceManager *> &Managers() {
            static std::unordered_map<const void *, ResourceManager *> managers;
            return managers;
        }
    } // namespace

    namespace Detail {
        namespace {
            // Calls nest on a thread and never move to another, so the innermost one is the thread's own to know.
            thread_local ValueBackend *innermost = nullptr;

            // Shared by every thread: a generation is never given twice, so a handle that strayed to another
            // thread finds no call there either. 0 is left for the handle of no call.
            std::atomic<uint64_t> lastGeneration {0};
        } // namespace

        ValueBackend::ValueBackend(): _generation(lastGeneration.fetch_add(1, std::memory_order_relaxed) + 1), _outer(innermost) {
            innermost = this;
        }

        ValueBackend::~ValueBackend() {
            innermost = _outer;
        }

        ValueBackend *ValueBackend::Innermost() {
            return innermost;
        }
    } // namespace Detail

    // Out of the header, which names TransferredValue without defining it.
    Utils::Result<TransferredValue, std::string> Value::ToTransfer() const {
        const Target target = Find();
        return target.backend != nullptr ? target.backend->TransferOf(target.slot) : Utils::Result<TransferredValue, std::string>::Ok();
    }

    void SetCallResourceManager(const void *runtime, ResourceManager *manager) {
        std::scoped_lock lock(ManagersMutex());
        if (manager != nullptr) {
            Managers()[runtime] = manager;
        }
        else {
            Managers().erase(runtime);
        }
    }

    void ForgetCallResourceManager(const ResourceManager *manager) {
        std::scoped_lock lock(ManagersMutex());
        std::erase_if(Managers(), [manager](const auto &registered) {
            return registered.second == manager;
        });
    }

    namespace Detail {
        ResourceManager *CallResourceManager(const void *runtime) {
            {
                std::scoped_lock lock(ManagersMutex());
                const auto it = Managers().find(runtime);
                if (it != Managers().end()) {
                    return it->second;
                }
            }
            const ScriptingModule *module = CoreModules::GetScriptingModule();
            return module != nullptr ? module->GetResourceManager() : nullptr;
        }

        NativeFunction CallableForwarder(std::shared_ptr<ScriptCallable> callable) {
            // What the call and the `done` of its Invoke share: the outcome when `done` came first, the deferred
            // result of the call when the call returned first.
            struct Answer final {
                bool told    = false;
                bool waiting = false;
                CallOutcome outcome;
                Deferred later;
            };

            return [callable = std::move(callable)](Call &call) {
                // An argument is named as the calling language counts it.
                const size_t first = call.GetLanguage() == Language::Lua ? 1 : 0;
                std::vector<TransferredValue> arguments(call.ArgCount());
                for (size_t i = 0; i < arguments.size(); ++i) {
                    auto copied = call.Arg(i).ToTransfer();
                    if (!copied) {
                        call.ThrowError("Argument " + std::to_string(i + first) + ": " + copied.GetError());
                        return;
                    }
                    arguments[i] = std::move(copied).GetValue();
                }

                const auto answer = std::make_shared<Answer>();
                callable->Invoke(arguments, ReturnMode::Value, [answer](CallOutcome &&outcome) {
                    if (!answer->waiting) {
                        answer->told    = true;
                        answer->outcome = std::move(outcome);
                    }
                    else if (outcome.threw) {
                        answer->later.Reject(std::move(outcome.error));
                    }
                    else {
                        answer->later.Resolve(std::move(outcome.value));
                    }
                });

                if (!answer->told) {
                    answer->later   = call.Defer();
                    answer->waiting = true;
                }
                else if (answer->outcome.threw) {
                    call.ThrowError(std::move(answer->outcome.error));
                }
                else if (auto result = call.FromTransfer(answer->outcome.value)) {
                    call.Return(std::move(result).GetValue());
                }
                else {
                    call.ThrowError(result.GetError());
                }
            };
        }
    } // namespace Detail
} // namespace Framework::Scripting::Bind
