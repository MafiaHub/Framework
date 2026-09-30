/*
 * MafiaHub OSS license
 * Copyright (c) 2021-2023, MafiaHub. All rights reserved.
 *
 * This file comes from MafiaHub, hosted at https://github.com/MafiaHub/Framework.
 * See LICENSE file in the source repository for information regarding licensing.
 */

// Compiled into each launcher rather than into FrameworkLoader, so the launcher's own
// FW_LAUNCHER_GAME_RESERVE sizes it. It must cover the game executable's SizeOfImage; the
// defaults fit the largest game any project maps, and every megabyte of it is a megabyte of
// empty image that antivirus heuristics read as a packer. See fwgame_begin in project.cpp.
#ifndef FW_LAUNCHER_GAME_RESERVE
#ifdef _M_AMD64
#define FW_LAUNCHER_GAME_RESERVE 0x6fffffff
#else
#define FW_LAUNCHER_GAME_RESERVE 0x2500000
#endif
#endif

#pragma bss_seg(".fwgame$b")
char fwgame_seg[FW_LAUNCHER_GAME_RESERVE];
