/*
 * MafiaHub OSS license
 * Copyright (c) 2026, MafiaHub. All rights reserved.
 *
 * This file comes from MafiaHub, hosted at https://github.com/MafiaHub/Framework.
 * See LICENSE file in the source repository for information regarding licensing.
 */

#pragma once

// The one place sol2 is configured and included: nothing else includes <sol/sol.hpp>, so that every translation
// unit sees the same sol.
//
// Lua is built as C++ here. sol then includes <lua.h>, <lauxlib.h> and <lualib.h> as they are, without lua.hpp and
// without extern "C", and never catches (...), which would swallow the exception a Lua error is.
#define SOL_USING_CXX_LUA 1

// A C++ exception does not cross lua_pcall as itself: one that is not Lua's own arrives there as status -1 without
// a message. sol's trampoline turns a std::exception into a Lua error instead, and raises it once the catch block
// is left.
#define SOL_EXCEPTIONS_SAFE_PROPAGATION 0

// Checked getters, checked usertype receivers and checked numerics, in every build type, so that a debug and a
// release build read a script's values the same way.
#define SOL_ALL_SAFETIES_ON 1

// Except the getters: with the safety on sol::stack::get checks the value again, and every caller here has checked
// it, since LuaConvert::Get has Is for precondition.
#define SOL_SAFE_GETTER 0

// The safeties also write every error to std::cerr and to a default traceback handler; the host has its own.
#define SOL_PRINT_ERRORS 0

// A string is not a number and a number is not a string (spec 1.2).
#define SOL_STRINGS_ARE_NUMBERS 0

#include <sol/sol.hpp>
