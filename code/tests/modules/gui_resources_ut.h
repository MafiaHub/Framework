/*
 * MafiaHub OSS license
 * Copyright (c) 2021-2024, MafiaHub. All rights reserved.
 *
 * This file comes from MafiaHub, hosted at https://github.com/MafiaHub/Framework.
 * See LICENSE file in the source repository for information regarding licensing.
 */

#pragma once

#include "gui/resources/directory_provider.h"
#include "gui/resources/memory_provider.h"
#include "gui/resources/mime.h"
#include "gui/resources/resource_path.h"
#include "gui/resources/resource_stream.h"

#include <filesystem>
#include <fstream>
#include <string>

MODULE(gui_resources, {
    using Framework::GUI::Resources::DirectoryProvider;
    using Framework::GUI::Resources::MemoryProvider;
    using Framework::GUI::Resources::MemoryStream;
    using Framework::GUI::Resources::MimeTypeForPath;
    using Framework::GUI::Resources::MimeTypeIsTextual;
    using Framework::GUI::Resources::NormalizeResourcePath;
    using Framework::GUI::Resources::ResourceStat;

    // Normalizes and returns the result, or "!" when the path was rejected. Lets
    // a case state its expectation as one comparison either way.
    auto normalize = [](const std::string &urlPath) -> std::string {
        std::string out;
        return NormalizeResourcePath(urlPath, out) ? out : "!";
    };

    // Everything the tests write lives under the working directory, which is the
    // build tree, and is removed again at the end of the directory case.
    const std::filesystem::path sandbox = std::filesystem::current_path() / "framework_ut_gui_resources";

    auto writeFile = [](const std::filesystem::path &path, const std::string &contents) {
        std::filesystem::create_directories(path.parent_path());
        std::ofstream file(path, std::ios::binary | std::ios::trunc);
        file.write(contents.data(), static_cast<std::streamsize>(contents.size()));
    };

    auto drain = [](Framework::GUI::Resources::ResourceStream &stream) {
        std::string out;
        char buffer[8];
        for (;;) {
            const std::int64_t read = stream.Read(buffer, sizeof(buffer));
            if (read <= 0) {
                break;
            }
            out.append(buffer, static_cast<std::size_t>(read));
        }
        return out;
    };

    // --- path normalization ---

    IT("strips the leading slash and keeps nested segments", {
        STREQUALS(normalize("/menu/assets/app.js").c_str(), "menu/assets/app.js");
    });

    IT("supplies index.html for the root and for a directory-shaped path", {
        STREQUALS(normalize("/").c_str(), "index.html");
        STREQUALS(normalize("/menu/").c_str(), "menu/index.html");
    });

    IT("drops the query and the fragment", {
        STREQUALS(normalize("/app.js?v=2").c_str(), "app.js");
        STREQUALS(normalize("/app.js#top").c_str(), "app.js");
        STREQUALS(normalize("/?v=2").c_str(), "index.html");
    });

    IT("collapses empty segments", {
        STREQUALS(normalize("/menu//assets///app.js").c_str(), "menu/assets/app.js");
    });

    IT("decodes ordinary percent escapes", {
        STREQUALS(normalize("/my%20menu/app.js").c_str(), "my menu/app.js");
    });

    IT("rejects a path that is not absolute", {
        STREQUALS(normalize("menu/app.js").c_str(), "!");
        STREQUALS(normalize("").c_str(), "!");
    });

    IT("rejects traversal spelled literally", {
        STREQUALS(normalize("/../secret").c_str(), "!");
        STREQUALS(normalize("/menu/../../secret").c_str(), "!");
        STREQUALS(normalize("/./menu").c_str(), "!");
    });

    IT("rejects traversal hidden in an escape", {
        STREQUALS(normalize("/%2e%2e/secret").c_str(), "!");
        STREQUALS(normalize("/%2E%2E/secret").c_str(), "!");
    });

    IT("rejects an escaped separator rather than decoding it", {
        // %2F would otherwise become a separator only after the segment split,
        // which is how a single segment smuggles a whole extra path.
        STREQUALS(normalize("/menu%2f..%2fsecret").c_str(), "!");
        STREQUALS(normalize("/menu%5c..%5csecret").c_str(), "!");
    });

    IT("rejects a Windows separator, a drive and a stream name", {
        STREQUALS(normalize("/menu\\..\\secret").c_str(), "!");
        STREQUALS(normalize("/C:/Windows/win.ini").c_str(), "!");
        STREQUALS(normalize("/app.js:stream").c_str(), "!");
    });

    IT("rejects an embedded NUL and a malformed escape", {
        STREQUALS(normalize("/app.js%00.txt").c_str(), "!");
        STREQUALS(normalize("/app%zz.js").c_str(), "!");
        STREQUALS(normalize("/app%2").c_str(), "!");
    });

    // --- MIME types ---

    IT("types the things a page is actually made of", {
        STREQUALS(MimeTypeForPath("index.html").c_str(), "text/html");
        STREQUALS(MimeTypeForPath("menu/app.js").c_str(), "text/javascript");
        STREQUALS(MimeTypeForPath("menu/app.mjs").c_str(), "text/javascript");
        STREQUALS(MimeTypeForPath("menu.css").c_str(), "text/css");
        STREQUALS(MimeTypeForPath("app.wasm").c_str(), "application/wasm");
        STREQUALS(MimeTypeForPath("site.webmanifest").c_str(), "application/manifest+json");
        STREQUALS(MimeTypeForPath("font.woff2").c_str(), "font/woff2");
    });

    IT("is case insensitive about the extension", {
        STREQUALS(MimeTypeForPath("LOGO.PNG").c_str(), "image/png");
    });

    IT("falls back to octet-stream without a usable extension", {
        STREQUALS(MimeTypeForPath("LICENSE").c_str(), "application/octet-stream");
        STREQUALS(MimeTypeForPath("archive.7z").c_str(), "application/octet-stream");
        // The dot belongs to a directory, so the file itself has no extension.
        STREQUALS(MimeTypeForPath("v1.2/README").c_str(), "application/octet-stream");
    });

    IT("recognizes which types need a charset", {
        EQUALS(MimeTypeIsTextual("text/html"), true);
        EQUALS(MimeTypeIsTextual("text/html; v=1"), true);
        EQUALS(MimeTypeIsTextual("application/json"), true);
        EQUALS(MimeTypeIsTextual("image/svg+xml"), true);
        EQUALS(MimeTypeIsTextual("image/png"), false);
        EQUALS(MimeTypeIsTextual("application/wasm"), false);
    });

    // --- streams ---

    IT("reads a memory stream to completion in chunks", {
        MemoryStream stream("abcdefghij");
        char buffer[4] = {};
        EQUALS(stream.Read(buffer, 4), 4);
        EQUALS(stream.Read(buffer, 4), 4);
        EQUALS(stream.Read(buffer, 4), 2);
        // Zero, not a failure, is what tells the handler the body is finished.
        EQUALS(stream.Read(buffer, 4), 0);
    });

    IT("skips within a memory stream and clamps at the end", {
        MemoryStream stream("abcdefghij");
        EQUALS(stream.Skip(4), 4);
        char buffer[2] = {};
        EQUALS(stream.Read(buffer, 2), 2);
        EQUALS(buffer[0] == 'e', true);
        EQUALS(stream.Skip(100), 4);
        EQUALS(stream.Skip(1), 0);
    });

    // --- providers ---

    IT("serves bytes held in memory and reports their size", {
        MemoryProvider provider("test");
        provider.Set("index.html", "<html>hi</html>", "text/html");

        ResourceStat stat;
        auto stream = provider.Open("index.html", stat);
        EQUALS(stream != nullptr, true);
        EQUALS(stat.size, 15u);
        STREQUALS(stat.mimeType.c_str(), "text/html");
        STREQUALS(drain(*stream).c_str(), "<html>hi</html>");

        ResourceStat missing;
        EQUALS(provider.Open("nope.html", missing) == nullptr, true);
        EQUALS(provider.Erase("index.html"), true);
        EQUALS(provider.Open("index.html", missing) == nullptr, true);
    });

    IT("serves a file from a directory root", {
        std::filesystem::remove_all(sandbox);
        writeFile(sandbox / "ui" / "index.html", "<html>menu</html>");
        writeFile(sandbox / "ui" / "assets" / "app.js", "console.log(1);");
        writeFile(sandbox / "secret.txt", "do not serve me");

        DirectoryProvider provider(sandbox / "ui");
        provider.MarkImmutable("assets/");

        ResourceStat stat;
        auto page = provider.Open("index.html", stat);
        EQUALS(page != nullptr, true);
        EQUALS(stat.size, 17u);
        EQUALS(stat.immutable, false);
        STREQUALS(drain(*page).c_str(), "<html>menu</html>");

        ResourceStat assetStat;
        auto asset = provider.Open("assets/app.js", assetStat);
        EQUALS(asset != nullptr, true);
        EQUALS(assetStat.immutable, true);
        STREQUALS(drain(*asset).c_str(), "console.log(1);");
    });

    IT("refuses to serve anything outside the directory root", {
        DirectoryProvider provider(sandbox / "ui");

        ResourceStat stat;
        // Normalization would already have refused this shape; the provider
        // refuses it again against the resolved path, because that is the only
        // guard a symlink pointing out of the root ever meets.
        EQUALS(provider.Open("../secret.txt", stat) == nullptr, true);
        EQUALS(provider.Open("missing.html", stat) == nullptr, true);
        // A directory is not a body.
        EQUALS(provider.Open("assets", stat) == nullptr, true);
    });

    IT("skips forward in a file and clamps at its end", {
        DirectoryProvider provider(sandbox / "ui");

        ResourceStat stat;
        auto asset = provider.Open("assets/app.js", stat);
        EQUALS(asset != nullptr, true);
        EQUALS(asset->Skip(8), 8);
        STREQUALS(drain(*asset).c_str(), "log(1);");
        // Nothing left to skip: zero, which the handler turns into the range
        // failure CEF expects rather than a silent short response.
        EQUALS(asset->Skip(4), 0);
    });

    IT("releases the file once the stream is dropped", {
        // The handle is held open for the life of the response, so a caller that
        // has finished with it must be able to delete what it was reading. This
        // is exactly what a failed cleanup here would prove broken.
        DirectoryProvider provider(sandbox / "ui");

        ResourceStat stat;
        auto page = provider.Open("index.html", stat);
        EQUALS(page != nullptr, true);
        page.reset();

        std::error_code error;
        std::filesystem::remove_all(sandbox, error);
        EQUALS(static_cast<bool>(error), false);
        EQUALS(std::filesystem::exists(sandbox), false);
    });
})
