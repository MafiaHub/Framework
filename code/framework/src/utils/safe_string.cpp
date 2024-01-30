/*
 * MafiaHub OSS license
 * Copyright (c) 2021-2023, MafiaHub. All rights reserved.
 *
 * This file comes from MafiaHub, hosted at https://github.com/MafiaHub/Framework.
 * See LICENSE file in the source repository for information regarding licensing.
 */

#include "safe_string.h"

namespace Framework::Utils {
    size_t fw_strlcpy(char *dst, const char *src, size_t size) {
        size_t src_len = strlen(src);
        size_t copy_len = src_len < size - 1 ? src_len : size - 1;
        memcpy(dst, src, copy_len);
        dst[copy_len] = '\0';
        return src_len;
    }
} // namespace Framework::Utils
