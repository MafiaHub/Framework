/*
 * MafiaHub OSS license
 * Copyright (c) 2021-2023, MafiaHub. All rights reserved.
 *
 * This file comes from MafiaHub, hosted at https://github.com/MafiaHub/Framework.
 * See LICENSE file in the source repository for information regarding licensing.
 */

#include "image_snapshot.h"

#include "logging/logger.h"

#include <cmath>
#include <cstring>
#include <fstream>

namespace Framework::Launcher::Loaders {
    namespace {
        constexpr uint32_t kSnapshotMagic   = 0x53495746; // "FWIS"
        constexpr uint32_t kSnapshotVersion = 1;

        struct FileHeader {
            uint32_t magic;
            uint32_t version;
            uint32_t executableChecksum;
            uint32_t sectionCount;
        };

        struct SectionHeader {
            uint32_t virtualAddress;
            uint32_t size;
        };

        constexpr size_t kPageSize = 0x1000;

        // Bits per byte above which a page is ciphertext rather than compiled code. Encrypted pages
        // sit at the very top of the scale (8.00 measured); real code stays well under 7.
        constexpr double kEncryptedPageEntropy = 7.7;

        double PageEntropy(const uint8_t *page) {
            size_t frequency[256] = {};
            for (size_t index = 0; index < kPageSize; ++index) {
                ++frequency[page[index]];
            }

            double entropy = 0.0;
            for (const auto count : frequency) {
                if (count == 0) {
                    continue;
                }

                const auto probability = static_cast<double>(count) / kPageSize;
                entropy -= probability * std::log2(probability);
            }
            return entropy;
        }

        const IMAGE_NT_HEADERS *NtHeadersOf(const uint8_t *image, size_t size) {
            if (size < sizeof(IMAGE_DOS_HEADER)) {
                return nullptr;
            }

            const auto dosHeader = reinterpret_cast<const IMAGE_DOS_HEADER *>(image);
            if (dosHeader->e_magic != IMAGE_DOS_SIGNATURE || static_cast<size_t>(dosHeader->e_lfanew) + sizeof(IMAGE_NT_HEADERS) > size) {
                return nullptr;
            }

            const auto ntHeaders = reinterpret_cast<const IMAGE_NT_HEADERS *>(image + dosHeader->e_lfanew);
            return ntHeaders->Signature == IMAGE_NT_SIGNATURE ? ntHeaders : nullptr;
        }
    } // namespace

    ImageSnapshot::ImageSnapshot(std::filesystem::path cachePath, uint32_t executableChecksum): _cachePath(std::move(cachePath)), _executableChecksum(executableChecksum) {}

    bool ImageSnapshot::Load() const {
        if (_loaded) {
            return !_sections.empty();
        }

        _loaded = true;

        std::ifstream file(_cachePath, std::ios::binary);
        if (!file.is_open()) {
            return false;
        }

        FileHeader header {};
        file.read(reinterpret_cast<char *>(&header), sizeof(header));
        if (!file.good() || header.magic != kSnapshotMagic || header.version != kSnapshotVersion) {
            return false;
        }

        // A game update changes the executable, and with it every offset the snapshot holds
        if (header.executableChecksum != _executableChecksum) {
            Logging::GetLogger(FRAMEWORK_INNER_LAUNCHER)->info("The cached image snapshot belongs to a different build of the game, it will be captured again");
            return false;
        }

        for (uint32_t index = 0; index < header.sectionCount; ++index) {
            SectionHeader sectionHeader {};
            file.read(reinterpret_cast<char *>(&sectionHeader), sizeof(sectionHeader));
            if (!file.good()) {
                _sections.clear();
                return false;
            }

            Section section;
            section.virtualAddress = sectionHeader.virtualAddress;
            section.data.resize(sectionHeader.size);
            file.read(reinterpret_cast<char *>(section.data.data()), sectionHeader.size);
            if (!file.good()) {
                _sections.clear();
                return false;
            }

            _sections.push_back(std::move(section));
        }

        return !_sections.empty();
    }

    bool ImageSnapshot::Store() const {
        std::error_code ec;
        std::filesystem::create_directories(_cachePath.parent_path(), ec);

        // Write beside the cache and rename, so an interrupted capture cannot leave a half file
        auto temporaryPath = _cachePath;
        temporaryPath += ".tmp";

        {
            std::ofstream file(temporaryPath, std::ios::binary | std::ios::trunc);
            if (!file.is_open()) {
                return false;
            }

            const FileHeader header {kSnapshotMagic, kSnapshotVersion, _executableChecksum, static_cast<uint32_t>(_sections.size())};
            file.write(reinterpret_cast<const char *>(&header), sizeof(header));

            for (const auto &section : _sections) {
                const SectionHeader sectionHeader {section.virtualAddress, static_cast<uint32_t>(section.data.size())};
                file.write(reinterpret_cast<const char *>(&sectionHeader), sizeof(sectionHeader));
                file.write(reinterpret_cast<const char *>(section.data.data()), section.data.size());
            }

            if (!file.good()) {
                file.close();
                std::filesystem::remove(temporaryPath, ec);
                return false;
            }
        }

        std::filesystem::remove(_cachePath, ec);
        std::filesystem::rename(temporaryPath, _cachePath, ec);
        if (ec) {
            std::filesystem::remove(temporaryPath, ec);
            return false;
        }

        return true;
    }

