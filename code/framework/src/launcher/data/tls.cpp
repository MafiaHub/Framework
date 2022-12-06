/*
 * MafiaHub OSS license
 * Copyright (c) 2020, CitizenFX
 * Copyright (c) 2021-2022, MafiaHub. All rights reserved.
 *
 * This file comes from MafiaHub, hosted at https://github.com/MafiaHub/Framework.
 * See LICENSE file in the source repository for information regarding licensing.
 */

#include <cstdint>

__declspec(thread) uint8_t tls1[sizeof(int) * 17600];

extern "C" uint8_t _tls_start;
extern "C" uint32_t _tls_index;

class TlsActivation {
  public:
    TlsActivation() {
        tls1[5] = 0;
    }
};

[[maybe_unused]] static TlsActivation TlsActivateItEarly;

extern "C" __declspec(dllexport) void GetThreadLocalStorage(void **base, uint32_t *index) {
    *base  = &_tls_start;
    *index = _tls_index;
}
