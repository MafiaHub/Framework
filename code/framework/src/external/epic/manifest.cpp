/*
 * MafiaHub OSS license
 * Copyright (c) 2021-2023, MafiaHub. All rights reserved.
 *
 * This file comes from MafiaHub, hosted at https://github.com/MafiaHub/Framework.
 * See LICENSE file in the source repository for information regarding licensing.
 */

#include "manifest.h"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <ranges>

namespace Framework::External::Epic {
    namespace {
        std::string ToLower(std::string s) {
            std::ranges::transform(s, s.begin(), [](unsigned char c) {
                return static_cast<char>(std::tolower(c));
            });
            return s;
        }

        // File name (portion after the last / or \) of a possibly-relative path.
        std::string FileName(const std::string &path) {
            const auto pos = path.find_last_of("/\\");
            return pos == std::string::npos ? path : path.substr(pos + 1);
        }
    } // namespace

    std::string GetManifestsDir() {
        std::string base = "C:\\ProgramData";
        if (const char *pd = std::getenv("PROGRAMDATA"); pd && *pd) {
            base = pd;
        }
        return base + "\\Epic\\EpicGamesLauncher\\Data\\Manifests";
    }

    std::vector<InstalledApp> EnumerateInstalledApps() {
        std::vector<InstalledApp> apps;

        // The dir holds an .item for every installed Epic title, so one malformed manifest must
        // not sink the scan: iterate with the non-throwing (ec) overloads, and try/catch each
        // parse — nlohmann::value() throws on a wrong-typed key or a non-object document.
        std::error_code ec;
        std::filesystem::directory_iterator it(GetManifestsDir(), ec);
        if (ec) {
            return apps; // Epic not installed / manifests dir unreadable
        }

        for (const std::filesystem::directory_iterator end; it != end; it.increment(ec)) {
            if (ec) {
                break;
            }
            const std::filesystem::directory_entry &entry = *it;

            std::error_code entryEc;
            if (!entry.is_regular_file(entryEc) || entry.path().extension() != ".item") {
                continue;
            }

            try {
                std::ifstream f(entry.path(), std::ios::binary);
                if (!f) {
                    continue;
                }

                nlohmann::json doc;
                f >> doc;

                InstalledApp app;
                app.appName          = doc.value("AppName", std::string {});
                app.displayName      = doc.value("DisplayName", std::string {});
                app.installLocation  = doc.value("InstallLocation", std::string {});
                app.launchExecutable = doc.value("LaunchExecutable", std::string {});
                app.catalogNamespace = doc.value("CatalogNamespace", std::string {});
                app.catalogItemId    = doc.value("CatalogItemId", std::string {});

                if (app.IsValid()) {
                    apps.push_back(std::move(app));
                }
            }
            catch (const std::exception &) {
                continue; // skip a bad manifest
            }
        }

        return apps;
    }

    InstalledApp FindInstalledApp(const std::string &exeFileName, const std::string &appName) {
        // Strip the directory off both sides: the manifest's launch exe is install-root-relative,
        // and exeFileName isn't guaranteed bare elsewhere in the launcher.
        const std::string wantExe = ToLower(FileName(exeFileName));
        const auto apps           = EnumerateInstalledApps();

        const auto it = std::ranges::find_if(apps, [&](const InstalledApp &app) {
            if (!appName.empty()) {
                return app.appName == appName;
            }
            return !wantExe.empty() && ToLower(FileName(app.launchExecutable)) == wantExe;
        });
        return it != apps.end() ? *it : InstalledApp {};
    }
} // namespace Framework::External::Epic
