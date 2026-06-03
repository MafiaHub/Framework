/*
 * MafiaHub OSS license
 * Copyright (c) 2021-2024, MafiaHub. All rights reserved.
 *
 * This file comes from MafiaHub, hosted at https://github.com/MafiaHub/Framework.
 * See LICENSE file in the source repository for information regarding licensing.
 */

#pragma once

#include "src/world/modules/base.hpp"
#include <spdlog/fmt/bundled/format.h>

template <>
struct fmt::formatter<glm::vec3> {
    constexpr auto parse(format_parse_context &ctx) {
        return ctx.begin();
    }

    template <typename FormatContext>
    auto format(const glm::vec3 &t, FormatContext &ctx) {
        return fmt::format_to(ctx.out(), "{:.2f}, {:.2f}, {:.2f}", t.x, t.y, t.z);
    }
};

template <>
struct fmt::formatter<glm::quat> {
    constexpr auto parse(format_parse_context &ctx) {
        return ctx.begin();
    }

    template <typename FormatContext>
    auto format(const glm::quat &t, FormatContext &ctx) {
        return fmt::format_to(ctx.out(), "{:.2f}, {:.2f}, {:.2f}, {:.2f}", t.x, t.y, t.z, t.w);
    }
};

template <>
struct fmt::formatter<Framework::World::Modules::Base::Transform> {
    constexpr auto parse(format_parse_context &ctx) {
        return ctx.begin();
    }

    template <typename FormatContext>
    auto format(const Framework::World::Modules::Base::Transform &t, FormatContext &ctx) {
        return fmt::format_to(ctx.out(), "Transform(pos=({}), vel=({}), rot=({}), genID={})", t.pos, t.vel, t.rot, t.GetGeneration());
    }
};
