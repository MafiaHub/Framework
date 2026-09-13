/*
 * MafiaHub OSS license
 * Copyright (c) 2021-2024, MafiaHub. All rights reserved.
 *
 * This file comes from MafiaHub, hosted at https://github.com/MafiaHub/Framework.
 * See LICENSE file in the source repository for information regarding licensing.
 */

#pragma once

// The hooking layer is Windows-only; framework_ut.cpp registers this module under the
// same guard.
#include <utils/safe_win32.h>

#include "utils/hooking/hooking.h"
#include "utils/hooking/hooking_patterns.h"
#include "utils/hooking/pattern_table.h"

#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

// MODULE() is a macro, so these have to live at file scope: a #pragma cannot survive a macro
// expansion. They mirror the on-disk layouts in pattern_table.cpp.
namespace FwPatternTableUT {
#pragma pack(push, 1)
    struct Header {
        char magic[8];
        uint32_t version;
        uint32_t entryCount;
        uint64_t patternSetHash;
        uint64_t targetImageBase;
        uint32_t targetSizeOfImage;
        uint32_t targetFileSize;
        uint32_t entriesCrc;
        uint32_t reserved;
    };

    // v1 agrees with v2 up to patternSetHash, then stores sizeOfImage before a 32-bit base.
    struct HeaderV1 {
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

    struct Entry {
        uint64_t hash;
        uint32_t rva;
        uint32_t reserved;
    };
#pragma pack(pop)

    static_assert(sizeof(Header) == 48, "v2 header must stay 48 bytes");
    static_assert(sizeof(HeaderV1) == 48, "v1 header must stay 48 bytes");
    static_assert(sizeof(Entry) == 16, "entry must stay 16 bytes");

