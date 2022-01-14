/*
 * MafiaHub OSS license
 * Copyright (c) 2020, CitizenFX
 * Copyright (c) 2022, MafiaHub. All rights reserved.
 *
 * This file comes from MafiaHub, hosted at https://github.com/MafiaHub/Framework.
 * See LICENSE file in the source repository for information regarding licensing.
 */

#pragma once

#include "hook_function.h"

namespace hook {
    namespace details {
        class function_stub_base {
          protected:
            void *m_functionAddress;

          public:
            inline void SetFunctionAddress(void *address) {
                m_functionAddress = address;
            }
        };

        class StubInitFunction: public HookFunctionBase {
          private:
            void *(*m_function)();

            function_stub_base *m_stub;

          public:
            StubInitFunction(function_stub_base *stub, void *(*function)()) {
                m_function = function;
                m_stub     = stub;
            }

            virtual void Run() {
                m_stub->SetFunctionAddress(m_function());
            }
        };

        template <typename TRet, typename TClass, typename... Args>
        class thiscall_stub_: public function_stub_base {
          private:
            StubInitFunction m_hookFunction;

          public:
            thiscall_stub_(void *(*getter)()): m_hookFunction(this, getter) {}

            inline TRet operator()(TClass klass, Args... args) {
                struct EmptyClass {};

                using TBare = std::remove_pointer_t<TClass>;

                using TThis = std::conditional_t<std::is_void_v<TBare> || std::is_integral_v<TBare>, EmptyClass, TBare>;

                union {
                    TRet (__thiscall TThis::*fn)(Args...);
                    struct {
                        void *ptr;
                        size_t off;
                    } raw;
                } mfPtr;

                mfPtr.raw.ptr = m_functionAddress;

                if constexpr (sizeof(mfPtr.raw.ptr) != sizeof(mfPtr.fn)) {
                    static_assert(sizeof(mfPtr.raw) == sizeof(mfPtr.fn), "more than just an adjustor");

                    mfPtr.raw.off = 0;
                }

                return (((TThis *)(klass))->*(mfPtr.fn))(args...);
            }
        };

        template <typename TRet, typename... Args>
        class cdecl_stub_: public function_stub_base {
          private:
            StubInitFunction m_hookFunction;

          public:
            cdecl_stub_(void *(*getter)()): m_hookFunction(this, getter) {}

            inline TRet operator()(Args... args) {
                return reinterpret_cast<TRet(__cdecl *)(Args...)>(m_functionAddress)(args...);
            }
        };
    } // namespace details

    template <typename TRet>
    class thiscall_stub {};

    template <typename TRet, typename... Args>
    class thiscall_stub<TRet(Args...)>: public details::thiscall_stub_<TRet, Args...> {
      public:
        thiscall_stub(void *(*getter)()): details::thiscall_stub_<TRet, Args...>(getter) {}
    };

    template <typename TRet>
    class cdecl_stub {};

    template <typename TRet, typename... Args>
    class cdecl_stub<TRet(Args...)>: public details::cdecl_stub_<TRet, Args...> {
      public:
        cdecl_stub(void *(*getter)()): details::cdecl_stub_<TRet, Args...>(getter) {}
    };
} // namespace hook
