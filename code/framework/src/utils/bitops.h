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
#define BIT_SET(var, val) ((var) |= (val))

// Clear the bit specified by 'val' in 'var'
#define BIT_CLR(var, val) ((var) &= ~(val))

// Check if the bit specified by 'val' in 'var' is set
#define BIT_HAS(var, val) ((var) & (val))

// Perform bitwise NOT on 'var'
#define BIT_NOT(var) (~(var))

// Perform bitwise XOR between 'var' and 'mask'
#define BIT_XOR(var, mask) ((var) ^= (mask))

// Perform bitwise AND between 'var' and 'mask'
#define BIT_AND(var, mask) ((var) &= (mask))
