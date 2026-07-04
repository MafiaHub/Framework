/*
 * MafiaHub OSS license
 * Copyright (c) 2021-2026, MafiaHub. All rights reserved.
 *
 * This file comes from MafiaHub, hosted at https://github.com/MafiaHub/Framework.
 * See LICENSE file in the source repository for information regarding licensing.
 */

#pragma once

#include <tracy/Tracy.hpp>

// Profiling instrumentation wrapper (Tracy backend).
//
// Mods and framework code should only ever use these FW_PROFILE_* macros -
// never include Tracy headers directly. This keeps the profiler backend
// swappable and the mod-facing surface stable.
//
// All macros compile to nothing when the framework is built with
// -DFW_PROFILING=OFF, so instrumentation can stay in shipped code.
// With the default on-demand mode, an instrumented build has near-zero
// overhead until a Tracy profiler actually connects.

// Marks the current scope as a profiling zone (RAII, ends when scope exits).
// The zone is named after the enclosing function.
#define FW_PROFILE_SCOPE() ZoneScoped

// Same, with an explicit zone name (must be a string literal).
#define FW_PROFILE_SCOPE_N(name) ZoneScopedN(name)

// Named zone with a fixed color (0xRRGGBB).
#define FW_PROFILE_SCOPE_NC(name, color) ZoneScopedNC(name, color)

// Attaches dynamic text to the current zone (e.g. entity id, resource name).
#define FW_PROFILE_TEXT(txt, size) ZoneText(txt, size)

// Attaches a numeric value to the current zone.
#define FW_PROFILE_VALUE(value) ZoneValue(value)

// Marks the end of a frame - place once per main-loop iteration.
#define FW_PROFILE_FRAME() FrameMark

// Marks the end of a named secondary frame set (must be a string literal).
#define FW_PROFILE_FRAME_N(name) FrameMarkNamed(name)

// Plots a value over time under the given name (must be a string literal).
#define FW_PROFILE_PLOT(name, value) TracyPlot(name, value)

// Emits a log message into the profiler timeline.
#define FW_PROFILE_MESSAGE(txt, size) TracyMessage(txt, size)

// Names the current thread in profiler output.
#define FW_PROFILE_THREAD(name) tracy::SetThreadName(name)

// Memory tracking hooks (pair every alloc with a free).
#define FW_PROFILE_ALLOC(ptr, size) TracyAlloc(ptr, size)
#define FW_PROFILE_FREE(ptr)        TracyFree(ptr)

// Fiber tracking (FTL job system): call when a fiber is scheduled in/out.
#define FW_PROFILE_FIBER_ENTER(name) TracyFiberEnter(name)
#define FW_PROFILE_FIBER_LEAVE       TracyFiberLeave
