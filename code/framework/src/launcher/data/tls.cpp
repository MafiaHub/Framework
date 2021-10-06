/*
 * This file is part of the CitizenFX project - http://citizen.re/
 *
 * See LICENSE and MENTIONS in the root of the source tree for information
 * regarding licensing.
 */

#include <cstdint>

#define DECLARE_TLS_VARS(i) __declspec(thread) uint8_t tls1[sizeof(int) * i];

#pragma region tls
DECLARE_TLS_VARS(7600);
#pragma endregion

#include <Windows.h>

extern "C" extern uint8_t _tls_start;
extern "C" extern uint32_t _tls_index;

extern "C" __declspec(dllexport) void GetThreadLocalStorage(void **base, uint32_t *index) {
    *base  = &_tls_start;
    *index = _tls_index;
}

struct TlsEnabler {
    TlsEnabler() {
        tls1[4] = GetTickCount64();
    }
};
TlsEnabler __enabler;
