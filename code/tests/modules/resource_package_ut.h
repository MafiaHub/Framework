/*
 * MafiaHub OSS license
 * Copyright (c) 2021-2024, MafiaHub. All rights reserved.
 *
 * This file comes from MafiaHub, hosted at https://github.com/MafiaHub/Framework.
 * See LICENSE file in the source repository for information regarding licensing.
 */

#pragma once

#include "gui/resources/directory_provider.h"
#include "integrations/client/resource_packages.h"
#include "scripting/node_engine.h"
#include "scripting/v8_engine.h"
#include "scripting/resource/resource_manager.h"
#include "scripting/resource/resource_packager.h"
#include "utils/crypto.h"
#include "utils/package/package.h"
#include "utils/vfs.h"

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <string>

MODULE(resource_package, {
    // Process singleton; Init is idempotent.
    Framework::Utils::Vfs::Get().Init(nullptr);

    const auto writeFile = [](const std::filesystem::path &path, const char *text) {
        std::filesystem::create_directories(path.parent_path());
        std::ofstream file(path, std::ios::binary | std::ios::trunc);
        file << text;
    };

    IT("generates distinct keys and round-trips hex", {
        bool ok = false;
        const auto a = Framework::Utils::Crypto::GenerateKey(&ok);
        EQUALS(ok, true);
        const auto b = Framework::Utils::Crypto::GenerateKey(&ok);
        EQUALS(ok, true);
        EQUALS(a == b, false);

        const auto hex = Framework::Utils::Crypto::ToHex(a.data(), a.size());
        UEQUALS(hex.size(), Framework::Utils::Crypto::kKeySize * 2);

        Framework::Utils::Crypto::Key decoded {};
        EQUALS(Framework::Utils::Crypto::FromHex(hex, decoded.data(), decoded.size()), true);
        EQUALS(decoded == a, true);

        // Wrong length must be refused rather than silently truncated.
        EQUALS(Framework::Utils::Crypto::FromHex("abcd", decoded.data(), decoded.size()), false);
        EQUALS(Framework::Utils::Crypto::FromHex(std::string(Framework::Utils::Crypto::kKeySize * 2, 'z'), decoded.data(), decoded.size()), false);
    });

    IT("AES-GCM round-trips and rejects a tampered tag", {
        bool ok = false;
        const auto key = Framework::Utils::Crypto::GenerateKey(&ok);
        const auto nonce = Framework::Utils::Crypto::GenerateNonce(&ok);
        EQUALS(ok, true);

        const std::string plain = "console.log('secret');";
        const std::string aad = "header";
        std::string cipher;
        Framework::Utils::Crypto::Tag tag {};
        EQUALS(Framework::Utils::Crypto::Encrypt(key, nonce, aad.data(), aad.size(), plain, cipher, tag), true);
        EQUALS(cipher == plain, false);

        std::string decrypted;
        EQUALS(Framework::Utils::Crypto::Decrypt(key, nonce, aad.data(), aad.size(), cipher, tag, decrypted), true);
        STREQUALS(decrypted.c_str(), plain.c_str());

        // A flipped tag byte must fail authentication.
        Framework::Utils::Crypto::Tag badTag = tag;
        badTag[0] ^= 0xFF;
        EQUALS(Framework::Utils::Crypto::Decrypt(key, nonce, aad.data(), aad.size(), cipher, badTag, decrypted), false);

        // So must different AAD, which is how a rewritten container header is caught.
        const std::string otherAad = "heager";
        EQUALS(Framework::Utils::Crypto::Decrypt(key, nonce, otherAad.data(), otherAad.size(), cipher, tag, decrypted), false);
    });

    IT("packages round-trip through write and open", {
        bool ok = false;
        const auto key = Framework::Utils::Crypto::GenerateKey(&ok);
        EQUALS(ok, true);

        Framework::Utils::Package::Writer writer;
        EQUALS(writer.Add("package.json", "{\"name\":\"demo\"}"), true);
        EQUALS(writer.Add("dist/client.js", "globalThis.x = 1;"), true);
        EQUALS(writer.Add("ui/index.html", "<h1>hi</h1>"), true);
        UEQUALS(writer.GetEntryCount(), 3u);

        std::string blob;
        EQUALS(writer.Build(&key, blob), true);
        EQUALS(Framework::Utils::Package::IsEncrypted(blob), true);

        // The plaintext must not be recoverable by scanning the container.
        EQUALS(blob.find("globalThis.x") == std::string::npos, true);
        EQUALS(blob.find("<h1>hi</h1>") == std::string::npos, true);

        std::string zip, error;
        EQUALS(Framework::Utils::Package::Open(blob, &key, zip, error), true);
        // Decrypting yields a real ZIP for PhysicsFS to parse.
        EQUALS(zip.size() > 4, true);
        EQUALS(zip.compare(0, 2, "PK") == 0, true);
    });

    IT("builds byte-identical containers for identical input", {
        // Guards both the fixed ZIP timestamp and the derived nonce. Testing only the unencrypted
        // path missed a random nonce that gave identical content a new hash on every server
        // restart, re-downloading every resource to every client.
        bool ok = false;
        const auto key = Framework::Utils::Crypto::GenerateKey(&ok);
        EQUALS(ok, true);

        const auto build = [](const Framework::Utils::Crypto::Key *k, const char *body, std::string &out) {
            Framework::Utils::Package::Writer writer;
            writer.Add("dist/client.js", body);
            writer.Add("package.json", "{}");
            return writer.Build(k, out);
        };

        std::string plainFirst, plainSecond;
        EQUALS(build(nullptr, "let a = 1;", plainFirst), true);
        EQUALS(build(nullptr, "let a = 1;", plainSecond), true);
        STREQUALS(Framework::Utils::Crypto::Sha256Hex(plainFirst).c_str(), Framework::Utils::Crypto::Sha256Hex(plainSecond).c_str());

        // The encrypted path must be stable too.
        std::string encFirst, encSecond;
        EQUALS(build(&key, "let a = 1;", encFirst), true);
        EQUALS(build(&key, "let a = 1;", encSecond), true);
        STREQUALS(Framework::Utils::Crypto::Sha256Hex(encFirst).c_str(), Framework::Utils::Crypto::Sha256Hex(encSecond).c_str());

        // Changed content must still change the container, and a different key must too.
        std::string changed;
        EQUALS(build(&key, "let a = 2;", changed), true);
        EQUALS(Framework::Utils::Crypto::Sha256Hex(changed) == Framework::Utils::Crypto::Sha256Hex(encFirst), false);

        const auto otherKey = Framework::Utils::Crypto::GenerateKey(&ok);
        std::string otherKeyBlob;
        EQUALS(build(&otherKey, "let a = 1;", otherKeyBlob), true);
        EQUALS(Framework::Utils::Crypto::Sha256Hex(otherKeyBlob) == Framework::Utils::Crypto::Sha256Hex(encFirst), false);

        // Distinct payloads must not share a derived nonce.
        const auto n1 = Framework::Utils::Crypto::DeriveNonce(key, "payload one");
        const auto n2 = Framework::Utils::Crypto::DeriveNonce(key, "payload two");
        const auto n1again = Framework::Utils::Crypto::DeriveNonce(key, "payload one");
        EQUALS(n1 == n2, false);
        EQUALS(n1 == n1again, true);
        EQUALS(Framework::Utils::Crypto::DeriveNonce(otherKey, "payload one") == n1, false);
    });

    IT("rejects a wrong key, a tampered body, and a truncated container", {
        bool ok = false;
        const auto key = Framework::Utils::Crypto::GenerateKey(&ok);
        const auto wrongKey = Framework::Utils::Crypto::GenerateKey(&ok);
        EQUALS(ok, true);

        Framework::Utils::Package::Writer writer;
        EQUALS(writer.Add("dist/client.js", "let a = 1;"), true);
        std::string blob;
        EQUALS(writer.Build(&key, blob), true);

        std::string zip, error;
        EQUALS(Framework::Utils::Package::Open(blob, &wrongKey, zip, error), false);
        EQUALS(error.empty(), false);

        // Flipping a payload byte must fail the tag, not surface altered script bytes.
        std::string tampered = blob;
        tampered[tampered.size() - 1] ^= 0x01;
        EQUALS(Framework::Utils::Package::Open(tampered, &key, zip, error), false);

        // Rewriting the header must fail too: it is authenticated as GCM additional data.
        std::string reheadered = blob;
        reheadered[40] ^= 0x01; // payloadSize
        EQUALS(Framework::Utils::Package::Open(reheadered, &key, zip, error), false);

        EQUALS(Framework::Utils::Package::Open(blob.substr(0, 20), &key, zip, error), false);
        EQUALS(Framework::Utils::Package::Open(std::string("not a package at all"), &key, zip, error), false);

        // An encrypted container must not be readable without a key.
        EQUALS(Framework::Utils::Package::Open(blob, nullptr, zip, error), false);
    });

    IT("refuses package paths that escape the resource root", {
        Framework::Utils::Package::Writer writer;
        EQUALS(writer.Add("../evil.js", "x"), false);
        EQUALS(writer.Add("a/../../evil.js", "x"), false);
        EQUALS(writer.Add("/etc/passwd", "x"), false);
        EQUALS(writer.Add("", "x"), false);
        UEQUALS(writer.GetEntryCount(), 0u);

        // Normalisation keeps a legitimate nested path.
        EQUALS(writer.Add("dist\\sub\\a.js", "x"), true);
        UEQUALS(writer.GetEntryCount(), 1u);

        // And rejects a duplicate of it after normalisation.
        EQUALS(writer.Add("dist/sub/a.js", "x"), false);
        UEQUALS(writer.GetEntryCount(), 1u);
    });

    IT("matches client file globs", {
        EQUALS(Framework::Scripting::ResourcePackager::MatchGlob("dist/client.js", "dist/client.js"), true);
        EQUALS(Framework::Scripting::ResourcePackager::MatchGlob("dist/client.js", "dist/server.js"), false);

        EQUALS(Framework::Scripting::ResourcePackager::MatchGlob("ui/*.html", "ui/index.html"), true);
        // A single star must not cross a separator.
        EQUALS(Framework::Scripting::ResourcePackager::MatchGlob("ui/*.html", "ui/deep/index.html"), false);

        EQUALS(Framework::Scripting::ResourcePackager::MatchGlob("ui/**/*.html", "ui/deep/index.html"), true);
        EQUALS(Framework::Scripting::ResourcePackager::MatchGlob("ui/**", "ui/a/b/c.css"), true);
        // "**/" must also match zero directories.
        EQUALS(Framework::Scripting::ResourcePackager::MatchGlob("ui/**/*.html", "ui/index.html"), true);

        EQUALS(Framework::Scripting::ResourcePackager::MatchGlob("dist/client.??", "dist/client.js"), true);
        EQUALS(Framework::Scripting::ResourcePackager::MatchGlob("dist/client.??", "dist/client.mjs"), false);

        EQUALS(Framework::Scripting::ResourcePackager::MatchGlob("*.js", "dist/client.js"), false);
        EQUALS(Framework::Scripting::ResourcePackager::MatchGlob("**/*.js", "dist/client.js"), true);
    });

    IT("parses the clientFiles allowlist", {
        Framework::Scripting::PackageManifest manifest;

        nlohmann::json withFiles = {
            {"name", "demo"},
            {"mafiahub", {{"client", "c.js"}, {"clientFiles", nlohmann::json::array({"ui/**", "a.js"})}}},
        };
        EQUALS(manifest.ParseJson(withFiles), true);
        UEQUALS(manifest.GetMafiaHubConfig().clientFiles.size(), 2u);
        STREQUALS(manifest.GetMafiaHubConfig().clientFiles[0].c_str(), "ui/**");

        // Absent means "fall back to the directory scan", not "ship nothing".
        nlohmann::json without = {{"name", "demo"}, {"mafiahub", {{"client", "c.js"}}}};
        EQUALS(manifest.ParseJson(without), true);
        EQUALS(manifest.GetMafiaHubConfig().clientFiles.empty(), true);

        // Non-string and empty entries are dropped rather than packaged as garbage paths.
        nlohmann::json mixed = {
            {"name", "demo"},
            {"mafiahub", {{"client", "c.js"}, {"clientFiles", nlohmann::json::array({"ok.js", 42, "", nlohmann::json::object()})}}},
        };
        EQUALS(manifest.ParseJson(mixed), true);
        UEQUALS(manifest.GetMafiaHubConfig().clientFiles.size(), 1u);
        STREQUALS(manifest.GetMafiaHubConfig().clientFiles[0].c_str(), "ok.js");
    });

    IT("keeps the server bundle out of the package", {
        // dist/server.js sits next to dist/client.js in the usual bundler layout.
        const auto root = std::filesystem::temp_directory_path() / "fwpak_ut_fallback";
        std::filesystem::remove_all(root);
        writeFile(root / "package.json", "{\"name\":\"demo\"}");
        writeFile(root / "dist" / "client.js", "client bytes");
        writeFile(root / "dist" / "server.js", "SECRET server bytes");
        writeFile(root / "dist" / "server.js.map", "server map");
        writeFile(root / "dist" / "shared.js", "shared bytes");

        nlohmann::json manifestJson = {
            {"name", "demo"},
            {"version", "1.0.0"},
            {"mafiahub", {{"server", "dist/server.js"}, {"client", "dist/client.js"}}},
        };
        Framework::Scripting::PackageManifest manifest;
        EQUALS(manifest.ParseJson(manifestJson), true);

        bool ok = false;
        const auto key = Framework::Utils::Crypto::GenerateKey(&ok);
        EQUALS(ok, true);

        Framework::Scripting::PackagedResource packaged;
        std::string error;
        EQUALS(Framework::Scripting::ResourcePackager::Package("demo", root.string(), manifest, &key, packaged, error), true);
        EQUALS(packaged.blob.find("SECRET") == std::string::npos, true);

        // Asserted through PhysicsFS, as the client sees it.
        std::string zip;
        EQUALS(Framework::Utils::Package::Open(packaged.blob, &key, zip, error), true);
        auto &vfs = Framework::Utils::Vfs::Get();
        EQUALS(vfs.MountMemory(std::move(zip), "ut_fallback.zip", "/resources/ut_fallback", error), true);

        EQUALS(vfs.Contains("/resources/ut_fallback/dist/client.js"), true);
        EQUALS(vfs.Contains("/resources/ut_fallback/package.json"), true);
        EQUALS(vfs.Contains("/resources/ut_fallback/dist/shared.js"), true);
        EQUALS(vfs.Contains("/resources/ut_fallback/dist/server.js"), false);
        EQUALS(vfs.Contains("/resources/ut_fallback/dist/server.js.map"), false);

        vfs.Unmount("ut_fallback.zip");
        std::filesystem::remove_all(root);
    });

    IT("ships exactly what clientFiles declares", {
        const auto root = std::filesystem::temp_directory_path() / "fwpak_ut_declared";
        std::filesystem::remove_all(root);
        writeFile(root / "package.json", "{\"name\":\"demo\"}");
        writeFile(root / "dist" / "client.js", "client bytes");
        writeFile(root / "dist" / "server.js", "SECRET server bytes");
        writeFile(root / "dist" / "internal.js", "SECRET internal bytes");
        writeFile(root / "ui" / "index.html", "<h1>ui</h1>");

        nlohmann::json manifestJson = {
            {"name", "demo"},
            {"version", "1.0.0"},
            {"mafiahub",
             {{"server", "dist/server.js"}, {"client", "dist/client.js"}, {"clientFiles", nlohmann::json::array({"dist/client.js", "ui/**"})}}},
        };
        Framework::Scripting::PackageManifest manifest;
        EQUALS(manifest.ParseJson(manifestJson), true);
        UEQUALS(manifest.GetMafiaHubConfig().clientFiles.size(), 2u);

        bool ok = false;
        const auto key = Framework::Utils::Crypto::GenerateKey(&ok);
        Framework::Scripting::PackagedResource packaged;
        std::string error;
        EQUALS(Framework::Scripting::ResourcePackager::Package("demo", root.string(), manifest, &key, packaged, error), true);

        // package.json, dist/client.js, ui/index.html -- and nothing else.
        UEQUALS(packaged.fileCount, 3u);
        EQUALS(packaged.blob.find("SECRET") == std::string::npos, true);

        std::filesystem::remove_all(root);
    });

    IT("mounts a verified package and refuses everything else", {
        auto &vfs = Framework::Utils::Vfs::Get();

        const auto cacheRoot = std::filesystem::temp_directory_path() / "fwpak_ut_mount";
        std::filesystem::remove_all(cacheRoot);
        std::filesystem::create_directories(cacheRoot);

        bool ok = false;
        const auto key = Framework::Utils::Crypto::GenerateKey(&ok);
        EQUALS(ok, true);

        Framework::Utils::Package::Writer writer;
        EQUALS(writer.Add("package.json", "{}"), true);
        EQUALS(writer.Add("dist/client.js", "client bytes"), true);
        std::string blob;
        EQUALS(writer.Build(&key, blob), true);
        const auto hash = Framework::Utils::Crypto::Sha256Hex(blob);

        {
            std::ofstream out(cacheRoot / "demo.fwpak", std::ios::binary | std::ios::trunc);
            out.write(blob.data(), static_cast<std::streamsize>(blob.size()));
        }

        Framework::Integrations::Client::ResourcePackageMounter mounter;
        std::string error;

        // No key yet: nothing may mount.
        EQUALS(mounter.Mount(cacheRoot.string(), "demo", hash, error), false);

        EQUALS(mounter.SetKey(Framework::Utils::Crypto::ToHex(key.data(), key.size())), true);
        EQUALS(mounter.HasKey(), true);

        // An announced hash that does not match the bytes on disk must be refused.
        EQUALS(mounter.Mount(cacheRoot.string(), "demo", std::string(64, 'a'), error), false);
        EQUALS(vfs.Contains("/resources/demo/dist/client.js"), false);

        // So must an empty hash -- that is how a resource with no package is announced.
        EQUALS(mounter.Mount(cacheRoot.string(), "demo", "", error), false);

        // A resource with no package file at all.
        EQUALS(mounter.Mount(cacheRoot.string(), "missing", hash, error), false);

        EQUALS(mounter.Mount(cacheRoot.string(), "demo", hash, error), true);
        UEQUALS(mounter.GetMountedResources().size(), 1u);

        std::string contents;
        EQUALS(vfs.Read("/resources/demo/dist/client.js", contents), true);
        STREQUALS(contents.c_str(), "client bytes");
        EQUALS(vfs.Contains("/resources/demo/package.json"), true);

        // What resource discovery walks.
        const auto dirs = vfs.EnumerateDirectories("/resources");
        EQUALS(std::find(dirs.begin(), dirs.end(), std::string("demo")) != dirs.end(), true);

        // Unmounting drops the bytes.
        mounter.Unmount(cacheRoot.string(), "demo");
        EQUALS(vfs.Contains("/resources/demo/dist/client.js"), false);
        EQUALS(mounter.GetMountedResources().empty(), true);

        std::filesystem::remove_all(cacheRoot);
    });

    IT("refuses a wrong key and an unencrypted container", {
        auto &vfs = Framework::Utils::Vfs::Get();

        const auto cacheRoot = std::filesystem::temp_directory_path() / "fwpak_ut_mount_bad";
        std::filesystem::remove_all(cacheRoot);
        std::filesystem::create_directories(cacheRoot);

        bool ok = false;
        const auto key = Framework::Utils::Crypto::GenerateKey(&ok);
        const auto otherKey = Framework::Utils::Crypto::GenerateKey(&ok);
        EQUALS(ok, true);

        const auto writePackage = [&](const char *name, const Framework::Utils::Crypto::Key *withKey) {
            Framework::Utils::Package::Writer writer;
            writer.Add("dist/client.js", "client bytes");
            std::string blob;
            writer.Build(withKey, blob);
            std::ofstream out(cacheRoot / (std::string(name) + ".fwpak"), std::ios::binary | std::ios::trunc);
            out.write(blob.data(), static_cast<std::streamsize>(blob.size()));
            return Framework::Utils::Crypto::Sha256Hex(blob);
        };

        const auto wrongKeyHash = writePackage("wrongkey", &key);
        // Refused despite an honest hash: a server cannot silently stop encrypting.
        const auto plainHash = writePackage("plain", nullptr);

        Framework::Integrations::Client::ResourcePackageMounter mounter;
        EQUALS(mounter.SetKey(Framework::Utils::Crypto::ToHex(otherKey.data(), otherKey.size())), true);

        std::string error;
        EQUALS(mounter.Mount(cacheRoot.string(), "wrongkey", wrongKeyHash, error), false);
        EQUALS(mounter.Mount(cacheRoot.string(), "plain", plainHash, error), false);
        EQUALS(vfs.Contains("/resources/wrongkey/dist/client.js"), false);
        EQUALS(vfs.Contains("/resources/plain/dist/client.js"), false);

        // A malformed hex key is rejected outright rather than silently truncated.
        EQUALS(mounter.SetKey("nope"), false);
        EQUALS(mounter.HasKey(), false);

        std::filesystem::remove_all(cacheRoot);
    });

    IT("serves mounted package files to the CEF resource scheme", {
        auto &vfs = Framework::Utils::Vfs::Get();

        const auto root = std::filesystem::temp_directory_path() / "fwpak_ut_cef";
        std::filesystem::remove_all(root);
        std::filesystem::create_directories(root);

        bool ok = false;
        const auto key = Framework::Utils::Crypto::GenerateKey(&ok);
        EQUALS(ok, true);
        Framework::Utils::Package::Writer writer;
        EQUALS(writer.Add("ui/index.html", "<h1>packaged</h1>"), true);
        std::string blob;
        EQUALS(writer.Build(&key, blob), true);

        std::string zip, error;
        EQUALS(Framework::Utils::Package::Open(blob, &key, zip, error), true);
        EQUALS(vfs.MountMemory(std::move(zip), "ut_cef.zip", "/resources/ut_cef", error), true);

        // Nothing on disk; the page exists only in the mounted package.
        Framework::GUI::Resources::DirectoryProvider provider(root);
        provider.SetVirtualPrefix(Framework::Utils::Vfs::kResourceMountRoot);

        Framework::GUI::Resources::ResourceStat stat;
        auto stream = provider.Open("ut_cef/ui/index.html", stat);
        EQUALS(stream != nullptr, true);
        UEQUALS(stat.size, static_cast<std::uint64_t>(17));

        char buffer[64] = {};
        const auto read = stream->Read(buffer, sizeof(buffer));
        EQUALS(read == 17, true);
        STREQUALS(std::string(buffer, 17).c_str(), "<h1>packaged</h1>");

        // Traversal is still refused for package-backed paths.
        Framework::GUI::Resources::ResourceStat escapeStat;
        EQUALS(provider.Open("../ut_cef/ui/index.html", escapeStat) == nullptr, true);

        // A path with nothing behind it in either store.
        Framework::GUI::Resources::ResourceStat missingStat;
        EQUALS(provider.Open("ut_cef/ui/missing.html", missingStat) == nullptr, true);

        vfs.Unmount("ut_cef.zip");
        std::filesystem::remove_all(root);
    });

    IT("packages a resource whose client and server entry are the same file", {
        // shared-utils declares utils.js as both; the shared file must still ship.
        const auto root = std::filesystem::temp_directory_path() / "fwpak_ut_shared";
        std::filesystem::remove_all(root);
        writeFile(root / "package.json", "{\"name\":\"shared\"}");
        writeFile(root / "utils.js", "exports.x = 1;");

        nlohmann::json manifestJson = {
            {"name", "shared"},
            {"version", "1.0.0"},
            {"mafiahub", {{"server", "utils.js"}, {"client", "utils.js"}}},
        };
        Framework::Scripting::PackageManifest manifest;
        EQUALS(manifest.ParseJson(manifestJson), true);

        bool ok = false;
        const auto key = Framework::Utils::Crypto::GenerateKey(&ok);
        EQUALS(ok, true);

        Framework::Scripting::PackagedResource packaged;
        std::string error;
        EQUALS(Framework::Scripting::ResourcePackager::Package("shared", root.string(), manifest, &key, packaged, error), true);
        UEQUALS(packaged.fileCount, 2u);

        std::string zip;
        EQUALS(Framework::Utils::Package::Open(packaged.blob, &key, zip, error), true);
        auto &vfs = Framework::Utils::Vfs::Get();
        EQUALS(vfs.MountMemory(std::move(zip), "ut_shared.zip", "/resources/ut_shared", error), true);
        EQUALS(vfs.Contains("/resources/ut_shared/utils.js"), true);

        vfs.Unmount("ut_shared.zip");
        std::filesystem::remove_all(root);
    });

    IT("refuses a key from a server speaking the old protocol", {
        // An older server writes a different ServerResources layout, so the key field lands on
        // whatever followed it. Every such shape must be rejected rather than half-accepted.
        Framework::Integrations::Client::ResourcePackageMounter mounter;

        EQUALS(mounter.SetKey(""), false);
        EQUALS(mounter.HasKey(), false);

        // A version string, which is what the desynced read actually produced.
        EQUALS(mounter.SetKey("1.0.0"), false);
        EQUALS(mounter.HasKey(), false);

        // Right length, not hex.
        EQUALS(mounter.SetKey(std::string(64, 'g')), false);
        EQUALS(mounter.HasKey(), false);

        // Right alphabet, wrong length.
        EQUALS(mounter.SetKey(std::string(63, 'a')), false);
        EQUALS(mounter.SetKey(std::string(65, 'a')), false);
        EQUALS(mounter.HasKey(), false);

        // A real key still works, and a later bad one clears it rather than leaving the old one.
        bool ok = false;
        const auto key = Framework::Utils::Crypto::GenerateKey(&ok);
        EQUALS(ok, true);
        EQUALS(mounter.SetKey(Framework::Utils::Crypto::ToHex(key.data(), key.size())), true);
        EQUALS(mounter.HasKey(), true);
        EQUALS(mounter.SetKey("garbage"), false);
        EQUALS(mounter.HasKey(), false);
    });

    IT("discovers a resource from its mounted package", {
        // The mount and the discovery root have to agree. They did not: the client configured
        // ResourceManager with the on-disk cache while packages mounted at /resources, so
        // discovery found nothing and no client resource ever ran.
        auto &vfs = Framework::Utils::Vfs::Get();

        bool ok = false;
        const auto key = Framework::Utils::Crypto::GenerateKey(&ok);
        EQUALS(ok, true);

        Framework::Utils::Package::Writer writer;
        EQUALS(writer.Add("package.json", "{\"name\":\"ut_discover\",\"version\":\"1.0.0\",\"mafiahub\":{\"client\":\"client.js\"}}"), true);
        EQUALS(writer.Add("client.js", "globalThis.utDiscover = 1;"), true);
        std::string blob;
        EQUALS(writer.Build(&key, blob), true);

        std::string zip, error;
        EQUALS(Framework::Utils::Package::Open(blob, &key, zip, error), true);
        EQUALS(vfs.MountMemory(std::move(zip), "ut_discover.zip", "/resources/ut_discover", error), true);

        Framework::Scripting::NodeEngine engine;
        EQUALS(engine.Init(), Framework::Scripting::ScriptingError::SCRIPTING_NONE);

        Framework::Scripting::ResourceManagerConfig config;
        config.resourcesPath = Framework::Utils::Vfs::kResourceMountRoot;
        config.isClient      = true;

        Framework::Scripting::ResourceManager manager(&engine, config);
        UEQUALS(manager.DiscoverResources(), 1u);
        EQUALS(manager.HasResource("ut_discover"), true);

        const auto *resource = manager.GetResource("ut_discover");
        EQUALS(resource != nullptr, true);
        if (resource) {
            // The entry point must resolve inside the mount, with '/' joins.
            STREQUALS(resource->GetClientEntryPoint().c_str(), "/resources/ut_discover/client.js");
            EQUALS(vfs.Contains(resource->GetClientEntryPoint()), true);
        }

        vfs.Unmount("ut_discover.zip");
        engine.Shutdown();
    });

    IT("resolves modules inside a mounted package", {
        // V8Engine cannot be Init()ed here (libnode owns the V8 platform in this binary), but the
        // virtual branch of ResolveModulePath touches no isolate, so the resolution the client
        // depends on is still exercised directly.
        auto &vfs = Framework::Utils::Vfs::Get();

        bool ok = false;
        const auto key = Framework::Utils::Crypto::GenerateKey(&ok);
        EQUALS(ok, true);

        Framework::Utils::Package::Writer writer;
        EQUALS(writer.Add("package.json", "{\"name\":\"ut_resolve\"}"), true);
        EQUALS(writer.Add("dist/client.js", "require('./helper.js');"), true);
        EQUALS(writer.Add("dist/helper.js", "module.exports = 1;"), true);
        EQUALS(writer.Add("lib/index.js", "module.exports = 2;"), true);
        std::string blob;
        EQUALS(writer.Build(&key, blob), true);

        std::string zip, error;
        EQUALS(Framework::Utils::Package::Open(blob, &key, zip, error), true);
        EQUALS(vfs.MountMemory(std::move(zip), "ut_resolve.zip", "/resources/ut_resolve", error), true);

        Framework::Scripting::V8Engine engine;
        engine.SetModuleRootPath(Framework::Utils::Vfs::kResourceMountRoot);

        // Relative require() between two files in the package.
        STREQUALS(engine.ResolveModulePath("./helper.js", "/resources/ut_resolve/dist").c_str(), "/resources/ut_resolve/dist/helper.js");

        // Extension inferred, and a directory resolving to index.js.
        STREQUALS(engine.ResolveModulePath("./helper", "/resources/ut_resolve/dist").c_str(), "/resources/ut_resolve/dist/helper.js");
        STREQUALS(engine.ResolveModulePath("../lib", "/resources/ut_resolve/dist").c_str(), "/resources/ut_resolve/lib/index.js");

        // Absolute virtual specifier.
        STREQUALS(engine.ResolveModulePath("/resources/ut_resolve/dist/helper.js", "/resources/ut_resolve/dist").c_str(), "/resources/ut_resolve/dist/helper.js");

        // Missing file resolves to nothing rather than a bogus path.
        STREQUALS(engine.ResolveModulePath("./missing.js", "/resources/ut_resolve/dist").c_str(), "");

        // Climbing out of the module root must be refused, not clamped.
        STREQUALS(engine.ResolveModulePath("../../../evil.js", "/resources/ut_resolve/dist").c_str(), "");
        STREQUALS(engine.ResolveModulePath("../../ut_resolve/dist/helper.js", "/resources/ut_resolve/dist").c_str(), "/resources/ut_resolve/dist/helper.js");

        vfs.Unmount("ut_resolve.zip");
    });

    IT("attributes a packaged script to its resource", {
        // Timer ownership, exports and error reporting all key off this.
        auto &vfs = Framework::Utils::Vfs::Get();

        bool ok = false;
        const auto key = Framework::Utils::Crypto::GenerateKey(&ok);
        EQUALS(ok, true);

        Framework::Utils::Package::Writer writer;
        EQUALS(writer.Add("package.json", "{\"name\":\"ut_attr\",\"version\":\"1.0.0\",\"mafiahub\":{\"client\":\"client.js\"}}"), true);
        EQUALS(writer.Add("client.js", "globalThis.x = 1;"), true);
        std::string blob;
        EQUALS(writer.Build(&key, blob), true);

        std::string zip, error;
        EQUALS(Framework::Utils::Package::Open(blob, &key, zip, error), true);
        EQUALS(vfs.MountMemory(std::move(zip), "ut_attr.zip", "/resources/ut_attr", error), true);

        Framework::Scripting::NodeEngine engine;
        EQUALS(engine.Init(), Framework::Scripting::ScriptingError::SCRIPTING_NONE);

        Framework::Scripting::ResourceManagerConfig config;
        config.resourcesPath = Framework::Utils::Vfs::kResourceMountRoot;
        config.isClient      = true;

        Framework::Scripting::ResourceManager manager(&engine, config);
        UEQUALS(manager.DiscoverResources(), 1u);

        STREQUALS(manager.GetResourceNameFromScriptPath("/resources/ut_attr/client.js").c_str(), "ut_attr");
        STREQUALS(manager.GetResourceNameFromScriptPath("/resources/ut_attr/deep/nested.js").c_str(), "ut_attr");
        STREQUALS(manager.GetResourceNameFromScriptPath("/resources/other/client.js").c_str(), "");
        STREQUALS(manager.GetResourceNameFromScriptPath("/resources/ut_attr").c_str(), "");
        STREQUALS(manager.GetResourceNameFromScriptPath("C:\\somewhere\\else\\client.js").c_str(), "");

        vfs.Unmount("ut_attr.zip");
        engine.Shutdown();
    });

    IT("normalizes virtual paths and refuses escapes", {
        STREQUALS(Framework::Utils::Vfs::NormalizeVirtual("/resources/demo/./a.js").c_str(), "/resources/demo/a.js");
        STREQUALS(Framework::Utils::Vfs::NormalizeVirtual("/resources/demo/sub/../a.js").c_str(), "/resources/demo/a.js");
        STREQUALS(Framework::Utils::Vfs::NormalizeVirtual("/resources//demo///a.js").c_str(), "/resources/demo/a.js");
        STREQUALS(Framework::Utils::Vfs::NormalizeVirtual("resources\\demo\\a.js").c_str(), "/resources/demo/a.js");

        // Climbing past the virtual root must fail rather than clamp.
        EQUALS(Framework::Utils::Vfs::NormalizeVirtual("/../evil.js").empty(), true);
        EQUALS(Framework::Utils::Vfs::NormalizeVirtual("/resources/../../evil.js").empty(), true);

        EQUALS(Framework::Utils::Vfs::IsVirtualPath("/resources/demo"), true);
        EQUALS(Framework::Utils::Vfs::IsVirtualPath("/resources"), true);
        EQUALS(Framework::Utils::Vfs::IsVirtualPath("/resourcesevil/x"), false);
        EQUALS(Framework::Utils::Vfs::IsVirtualPath("C:\\Users\\x\\cache\\demo"), false);
        EQUALS(Framework::Utils::Vfs::IsVirtualPath("/home/user/resources/demo"), false);

        STREQUALS(Framework::Utils::Vfs::ResourcePath("demo").c_str(), "/resources/demo");
    });
})
