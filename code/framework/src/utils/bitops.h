/*
 * MafiaHub OSS license
 * Copyright (c) 2021-2023, MafiaHub. All rights reserved.
 *
 * This file comes from MafiaHub, hosted at https://github.com/MafiaHub/Framework.
 * See LICENSE file in the source repository for information regarding licensing.
 */

#pragma once

// Define a flag at given position 'pos' in enum
#define BIT_FLD(pos) (1U << (pos))

// Set the bit specified by 'val' in 'var'
#define BIT_SET(var, val) (((uint64_t)var) |= ((uint64_t)val))

// Clear the bit specified by 'val' in 'var'
#define BIT_CLR(var, val) (((uint64_t)var) &= ~((uint64_t)val))

// Check if the bit specified by 'val' in 'var' is set
#define BIT_HAS(var, val) (((uint64_t)var) & ((uint64_t)val))

// Perform bitwise NOT on 'var'
#define BIT_NOT(var) (~((uint64_t)var))

// Perform bitwise XOR between 'var' and 'mask'
#define BIT_XOR(var, mask) (((uint64_t)var) ^= ((uint64_t)mask))

// Perform bitwise AND between 'var' and 'mask'
#define BIT_AND(var, mask) (((uint64_t)var) &= ((uint64_t)mask))
