/*
 * MafiaHub OSS license
 * Copyright (c) 2021-2023, MafiaHub. All rights reserved.
 *
 * This file comes from MafiaHub, hosted at https://github.com/MafiaHub/Framework.
 * See LICENSE file in the source repository for information regarding licensing.
 */

#pragma once

#include "platform.h"

#include "launcher/loaders/image_snapshot.h"

#include <external/epic/manifest.h>
#include <external/steam/wrapper.h>
#include <function2/function2.hpp>

#include <memory>
#include <optional>
#include <string>

namespace Framework::Launcher::Platforms {
    // The Steam client is asked for the app's install directory
    class Steam final: public Platform {
      public:
        struct Options {
            AppId_t appId;

            // a game folder the player picked that holds steam_api is started as the Steam copy
            bool adoptManualCopies = false;
        };

        explicit Steam(Options options): _options(options) {}

        const char *GetName() const override {
            return "Steam";
        }

        PlatformCheckStatus Resolve(const PlatformHost &host, PlatformResolution &resolution, bool reportErrors) override;
        bool AdoptManualCopy(const PlatformHost &host, PlatformResolution &resolution) override;
        bool PrepareLaunch(const PlatformHost &host) override;

      private:
        void PrepareAppIdentity(const PlatformHost &host) const;

        Options _options;
        bool _tried = false;
    };

    // The Epic launcher's manifests, matched by `appName` (the catalog's "AppName"), else by the
    // launch executable's file name. Outside that launcher the game's EOS ownership check wants what
    // it would pass, so the player is signed in to Epic before the game starts.
    class Epic final: public Platform {
      public:
        using SignInProc = fu2::function<bool() const>;

        struct Options {
            std::wstring appName;

            // signs the player in when no stored Epic sign-in refreshes, persisting it through
            // External::Epic::SignInWithAuthorizationCode; unset, the Framework's epic_sign_in.exe
            // window beside the launcher, else a browser + clipboard sign-in
            SignInProc signIn;
        };

        explicit Epic(Options options = {}): _options(std::move(options)) {}

        const char *GetName() const override {
            return "Epic";
        }

        PlatformCheckStatus Resolve(const PlatformHost &host, PlatformResolution &resolution, bool reportErrors) override;
        bool PrepareLaunch(const PlatformHost &host) override;

        std::wstring GetLaunchArguments() const override {
            return _launchArguments;
        }

      private:
        Options _options;
        External::Epic::InstalledApp _app;
        std::wstring _launchArguments;
    };

    // The Rockstar Games Launcher's registry entries, matched by `titleKey` (e.g. L"GTA: San
    // Andreas"), else by the title holding the executable. See the README for the image snapshot.
    class Rockstar final: public Platform {
      public:
        // Captures the snapshot on the first run of a build. Naming Loaders::CaptureImageSnapshot
        // here is what links the process-reading code into that launcher and no other.
        using ImageSnapshotCaptureProc = bool (*)(Loaders::ImageSnapshot &snapshot, const std::wstring &gamePath, const std::wstring &executableName, const std::vector<uint8_t> &sourceImage);

        struct Options {
            std::wstring titleKey;

            // capture the code the title's wrapper decrypts from a launcher-authorised run and
            // replay it, so the mapped image can be entered past the stub; a copy the player picks
            // is then started the same way
            bool useImageSnapshot                 = false;
            ImageSnapshotCaptureProc captureImage = nullptr;
        };

        explicit Rockstar(Options options = {}): _options(std::move(options)) {}

        const char *GetName() const override {
            return "Rockstar Games Launcher";
        }

        PlatformCheckStatus Resolve(const PlatformHost &host, PlatformResolution &resolution, bool reportErrors) override;
        bool AdoptManualCopy(const PlatformHost &host, PlatformResolution &resolution) override;
        bool PrepareImage(const PlatformHost &host, const std::wstring &executablePath, std::span<const uint8_t> image) override;
        void OnSectionsMapped(HMODULE module) override;
        uintptr_t ResolveEntryPoint(uintptr_t imageBase, uintptr_t entryPoint) override;

      private:
        Options _options;
        std::optional<Loaders::ImageSnapshot> _snapshot;
    };

    // The Microsoft Store / Xbox app (PC Game Pass) package of `packageFamily` (e.g.
    // L"Publisher.Game_hash") installed for the current user. Its executable is licence-protected
    // and its runtime tied to the package identity, so the launcher relaunches itself inside the
    // package, as the Application `appId` its manifest declares, and that copy maps the game.
    class MicrosoftStore final: public Platform {
      public:
        struct Options {
            std::wstring packageFamily;
            std::wstring appId = L"App";
        };

        explicit MicrosoftStore(Options options): _options(std::move(options)) {}

        const char *GetName() const override {
            return "Microsoft Store";
        }

        PlatformCheckStatus Resolve(const PlatformHost &host, PlatformResolution &resolution, bool reportErrors) override;

      private:
        Options _options;
    };

    // The game path stored in the launcher's JSON config, or one the player selects when prompting
    class Classic final: public Platform {
      public:
        using SelectionProc = fu2::function<std::wstring(std::wstring gameRoot) const>;

        struct Options {
            bool prompt            = true;
            std::string title      = "Select your game's executable";
            std::string filter     = "Game.exe";
            std::string filterName = "Your Game.exe";
            std::string extension  = "*.exe";

            // rewrites the picked game root before it is checked
            SelectionProc onSelection;
        };

        explicit Classic(Options options = {}): _options(std::move(options)) {}

        const char *GetName() const override {
            return "Game folder";
        }

        bool IsManualSelection() const override {
            return true;
        }

        PlatformCheckStatus Resolve(const PlatformHost &host, PlatformResolution &resolution, bool reportErrors) override;

      private:
        bool Prompt(const PlatformHost &host, std::wstring &gameRoot) const;

        Options _options;
    };
} // namespace Framework::Launcher::Platforms
