/*
 * MafiaHub OSS license
 * Copyright (c) 2021-2023, MafiaHub. All rights reserved.
 *
 * This file comes from MafiaHub, hosted at https://github.com/MafiaHub/Framework.
 * See LICENSE file in the source repository for information regarding licensing.
 */

#pragma once

#include <tuple>

namespace Framework::Utils {
    template <typename T> struct function_traits : public function_traits<decltype(&T::operator())> {};

    template <typename ClassType, typename ReturnType, typename... Args>
    struct function_traits<ReturnType (ClassType::*)(Args...) const> {
        template <size_t i> using arg_t = std::tuple_element_t<i, std::tuple<Args...>>;
    };
} // namespace Framework::Utils