    bool ImageSnapshot::IsAvailable() const {
        return Load();
    }

    bool ImageSnapshot::Apply(HMODULE module) const {
        if (!Load()) {
            return false;
        }

        const auto base = reinterpret_cast<uint8_t *>(module);
        for (const auto &section : _sections) {
            const auto target = base + section.virtualAddress;

            DWORD oldProtect = 0;
            if (!VirtualProtect(target, section.data.size(), PAGE_EXECUTE_READWRITE, &oldProtect)) {
                Logging::GetLogger(FRAMEWORK_INNER_LAUNCHER)->error("Could not make the mapped section at {:#x} writable for the image snapshot", section.virtualAddress);
                return false;
            }

            std::memcpy(target, section.data.data(), section.data.size());

            DWORD restored = 0;
            VirtualProtect(target, section.data.size(), oldProtect, &restored);
        }

        FlushInstructionCache(GetCurrentProcess(), nullptr, 0);
        Logging::GetLogger(FRAMEWORK_INNER_LAUNCHER)->info("Applied the cached image snapshot ({} sections) over the mapped game", _sections.size());
        return true;
    }

    bool ImageSnapshot::CaptureFrom(HANDLE process, const std::vector<uint8_t> &sourceImage) {
        const auto logger    = Logging::GetLogger(FRAMEWORK_INNER_LAUNCHER);
        const auto ntHeaders = NtHeadersOf(sourceImage.data(), sourceImage.size());
        if (!ntHeaders) {
            return false;
        }

        // The wrapped titles this exists for have their relocations stripped, so the authorised
        // process maps at the same base the mapped image will use and section RVAs line up.
        const auto base    = static_cast<uintptr_t>(ntHeaders->OptionalHeader.ImageBase);
        auto section       = IMAGE_FIRST_SECTION(ntHeaders);
        const auto endOfIt = section + ntHeaders->FileHeader.NumberOfSections;

        std::vector<Section> captured;
        size_t encryptedPages = 0;
        size_t pendingPages   = 0;

        for (; section != endOfIt; ++section) {
            // The wrapper decrypts code; everything else the mapped image already has from disk
            if (!(section->Characteristics & IMAGE_SCN_MEM_EXECUTE) || section->SizeOfRawData == 0) {
                continue;
            }

            if (static_cast<size_t>(section->PointerToRawData) + section->SizeOfRawData > sourceImage.size()) {
                return false;
            }

            Section entry;
            entry.virtualAddress = section->VirtualAddress;
            entry.data.resize(section->SizeOfRawData);

            SIZE_T read = 0;
            if (!ReadProcessMemory(process, reinterpret_cast<LPCVOID>(base + section->VirtualAddress), entry.data.data(), entry.data.size(), &read) || read != entry.data.size()) {
                logger->warn("Could not read the authorised game's section at {:#x}", section->VirtualAddress);
                return false;
            }

            // The wrapper decrypts its pages progressively, so a capture taken too early bakes
            // half-decrypted code into the cache and the game runs into ciphertext. Only the pages
            // that are ciphertext on disk have to come from memory, and every one of them must
            // have changed before the capture is worth keeping.
            for (size_t offset = 0; offset + kPageSize <= entry.data.size(); offset += kPageSize) {
                const auto filePage = sourceImage.data() + section->PointerToRawData + offset;
                if (PageEntropy(filePage) < kEncryptedPageEntropy) {
                    continue;
                }

                ++encryptedPages;
                if (std::memcmp(entry.data.data() + offset, filePage, kPageSize) == 0) {
                    ++pendingPages;
                }
            }

            captured.push_back(std::move(entry));
        }

        if (captured.empty()) {
            return false;
        }

        if (pendingPages > 0) {
            logger->debug("The wrapper has decrypted {} of {} pages so far, waiting for the rest", encryptedPages - pendingPages, encryptedPages);
            return false;
        }

        _sections = std::move(captured);
        _loaded   = true;

        if (!Store()) {
            logger->warn("Captured the decrypted game code but could not write it to {}", _cachePath.string());
            return false;
        }

        logger->info("Captured {} code sections ({} decrypted pages) from the authorised game into {}", _sections.size(), encryptedPages, _cachePath.string());
        return true;
    }
} // namespace Framework::Launcher::Loaders
