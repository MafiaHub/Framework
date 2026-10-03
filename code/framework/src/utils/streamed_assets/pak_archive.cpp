/*
 * MafiaHub OSS license
 * Copyright (c) 2021-2026, MafiaHub. All rights reserved.
 *
 * This file comes from MafiaHub, hosted at https://github.com/MafiaHub/Framework.
 * See LICENSE file in the source repository for information regarding licensing.
 */

#include "pak_archive.h"

#include <miniz.h>

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <unordered_set>

namespace Framework::Utils::StreamedAssets {
    namespace {
        // Every entry is stamped 2020-01-01 00:00. Any fixed instant does; a moving one would
        // change the hash of an unchanged pak on every build.
        constexpr std::uint16_t kDosDate = ((2020 - 1980) << 9) | (1 << 5) | 1;
        constexpr std::uint16_t kDosTime = 0;

        constexpr std::uint32_t kLocalHeaderSignature   = 0x04034B50;
        constexpr std::uint32_t kCentralHeaderSignature = 0x02014B50;
        constexpr std::uint32_t kEndOfCentralDirSig     = 0x06054B50;
        constexpr std::size_t kLocalHeaderSize          = 30;
        constexpr std::size_t kCentralHeaderSize        = 46;
        constexpr std::size_t kEndOfCentralDirSize      = 22;

        void WriteU16(unsigned char *p, std::uint16_t value) {
            p[0] = static_cast<unsigned char>(value);
            p[1] = static_cast<unsigned char>(value >> 8);
        }

        void WriteU32(unsigned char *p, std::uint32_t value) {
            WriteU16(p, static_cast<std::uint16_t>(value));
            WriteU16(p + 2, static_cast<std::uint16_t>(value >> 16));
        }

        std::uint16_t ReadU16(const unsigned char *p) {
            return static_cast<std::uint16_t>(p[0] | (p[1] << 8));
        }

        std::uint32_t ReadU32(const unsigned char *p) {
            return static_cast<std::uint32_t>(p[0]) | (static_cast<std::uint32_t>(p[1]) << 8) | (static_cast<std::uint32_t>(p[2]) << 16) | (static_cast<std::uint32_t>(p[3]) << 24);
        }

        bool ReadFile(const std::string &path, std::vector<unsigned char> &out, std::string &error) {
            std::error_code code;
            const std::uintmax_t size = std::filesystem::file_size(path, code);
            if (code) {
                error = "cannot read the size of " + path;
                return false;
            }
            if (size > kMaxEntrySize) {
                error = path + " is larger than an entry may be";
                return false;
            }
            out.resize(static_cast<std::size_t>(size));
            std::ifstream file(path, std::ios::binary);
            if (!file || (size > 0 && !file.read(reinterpret_cast<char *>(out.data()), static_cast<std::streamsize>(size)))) {
                error = "cannot read " + path;
                return false;
            }
            return true;
        }

        // A pak carries no archive comment -- CryEngine parses one of eight bytes or more as its
        // extended header -- so the end-of-central-directory record is the file's last 22 bytes.
        bool HasNoArchiveComment(std::FILE *file, std::uint64_t fileSize) {
            if (fileSize < kEndOfCentralDirSize) {
                return false;
            }
            unsigned char record[kEndOfCentralDirSize];
            if (_fseeki64(file, static_cast<long long>(fileSize - kEndOfCentralDirSize), SEEK_SET) != 0 || std::fread(record, 1, sizeof(record), file) != sizeof(record)) {
                return false;
            }
            return ReadU32(record) == kEndOfCentralDirSig && ReadU16(record + 20) == 0 && ReadU16(record + 4) == 0 && ReadU16(record + 6) == 0;
        }

        // What an engine reading the local header for the data offset compares: CRC, sizes,
        // method, name.
        bool LocalHeaderAgrees(std::FILE *file, const mz_zip_archive_file_stat &stat, std::string &error) {
            unsigned char header[kLocalHeaderSize];
            if (_fseeki64(file, static_cast<long long>(stat.m_local_header_ofs), SEEK_SET) != 0 || std::fread(header, 1, sizeof(header), file) != sizeof(header)) {
                error = "cannot read the local header of " + std::string(stat.m_filename);
                return false;
            }
            const std::size_t nameLength = std::char_traits<char>::length(stat.m_filename);
            if (ReadU32(header) != kLocalHeaderSignature || ReadU16(header + 8) != stat.m_method || ReadU32(header + 14) != stat.m_crc32 || ReadU32(header + 18) != stat.m_comp_size || ReadU32(header + 22) != stat.m_uncomp_size || ReadU16(header + 26) != nameLength || ReadU16(header + 28) != 0) {
                error = "the local header of " + std::string(stat.m_filename) + " disagrees with the central directory";
                return false;
            }
            std::string name(nameLength, '\0');
            if (std::fread(name.data(), 1, nameLength, file) != nameLength || name != stat.m_filename) {
                error = "the local name of " + std::string(stat.m_filename) + " disagrees with the central directory";
                return false;
            }
            return true;
        }
    } // namespace

