/*
 * MafiaHub OSS license
 * Copyright (c) 2021-2026, MafiaHub. All rights reserved.
 *
 * This file comes from MafiaHub, hosted at https://github.com/MafiaHub/Framework.
 * See LICENSE file in the source repository for information regarding licensing.
 */

#pragma once

// How a client finds and starts the CEF host, shared by both so the two cannot drift.
namespace Framework::GUI::Remote {
    // Next to the client module: the 64-bit host and its own CEF runtime, apart from any 32-bit one.
    inline constexpr const wchar_t *kHostDirectory  = L"cef64";
    inline constexpr const wchar_t *kHostExecutable = L"cef_host.exe";

    // --<switch>=<value> on the host's command line.
    inline constexpr const wchar_t *kHostPipeSwitch   = L"fw-pipe";
    inline constexpr const wchar_t *kHostTokenSwitch  = L"fw-token";
    inline constexpr const wchar_t *kHostRootSwitch   = L"fw-root";
    inline constexpr const wchar_t *kHostParentSwitch = L"fw-host-parent";
} // namespace Framework::GUI::Remote
