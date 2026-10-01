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

#include "utils/hooking/hooking_patterns.h"

#include <cstdint>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

// MODULE() is a macro, so the record layout has to live at file scope. It mirrors what
// hooking_patterns.cpp reads and appends: a packed 64-bit hash then a pointer-sized address.
namespace FwPatternHintsUT {
    struct Record {
        uint64_t hash;
        uintptr_t address;
    };

    static_assert(sizeof(Record) == sizeof(uint64_t) + sizeof(uintptr_t), "a hints record carries no padding");

    inline void WriteRecords(const std::string &path, const std::vector<Record> &records) {
        std::ofstream out(path, std::ios::binary | std::ios::trunc);
        for (const auto &record : records) {
            out.write(reinterpret_cast<const char *>(&record.hash), sizeof(record.hash));
            out.write(reinterpret_cast<const char *>(&record.address), sizeof(record.address));
        }
    }

    inline std::vector<Record> ReadRecords(const std::string &path) {
        std::vector<Record> records;
        std::ifstream in(path, std::ios::binary);
        Record record {};
        while (in.read(reinterpret_cast<char *>(&record.hash), sizeof(record.hash)) && in.read(reinterpret_cast<char *>(&record.address), sizeof(record.address))) {
            records.push_back(record);
        }
        return records;
    }
} // namespace FwPatternHintsUT

// Guards fw_hints.dat, the scan cache the pattern engine keeps in the working directory. Two
// faults compounded there. Loading re-entered itself through pattern::hint() and recursed until
// the CRT ran out of stdio streams, so every level read the whole file again. And every scan
// appended its matches whether or not they were already cached, so the file grew by a full set
// of patterns per boot. A KCDC install reached 374k records for 1,110 distinct pairs and spent
// ~40 s of every boot before the first module ran. These tests pin the file contract; the
// load itself is a pure function of the file, which is what keeps it from re-entering.
MODULE(pattern_hints, {
    using namespace FwPatternHintsUT;

    const auto workDir   = std::filesystem::temp_directory_path() / "fw_pattern_hints_ut";
    const auto tempPath  = (workDir / "hints.bin").string();
    const auto savedPath = std::filesystem::current_path();
    std::filesystem::remove_all(workDir);
    std::filesystem::create_directories(workDir);

    IT("collapses a file of repeated records to one entry per pair", {
        // The shape the old append produced: the same set written once per boot.
        std::vector<Record> records;
        for (int boot = 0; boot < 1000; ++boot) {
            records.push_back({0x1111111111111111ull, 0x140001000});
            records.push_back({0x2222222222222222ull, 0x140002000});
            records.push_back({0x3333333333333333ull, 0x140003000});
        }
        WriteRecords(tempPath, records);

        const auto hints = hook::load_hints(tempPath);
        UEQUALS(hints.size(), size_t {3});
        UEQUALS(hints.count(0x2222222222222222ull), size_t {1});
        UEQUALS(hints.find(0x2222222222222222ull)->second, uintptr_t {0x140002000});
    });

    IT("keeps every distinct address a pattern matched", {
        // A pattern with several matches caches each of them under the one hash.
        WriteRecords(tempPath, {{0x4444444444444444ull, 0x140004000}, {0x4444444444444444ull, 0x140005000}, {0x4444444444444444ull, 0x140004000}});

        const auto hints = hook::load_hints(tempPath);
        UEQUALS(hints.size(), size_t {2});
        UEQUALS(hints.count(0x4444444444444444ull), size_t {2});
    });

    IT("drops a trailing partial record instead of inventing an entry", {
        // The old eof()-driven loop ran once more after the last full record and inserted
        // whatever the failed reads left in its locals.
        WriteRecords(tempPath, {{0x5555555555555555ull, 0x140006000}, {0x6666666666666666ull, 0x140007000}});
        {
            std::ofstream out(tempPath, std::ios::binary | std::ios::app);
            const uint32_t stray = 0xDEADBEEF;
            out.write(reinterpret_cast<const char *>(&stray), sizeof(stray));
        }

        const auto hints = hook::load_hints(tempPath);
        UEQUALS(hints.size(), size_t {2});
        UEQUALS(hints.count(0x5555555555555555ull), size_t {1});
        UEQUALS(hints.count(0x6666666666666666ull), size_t {1});
    });

    IT("reads a missing file as no hints", {
        std::filesystem::remove(tempPath);
        UEQUALS(hook::load_hints(tempPath).size(), size_t {0});
    });

    IT("persists a scanned match once, however often it is scanned", {
        // Scans append to fw_hints.dat in the working directory, so they run from a scratch
        // one. A range pattern never consults the hints, so both scans really scan; only the
        // first may write.
        std::filesystem::current_path(workDir);
        std::filesystem::remove("fw_hints.dat");

        const std::vector<uint8_t> haystack = {0x90, 0x90, 0x4D, 0x48, 0x75, 0x62, 0x13, 0x37, 0xC0, 0xDE, 0x90, 0x90};
        const auto begin                    = reinterpret_cast<uintptr_t>(haystack.data());
        const auto end                      = begin + haystack.size();
        const auto expected                 = reinterpret_cast<uintptr_t>(haystack.data() + 2);

        UEQUALS(hook::range_pattern(begin, end, "4D 48 75 62 13 37 C0 DE").get_first<uint8_t>(), haystack.data() + 2);
        const auto afterFirst = ReadRecords("fw_hints.dat");

        UEQUALS(hook::range_pattern(begin, end, "4D 48 75 62 13 37 C0 DE").get_first<uint8_t>(), haystack.data() + 2);
        const auto afterSecond = ReadRecords("fw_hints.dat");

        std::filesystem::current_path(savedPath);

        UEQUALS(afterFirst.size(), size_t {1});
        UEQUALS(afterFirst[0].address, expected);
        UEQUALS(afterSecond.size(), size_t {1});
    });

    std::filesystem::current_path(savedPath);
    std::filesystem::remove_all(workDir);
});