    bool PakArchive::Write(const std::string &outputPath, std::vector<PakSourceEntry> entries, const Policy &policy, std::string &error) {
        if (entries.empty() || entries.size() > kMaxPakEntries) {
            error = "a pak needs between 1 and 65535 entries";
            return false;
        }
        std::sort(entries.begin(), entries.end(), [](const PakSourceEntry &a, const PakSourceEntry &b) { return a.path < b.path; });
        for (std::size_t i = 1; i < entries.size(); ++i) {
            if (entries[i].path == entries[i - 1].path) {
                error = "two files map to " + entries[i].path;
                return false;
            }
        }

        std::error_code code;
        std::filesystem::create_directories(std::filesystem::path(outputPath).parent_path(), code);
        std::ofstream out(outputPath, std::ios::binary | std::ios::trunc);
        if (!out) {
            error = "cannot create " + outputPath;
            return false;
        }

        // Written by hand rather than through miniz's writer, which flags every entry with a
        // data descriptor whatever it is asked: this way each header is exactly the subset an
        // engine reads -- version 20, no flags, real sizes in the local header, no extra field.
        struct CentralRecord {
            std::string name;
            std::uint16_t method = 0;
            std::uint32_t crc    = 0;
            std::uint32_t packed = 0;
            std::uint32_t size   = 0;
            std::uint32_t offset = 0;
        };
        std::vector<CentralRecord> records;
        records.reserve(entries.size());

        std::uint64_t position = 0;
        std::vector<unsigned char> data;
        for (const PakSourceEntry &entry : entries) {
            if (!ReadFile(entry.sourceFile, data, error)) {
                return false;
            }

            CentralRecord record;
            record.name   = entry.path;
            record.crc    = static_cast<std::uint32_t>(mz_crc32(MZ_CRC32_INIT, data.data(), data.size()));
            record.size   = static_cast<std::uint32_t>(data.size());
            record.offset = static_cast<std::uint32_t>(position);

            void *deflated     = nullptr;
            std::size_t packed = 0;
            if (!policy.PrefersStore(entry.path) && !data.empty()) {
                deflated = tdefl_compress_mem_to_heap(data.data(), data.size(), &packed, static_cast<int>(tdefl_create_comp_flags_from_zip_params(MZ_DEFAULT_LEVEL, -MZ_DEFAULT_WINDOW_BITS, MZ_DEFAULT_STRATEGY)));
            }
            // Deflate only where it pays; a stored entry is also the cheaper one to stream.
            const bool useDeflate = deflated != nullptr && packed < data.size();
            record.method         = useDeflate ? MZ_DEFLATED : 0;
            record.packed         = useDeflate ? static_cast<std::uint32_t>(packed) : record.size;

            unsigned char header[kLocalHeaderSize] = {};
            WriteU32(header, kLocalHeaderSignature);
            WriteU16(header + 4, 20);
            WriteU16(header + 8, record.method);
            WriteU16(header + 10, kDosTime);
            WriteU16(header + 12, kDosDate);
            WriteU32(header + 14, record.crc);
            WriteU32(header + 18, record.packed);
            WriteU32(header + 22, record.size);
            WriteU16(header + 26, static_cast<std::uint16_t>(record.name.size()));
            out.write(reinterpret_cast<const char *>(header), sizeof(header));
            out.write(record.name.data(), static_cast<std::streamsize>(record.name.size()));
            if (useDeflate) {
                out.write(static_cast<const char *>(deflated), static_cast<std::streamsize>(packed));
            }
            else if (!data.empty()) {
                out.write(reinterpret_cast<const char *>(data.data()), static_cast<std::streamsize>(data.size()));
            }
            mz_free(deflated);

            position += sizeof(header) + record.name.size() + record.packed;
            if (position > kMaxPakSize) {
                error = outputPath + " would be 2 GB or larger, which CryPak refuses";
                return false;
            }
            records.push_back(std::move(record));
        }

        const std::uint64_t directoryOffset = position;
        for (const CentralRecord &record : records) {
            unsigned char header[kCentralHeaderSize] = {};
            WriteU32(header, kCentralHeaderSignature);
            WriteU16(header + 4, 20);
            WriteU16(header + 6, 20);
            WriteU16(header + 10, record.method);
            WriteU16(header + 12, kDosTime);
            WriteU16(header + 14, kDosDate);
            WriteU32(header + 16, record.crc);
            WriteU32(header + 20, record.packed);
            WriteU32(header + 24, record.size);
            WriteU16(header + 28, static_cast<std::uint16_t>(record.name.size()));
            WriteU32(header + 42, record.offset);
            out.write(reinterpret_cast<const char *>(header), sizeof(header));
            out.write(record.name.data(), static_cast<std::streamsize>(record.name.size()));
            position += sizeof(header) + record.name.size();
        }

        unsigned char end[kEndOfCentralDirSize] = {};
        WriteU32(end, kEndOfCentralDirSig);
        WriteU16(end + 8, static_cast<std::uint16_t>(records.size()));
        WriteU16(end + 10, static_cast<std::uint16_t>(records.size()));
        WriteU32(end + 12, static_cast<std::uint32_t>(position - directoryOffset));
        WriteU32(end + 16, static_cast<std::uint32_t>(directoryOffset));
        out.write(reinterpret_cast<const char *>(end), sizeof(end));
        position += sizeof(end);

        out.close();
        if (!out || position > kMaxPakSize) {
            error = "cannot write " + outputPath;
            return false;
        }
        return true;
    }

