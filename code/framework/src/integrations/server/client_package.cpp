/*
 * MafiaHub OSS license
 * Copyright (c) 2021-2026, MafiaHub. All rights reserved.
 *
 * This file comes from MafiaHub, hosted at https://github.com/MafiaHub/Framework.
 * See LICENSE file in the source repository for information regarding licensing.
 */

#include "client_package.h"

#include "asset_file_server.h"

#include <scripting/resource/resource_packager.h>
#include <utils/package/package.h>

#include <fstream>
#include <iterator>
#include <string>

namespace Framework::Integrations::Server {
    namespace {
        // Rename where the platform allows it, which keeps a reader of the old file on the old bytes. Windows
        // refuses to replace a file a download has open: there the bytes are copied over in place, as
        // packaging always did before.
        bool Replace(const std::filesystem::path &from, const std::filesystem::path &to) {
            std::error_code code;
            std::filesystem::rename(from, to, code);
            if (!code) {
                return true;
            }
            // Read whole before the target is opened: a source that cannot be read leaves the target as it was.
            std::string bytes;
            {
                std::ifstream in(from, std::ios::binary);
                if (!in) {
                    return false;
                }
                bytes.assign(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
                if (in.bad()) {
                    return false;
                }
            }
            {
                std::ofstream out(to, std::ios::binary | std::ios::trunc);
                out.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
                if (!out.good()) {
                    return false;
                }
            }
            std::filesystem::remove(from, code);
            return true;
        }
    } // namespace

    ClientPackage BuildClientPackage(const ClientPackageRequest &request) {
        ClientPackage package;
        package.name   = request.name;
        package.served = request.stagingDir / (request.name + Utils::Package::kExtension);

        Scripting::PackagedResource packaged;
        if (!Scripting::ResourcePackager::Package(request.name, request.path, request.manifest, &request.key, packaged, package.error, request.serverOnlyResources)) {
            if (package.error.empty()) {
                package.error = "packaging failed";
            }
            return package;
        }
        package.sha256    = packaged.sha256;
        package.size      = packaged.blob.size();
        package.fileCount = packaged.fileCount;

        std::error_code code;
        if (std::filesystem::exists(package.served, code) && !code && Utils::Crypto::ConstantTimeEquals(Utils::Crypto::Sha256FileHex(package.served.string()), packaged.sha256)) {
            // The same bytes are served already; only a missing chunk list is made up for.
            if (!AssetFileServer::HasCurrentChunkList(package.served)) {
                AssetFileServer::WriteChunkList(package.served, AssetFileServer::ChunkListPath(package.served));
            }
            package.built = true;
            return package;
        }

        const std::filesystem::path pending = package.served.string() + "." + std::to_string(request.build) + ".next";
        {
            std::ofstream out(pending, std::ios::binary | std::ios::trunc);
            out.write(packaged.blob.data(), static_cast<std::streamsize>(packaged.blob.size()));
            if (!out.good()) {
                package.error = "could not write '" + pending.string() + "'";
                out.close();
                std::filesystem::remove(pending, code);
                return package;
            }
        }
        // Without it the package is still served: publishing makes the list then, on the tick thread.
        AssetFileServer::WriteChunkList(pending, AssetFileServer::ChunkListPath(pending));
        package.pending = pending;
        package.built   = true;
        return package;
    }

    bool CommitClientPackage(ClientPackage &package) {
        if (!package.built) {
            return false;
        }
        if (package.pending.empty()) {
            return true;
        }
        const std::filesystem::path pendingList = AssetFileServer::ChunkListPath(package.pending);
        if (!Replace(package.pending, package.served)) {
            return false;
        }
        std::error_code code;
        const std::filesystem::path servedList = AssetFileServer::ChunkListPath(package.served);
        if (std::filesystem::exists(pendingList, code) && Replace(pendingList, servedList)) {
            // Never older than the file it describes, whichever way the two got here.
            std::filesystem::last_write_time(servedList, std::filesystem::file_time_type::clock::now(), code);
        }
        package.pending.clear();
        return true;
    }

    void DiscardClientPackage(ClientPackage &package) {
        if (package.pending.empty()) {
            return;
        }
        std::error_code code;
        std::filesystem::remove(AssetFileServer::ChunkListPath(package.pending), code);
        std::filesystem::remove(package.pending, code);
        package.pending.clear();
    }
} // namespace Framework::Integrations::Server
