/*
 * MafiaHub OSS license
 * Copyright (c) 2021-2023, MafiaHub. All rights reserved.
 *
 * This file comes from MafiaHub, hosted at https://github.com/MafiaHub/Framework.
 * See LICENSE file in the source repository for information regarding licensing.
 */

#pragma once

namespace Framework::Launcher {
    // Steers hybrid-graphics machines onto the discrete GPU. Must run before anything
    // in the process creates a D3D device.
    void ForceHighPerformanceGPU();
} // namespace Framework::Launcher
