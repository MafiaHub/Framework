/*
 * MafiaHub OSS license
 * Copyright (c) 2021-2024, MafiaHub. All rights reserved.
 *
 * This file comes from MafiaHub, hosted at https://github.com/MafiaHub/Framework.
 * See LICENSE file in the source repository for information regarding licensing.
 */

#include "pattern_table.h"

#include <utils/safe_win32.h>

#include <cstdint>
#include <cstring>
#include <fstream>
#include <vector>

#include <logging/logger.h>

#include "hooking.h"
#include "hooking_patterns.h"

namespace hook {
    namespace {
        constexpr uint32_t kFormatVersion = 1;

#pragma pack(push, 1)
        struct TableHeader {
            char magic[8];
            uint32_t version;
            uint32_t entryCount;
            uint64_t patternSetHash;
            uint32_t targetSizeOfImage;
            uint32_t targetImageBase;
            uint32_t targetFileSize;
            uint32_t entriesCrc;
            uint64_t reserved;
        };

        struct TableEntry {
            uint64_t hash;
            uint32_t rva;
            uint32_t reserved;
        };
#pragma pack(pop)

        static_assert(sizeof(TableHeader) == 48, "pattern table header layout changed");
        static_assert(sizeof(TableEntry) == 16, "pattern table entry layout changed");

        uint32_t Crc32(const uint8_t *data, size_t size) {
            uint32_t crc = 0xFFFFFFFFu;
            for (size_t i = 0; i < size; ++i) {
                crc ^= data[i];
                for (int bit = 0; bit < 8; ++bit) {
                    crc = (crc >> 1) ^ (0xEDB88320u & (0u - (crc & 1u)));
                }
            }
            return ~crc;
        }
    } // namespace

    size_t load_pattern_table(const std::string &path) {
        const auto log = Framework::Logging::GetLogger("Hooking");

        std::ifstream file(path, std::ios::binary | std::ios::ate);
        if (!file.is_open()) {
            log->warn("Pattern table {} is missing, every pattern will be resolved by scanning", path);
            return 0;
        }

        const auto size = static_cast<size_t>(file.tellg());
        file.seekg(0);
        std::vector<uint8_t> blob(size);
        if (size < sizeof(TableHeader) || !file.read(reinterpret_cast<char *>(blob.data()), size)) {
            log->warn("Pattern table {} is truncated, every pattern will be resolved by scanning", path);
            return 0;
        }

        const auto *header = reinterpret_cast<const TableHeader *>(blob.data());
        if (memcmp(header->magic, "FWPATTBL", 8) != 0 || header->version != kFormatVersion) {
            log->warn("Pattern table {} is not format v{}, every pattern will be resolved by scanning", path, kFormatVersion);
            return 0;
        }
        if (size != sizeof(TableHeader) + static_cast<size_t>(header->entryCount) * sizeof(TableEntry)) {
            log->warn("Pattern table {} does not hold the {} entries it declares, every pattern will be resolved by scanning", path, header->entryCount);
            return 0;
        }

        const auto *entries = reinterpret_cast<const TableEntry *>(blob.data() + sizeof(TableHeader));
        if (Crc32(reinterpret_cast<const uint8_t *>(entries), size - sizeof(TableHeader)) != header->entriesCrc) {
            log->warn("Pattern table {} is corrupt, every pattern will be resolved by scanning", path);
            return 0;
        }

        auto *base            = reinterpret_cast<const uint8_t *>(getRVA<void>(0));
        const auto *dosHeader = reinterpret_cast<const IMAGE_DOS_HEADER *>(base);
        const auto *ntHeader  = reinterpret_cast<const IMAGE_NT_HEADERS *>(base + dosHeader->e_lfanew);
        if (ntHeader->OptionalHeader.SizeOfImage != header->targetSizeOfImage || ntHeader->OptionalHeader.ImageBase != header->targetImageBase) {
            log->warn("Pattern table {} was built for a different game image, every pattern will be resolved by scanning", path);
            return 0;
        }

        for (uint32_t i = 0; i < header->entryCount; ++i) {
            pattern::hint(entries[i].hash, reinterpret_cast<uintptr_t>(base + entries[i].rva));
        }
        log->info("Pattern table seeded {} addresses", header->entryCount);
        return header->entryCount;
    }
} // namespace hook
