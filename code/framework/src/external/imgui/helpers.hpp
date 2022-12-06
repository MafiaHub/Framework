/*
 * MafiaHub OSS license
 * Copyright (c) 2021-2022, MafiaHub. All rights reserved.
 *
 * This file comes from MafiaHub, hosted at https://github.com/MafiaHub/Framework.
 * See LICENSE file in the source repository for information regarding licensing.
 */

#pragma once

#include "utils/function_traits.hpp"

#include <function2.hpp>

namespace Framework::External::ImGUI {
    template <typename F>
    auto getCallback(F f) {
        return [](typename Framework::Utils::function_traits<F>::template arg_t<0> data) {
            auto &f = *static_cast<F *>(data->UserData);
            return f(data);
        };
    }
}; // namespace Framework::External::ImGUI
