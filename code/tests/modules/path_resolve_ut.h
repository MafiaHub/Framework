/*
 * MafiaHub OSS license
 * Copyright (c) 2021-2024, MafiaHub. All rights reserved.
 *
 * This file comes from MafiaHub, hosted at https://github.com/MafiaHub/Framework.
 * See LICENSE file in the source repository for information regarding licensing.
 */

#pragma once

#include "utils/path.h"

#include <filesystem>

// Backslash separators and drive prefixes are only meaningful on Windows; on
// POSIX they are ordinary filename characters and the same inputs resolve to
// (harmless) files inside the root
#ifdef _WIN32
inline constexpr bool kPathSepIsWindows = true;
#else
inline constexpr bool kPathSepIsWindows = false;
#endif

MODULE(path_resolve, {
    using Framework::Utils::ResolvePathUnderRoot;
    namespace fs = std::filesystem;

    const fs::path root = fs::temp_directory_path() / "fw_path_resolve_ut" / "ui";
    fs::create_directories(root / "sub");
    const fs::path rootCanonical = fs::weakly_canonical(root);

    IT("maps empty and / to index.html", {
        EQUALS(ResolvePathUnderRoot(root, "/") == rootCanonical / "index.html", true);
        EQUALS(ResolvePathUnderRoot(root, "") == rootCanonical / "index.html", true);
    });

    IT("resolves nested paths under the root", {
        EQUALS(ResolvePathUnderRoot(root, "/sub/style.css") == rootCanonical / "sub" / "style.css", true);
    });

    IT("resolves lexically without requiring the file to exist", {
        EQUALS(ResolvePathUnderRoot(root, "/missing/deep/file.js") == rootCanonical / "missing" / "deep" / "file.js", true);
    });

    IT("allows dot-dot that stays inside the root", {
        EQUALS(ResolvePathUnderRoot(root, "/sub/../index.html") == rootCanonical / "index.html", true);
    });

    IT("rejects dot-dot traversal escaping the root", {
        EQUALS(ResolvePathUnderRoot(root, "/../secret.txt").empty(), true);
        EQUALS(ResolvePathUnderRoot(root, "/sub/../../secret.txt").empty(), true);
        EQUALS(ResolvePathUnderRoot(root, "/sub/../../../../../etc/passwd").empty(), true);
    });

    IT("rejects backslash traversal where backslash separates", {
        EQUALS(ResolvePathUnderRoot(root, "/..\\secret.txt").empty(), kPathSepIsWindows);
        EQUALS(ResolvePathUnderRoot(root, "/sub\\..\\..\\secret.txt").empty(), kPathSepIsWindows);
    });

    IT("rejects drive injection where drives exist", {
        EQUALS(ResolvePathUnderRoot(root, "/C:/Windows/win.ini").empty(), kPathSepIsWindows);
        EQUALS(ResolvePathUnderRoot(root, "/C:\\Windows\\win.ini").empty(), kPathSepIsWindows);
    });

    fs::remove_all(fs::temp_directory_path() / "fw_path_resolve_ut");
});
