#include <cstdint>

#define DECLARE_TLS_VARS(i) __declspec(thread) uint8_t tls1[sizeof(int) * i];

#pragma region tls
DECLARE_TLS_VARS(30000);
#pragma endregion

#include <Windows.h>

extern "C" extern char _tls_start;
extern "C" extern ULONG _tls_index;

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
