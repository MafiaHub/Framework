/*
 * MafiaHub OSS license
 * Copyright (c) 2026, MafiaHub. All rights reserved.
 *
 * This file comes from MafiaHub, hosted at https://github.com/MafiaHub/Framework.
 * See LICENSE file in the source repository for information regarding licensing.
 */

#pragma once

#include "../bind/class.h"

namespace Framework::Scripting::Builtins {
    // Gives the class of a vector its Lua operators: + and - between two vectors, * by a number on either side,
    // / by a number, unary - and ==, which compares the components exactly. Each gives a new vector and leaves
    // its operands as they were. V wraps a glm vector: it is built from one and gives it with vec().
    template <typename V>
    Bind::Class<V> &AddVectorOperators(Bind::Class<V> &definition) {
        using Bind::LuaMetaEvent;
        return definition
            .LuaMeta(LuaMetaEvent::Add,
                [](const V &left, const V &right) {
                    return V(left.vec() + right.vec());
                })
            .LuaMeta(LuaMetaEvent::Sub,
                [](const V &left, const V &right) {
                    return V(left.vec() - right.vec());
                })
            .LuaMeta(LuaMetaEvent::Mul,
                [](const V &vector, float scalar) {
                    return V(vector.vec() * scalar);
                })
            .LuaMeta(LuaMetaEvent::Mul,
                [](float scalar, const V &vector) {
                    return V(scalar * vector.vec());
                })
            .LuaMeta(LuaMetaEvent::Div,
                [](const V &vector, float scalar) {
                    return V(vector.vec() / scalar);
                })
            .LuaMeta(LuaMetaEvent::Unm,
                [](const V &vector) {
                    return V(-vector.vec());
                })
            .LuaMeta(LuaMetaEvent::Eq, [](const V &left, const V &right) {
                return left.vec() == right.vec();
            });
    }
} // namespace Framework::Scripting::Builtins