    bool PakArchive::Validate(const std::string &pakPath, const Policy &policy, std::string_view lane, std::string_view resource, std::vector<PakEntry> &entries, std::string &error) {
        entries.clear();

        std::error_code code;
        const std::uintmax_t fileSize = std::filesystem::file_size(pakPath, code);
        if (code || fileSize == 0 || fileSize > kMaxPakSize) {
            error = "the pak is missing, empty or 2 GB or larger";
            return false;
        }

        std::FILE *file = nullptr;
        if (fopen_s(&file, pakPath.c_str(), "rb") != 0 || file == nullptr) {
            error = "cannot open the pak";
            return false;
        }
        if (!HasNoArchiveComment(file, fileSize)) {
            std::fclose(file);
            error = "the pak carries an archive comment or spans volumes";
            return false;
        }

        // miniz takes the stream's current position as the archive's first byte.
        mz_zip_archive zip {};
        if (_fseeki64(file, 0, SEEK_SET) != 0 || !mz_zip_reader_init_cfile(&zip, file, fileSize, 0)) {
            std::fclose(file);
            error = "the pak is not a readable zip";
            return false;
        }

        bool ok                       = true;
        const mz_uint count           = mz_zip_reader_get_num_files(&zip);
        std::unordered_set<std::string> seen;
        if (count == 0 || count > kMaxPakEntries || mz_zip_is_zip64(&zip)) {
            error = "the pak has no entries, too many, or needs ZIP64";
            ok    = false;
        }

        for (mz_uint i = 0; ok && i < count; ++i) {
            mz_zip_archive_file_stat stat {};
            if (!mz_zip_reader_file_stat(&zip, i, &stat)) {
                error = "cannot read entry " + std::to_string(i);
                ok    = false;
                break;
            }

            std::string normalized;
            std::string reason;
            if (stat.m_is_directory || !policy.NormalizeEntry(lane, resource, stat.m_filename, normalized, reason)) {
                error = std::string(stat.m_filename) + ": " + (stat.m_is_directory ? "directory entries are not written" : reason);
                ok    = false;
                break;
            }
            if (normalized != stat.m_filename) {
                error = std::string(stat.m_filename) + ": entry is not in normalised form";
                ok    = false;
                break;
            }
            // Bit 0 encryption, bit 3 a data descriptor (the local sizes would read zero).
            if (stat.m_is_encrypted || (stat.m_bit_flag & 0x9) != 0 || (stat.m_method != 0 && stat.m_method != MZ_DEFLATED) || (stat.m_version_needed & 0xFF) > 20) {
                error = std::string(stat.m_filename) + ": entry is encrypted, deferred, or not store or deflate (flags " + std::to_string(stat.m_bit_flag) + ", method " + std::to_string(stat.m_method) + ", version " + std::to_string(stat.m_version_needed) + ")";
                ok    = false;
                break;
            }
            if (stat.m_uncomp_size > kMaxEntrySize || (stat.m_method == 0 && stat.m_comp_size != stat.m_uncomp_size)) {
                error = std::string(stat.m_filename) + ": entry size is out of range";
                ok    = false;
                break;
            }
            if (!seen.insert(normalized).second) {
                error = std::string(stat.m_filename) + ": entry appears twice";
                ok    = false;
                break;
            }
            if (!LocalHeaderAgrees(file, stat, error)) {
                ok = false;
                break;
            }
            entries.push_back({std::move(normalized), static_cast<std::uint32_t>(stat.m_crc32), static_cast<std::uint64_t>(stat.m_uncomp_size)});
        }

        mz_zip_reader_end(&zip);
        std::fclose(file);
        if (!ok) {
            entries.clear();
        }
        return ok;
    }
} // namespace Framework::Utils::StreamedAssets
