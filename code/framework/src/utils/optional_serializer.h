/*
 * MafiaHub OSS license
 * Copyright (c) 2021-2023, MafiaHub. All rights reserved.
 *
 * This file comes from MafiaHub, hosted at https://github.com/MafiaHub/Framework.
 * See LICENSE file in the source repository for information regarding licensing.
 */

#pragma once

#include "utils/optional.h"

#include <BitStream.h>

namespace Framework::Networking {
    template <typename T>
    inline void SerializeOptional(Utils::Optional<T> &opt, SLNet::BitStream *bs, bool write) {
        if (write) {
            bool hasValue = opt.HasValue();
            bs->Write(hasValue);
            if (hasValue) {
                bs->Write(opt.Value());
            }
        }
        else {
            bool hasValue = false;
            bs->Read(hasValue);
            if (hasValue) {
                T value {};
                bs->Read(value);
                opt = value;
            }
            else {
                opt.Clear();
            }
        }
    }
} // namespace Framework::Networking