    inline uint32_t Crc32(const uint8_t *data, size_t size) {
        uint32_t crc = 0xFFFFFFFFu;
        for (size_t i = 0; i < size; ++i) {
            crc ^= data[i];
            for (int bit = 0; bit < 8; ++bit) {
                crc = (crc >> 1) ^ (0xEDB88320u & (0u - (crc & 1u)));
            }
        }
        return ~crc;
    }
} // namespace FwPatternTableUT

// Guards the x64 pattern-table contract. The generator used to read ImageBase from the PE32
// offset, so for a PE32+ image it recorded the high dword (1 for the usual 0x140000000); the
// loader compared that against the real base and rejected every table. Nothing failed loudly —
// each pattern just quietly fell back to a full-image scan — so the feature was dead on x64 for
// as long as it existed. These tests pin the header layout and the accept/reject decisions so a
// regression shows up here instead of as unexplained boot time.
MODULE(pattern_table, {
    using namespace FwPatternTableUT;

    // This test binary's own PE headers stand in for the game image.
    const auto *self        = reinterpret_cast<const uint8_t *>(GetModuleHandleW(nullptr));
    const auto *dosHeader   = reinterpret_cast<const IMAGE_DOS_HEADER *>(self);
    const auto *ntHeader    = reinterpret_cast<const IMAGE_NT_HEADERS *>(self + dosHeader->e_lfanew);
    const uint64_t selfBase = ntHeader->OptionalHeader.ImageBase;
    const uint32_t selfSize = ntHeader->OptionalHeader.SizeOfImage;

    // load_pattern_table() resolves the loaded module through getRVA(), which reads
    // baseAddressDifference — zero until set_base() runs, which would send it dereferencing
    // the preferred base instead of where this binary actually sits. Every caller does this
    // first; so must the tests.
    hook::set_base(reinterpret_cast<uintptr_t>(self));

    const auto tempPath   = (std::filesystem::temp_directory_path() / "fw_pattern_table_ut.bin").string();
    const auto absentPath = (std::filesystem::temp_directory_path() / "fw_pattern_table_ut_absent.bin").string();

    const std::vector<Entry> oneEntry = {{0x1122334455667788ull, 0x1000, 0}};
    const auto entryBytes             = [&](const std::vector<Entry> &e) {
        return e.empty() ? Crc32(nullptr, 0) : Crc32(reinterpret_cast<const uint8_t *>(e.data()), e.size() * sizeof(Entry));
    };

    const auto writeTable = [&](uint32_t version, uint64_t imageBase, uint32_t sizeOfImage, const std::vector<Entry> &entries, uint32_t declaredCount) {
        Header header {};
        memcpy(header.magic, "FWPATTBL", 8);
        header.version           = version;
        header.entryCount        = declaredCount;
        header.targetImageBase   = imageBase;
        header.targetSizeOfImage = sizeOfImage;
        header.entriesCrc        = entryBytes(entries);

        std::ofstream out(tempPath, std::ios::binary | std::ios::trunc);
        out.write(reinterpret_cast<const char *>(&header), sizeof(header));
        if (!entries.empty()) {
            out.write(reinterpret_cast<const char *>(entries.data()), entries.size() * sizeof(Entry));
        }
    };

    const auto writeValid = [&](uint64_t imageBase, uint32_t sizeOfImage) {
        writeTable(2, imageBase, sizeOfImage, oneEntry, 1);
    };

    IT("accepts a v2 table whose 64-bit image base matches the loaded module", {
        writeValid(selfBase, selfSize);
        UEQUALS(hook::load_pattern_table(tempPath), size_t {1});
    });

    IT("rejects a table that records only the low dword of an x64 image base", {
        // The exact shape the old PE32-offset parse produced is a truncated base: a table
        // claiming 0x40000000 for an image based at 0x140000000 must not be trusted.
        if (selfBase > 0xFFFFFFFFull) {
            writeValid(selfBase & 0xFFFFFFFFull, selfSize);
            UEQUALS(hook::load_pattern_table(tempPath), size_t {0});
        }
    });

    IT("rejects a table built for a different image", {
        writeValid(selfBase, selfSize + 0x1000);
        UEQUALS(hook::load_pattern_table(tempPath), size_t {0});
    });

    IT("still recognises the legacy v1 layout", {
        HeaderV1 header {};
        memcpy(header.magic, "FWPATTBL", 8);
        header.version           = 1;
        header.entryCount        = 1;
        header.targetSizeOfImage = selfSize;
        header.targetImageBase   = static_cast<uint32_t>(selfBase);
        header.entriesCrc        = entryBytes(oneEntry);

        std::ofstream out(tempPath, std::ios::binary | std::ios::trunc);
        out.write(reinterpret_cast<const char *>(&header), sizeof(header));
        out.write(reinterpret_cast<const char *>(oneEntry.data()), oneEntry.size() * sizeof(Entry));
        out.close();

        // v1 can only describe an image based below 4 GB, which is the 32-bit case it exists
        // for. On an x64 host the widened base check correctly refuses it rather than matching
        // a truncated base — that refusal is the bug this whole module guards.
        UEQUALS(hook::load_pattern_table(tempPath), selfBase > 0xFFFFFFFFull ? size_t {0} : size_t {1});
    });

    IT("rejects an unknown format version", {
        writeTable(99, selfBase, selfSize, oneEntry, 1);
        UEQUALS(hook::load_pattern_table(tempPath), size_t {0});
    });

    IT("rejects entry bytes altered after the checksum was taken", {
        writeValid(selfBase, selfSize);
        {
            std::fstream f(tempPath, std::ios::binary | std::ios::in | std::ios::out);
            f.seekp(sizeof(Header));
            const uint64_t tampered = 0xDEADBEEFDEADBEEFull;
            f.write(reinterpret_cast<const char *>(&tampered), sizeof(tampered));
        }
        UEQUALS(hook::load_pattern_table(tempPath), size_t {0});
    });

    IT("rejects a table that declares more entries than it carries", {
        writeTable(2, selfBase, selfSize, {}, 4);
        UEQUALS(hook::load_pattern_table(tempPath), size_t {0});
    });

    IT("reports nothing seeded when the table is absent", {
        std::filesystem::remove(absentPath);
        UEQUALS(hook::load_pattern_table(absentPath), size_t {0});
    });

    IT("sizes the preferred-base window from the module's real SizeOfImage", {
        // The window used to stop at base + 96 MB, so a resolved address past that line was
        // handed back unrelocated. A 444 MB image keeps four fifths of its RVA space up there.
        hook::set_base(reinterpret_cast<uintptr_t>(self));
        UEQUALS(hook::preferredImageEnd, hook::kPreferredImageBase + selfSize);
    });

    std::filesystem::remove(tempPath);
});
