/*
 * MafiaHub OSS license
 * Copyright (c) 2026, MafiaHub. All rights reserved.
 *
 * This file comes from MafiaHub, hosted at https://github.com/MafiaHub/Framework.
 * See LICENSE file in the source repository for information regarding licensing.
 */

#pragma once

#include <integrations/client/scripting/builtins/fonts.h>
#include <scripting/engine_scope.h>
#include <scripting/resource/resource_manager.h>
#include <scripting/v8_engine.h>
#include <utils/package/package.h>
#include <utils/vfs.h>

#include <string>
#include <utility>
#include <vector>

namespace FontsTests {
    using Framework::Integrations::Client::Scripting::Builtins::Fonts;

    // Only the four-byte tag is read before the atlas parses a face, so a tag and padding stand in for a font.
    inline std::string Face(const char (&tag)[5], size_t size = 256) {
        std::string bytes(tag, 4);
        bytes.resize(size, '\0');
        return bytes;
    }

    inline bool Mount(const std::string &name, std::vector<std::pair<std::string, std::string>> files) {
        Framework::Utils::Package::Writer writer;
        writer.Add("package.json", "{\"name\":\"" + name + "\",\"version\":\"1.0.0\",\"mafiahub\":{\"clientScripts\":[\"client.js\"]}}");
        for (auto &[path, contents] : files) {
            writer.Add(path, contents);
        }
        std::string blob, zip, error;
        return writer.Build(nullptr, blob) && Framework::Utils::Package::Open(blob, nullptr, zip, error) && Framework::Utils::Vfs::Get().MountMemory(std::move(zip), name + ".zip", Framework::Utils::Vfs::ResourcePath(name), error);
    }

    // Called from the resource's own script, so the builtin can tell whose package to read.
    inline const char *kScript = "globalThis.attempt = (...args) => { try { Fonts.register(...args); return 'ok'; } catch (e) { return e.name + ': ' + e.message; } };"
                                 "globalThis.expect = (got, want) => { if (!got.startsWith(want)) throw Error(got + ' != ' + want); };";

    struct Registered {
        std::string family;
        Framework::External::ImGUI::FontFamilySource faces;
    };
} // namespace FontsTests

MODULE(fonts, {
    IT("registers a family from the resource's own files and refuses anything else", {
        using namespace FontsTests;
        auto &vfs = Framework::Utils::Vfs::Get();
        EQUALS(vfs.Init(nullptr), true);
        const std::string regular = Face("\0\1\0\0"), bold = Face("OTTO", 300);
        EQUALS(Mount("typefaces",
                   {{"client.js", kScript}, {"fonts/regular.ttf", regular}, {"fonts/bold.otf", bold}, {"fonts/web.woff2", Face("wOF2")}, {"fonts/notes.txt", std::string(256, 'x')}, {"fonts/huge.ttf", Face("true", Fonts::kMaxFileBytes + 1)}}),
            true);
        EQUALS(Mount("neighbour", {{"client.js", "void 0;"}, {"secret.ttf", Face("true")}}), true);

        std::vector<Registered> registered;
        int cleared = 0;
        Fonts::SetTarget(
            [&](const std::string &family, Framework::External::ImGUI::FontFamilySource faces) {
                registered.push_back({family, std::move(faces)});
            },
            [&] {
                ++cleared;
            });

        Framework::Scripting::V8Engine engine;
        engine.SetModuleRootPath(Framework::Utils::Vfs::kResourceMountRoot);
        EQUALS(engine.Init(), Framework::Scripting::ScriptingError::SCRIPTING_NONE);
        {
            Framework::Scripting::ResourceManagerConfig config;
            config.resourcesPath = Framework::Utils::Vfs::kResourceMountRoot;
            config.isClient      = true;
            Framework::Scripting::ResourceManager manager(&engine, config);
            {
                Framework::Scripting::EngineScope scope(&engine);
                const auto context = scope.Context();
                Fonts::Register(scope.Isolate(), context, context->Global(), &manager);
            }
            EQUALS(manager.DiscoverResources(), 2u);
            EQUALS(static_cast<bool>(manager.StartAll()), true);

            EQUALS(engine.Execute("expect(attempt('Inter', 'fonts/regular.ttf'), 'ok');"), true);
            EQUALS(engine.Execute("expect(attempt('Inter', { regular: './fonts/regular.ttf', bold: 'fonts\\\\bold.otf' }), 'ok');"), true);
            EQUALS(registered.size(), 2u);
            STREQUALS(registered[1].family.c_str(), "Inter");
            EQUALS((registered[1].faces.regular == regular && registered[1].faces.bold == bold && registered[1].faces.italic.empty()), true);

            const char *typeErrors[] {
                "attempt('', 'fonts/regular.ttf')",
                "attempt('x'.repeat(65), 'fonts/regular.ttf')",
                "attempt('Inter')",
                "attempt('Inter', { bold: 'fonts/bold.otf' })",
                "attempt('Inter', { regular: 'fonts/regular.ttf', heavy: 'fonts/bold.otf' })",
                "attempt('Inter', { regular: 'fonts/regular.ttf', italic: 3 })",
            };
            for (const char *call : typeErrors) {
                EQUALS(engine.Execute(std::string("expect(") + call + ", 'TypeError');"), true);
            }
            const char *errors[] {
                "attempt('Inter', 'fonts/missing.ttf'), 'Error: Fonts.register: regular \\'fonts/missing.ttf\\' is not in resource'",
                "attempt('Inter', '../neighbour/secret.ttf'), 'Error: Fonts.register: regular \\'../neighbour/secret.ttf\\' is not a file inside'",
                "attempt('Inter', '../../resources/neighbour/secret.ttf'), 'Error: Fonts.register: regular \\'../../resources/neighbour/secret.ttf\\' is not a file inside'",
                "attempt('Inter', 'fonts/web.woff2'), 'Error: Fonts.register: regular \\'fonts/web.woff2\\' is WOFF/WOFF2'",
                "attempt('Inter', 'fonts/notes.txt'), 'Error: Fonts.register: regular \\'fonts/notes.txt\\' is not a TrueType'",
                "attempt('Inter', 'fonts/huge.ttf'), 'Error: Fonts.register: regular \\'fonts/huge.ttf\\' is larger than 32 MiB'",
                "attempt('Inter', { regular: 'fonts/regular.ttf', italic: 'fonts/web.woff2' }), 'Error: Fonts.register: italic'",
            };
            for (const char *call : errors) {
                EQUALS(engine.Execute(std::string("expect(") + call + ");"), true);
            }
            // A refused call registers none of its faces.
            EQUALS(registered.size(), 2u);

            Fonts::Reset();
            EQUALS(cleared, 1);
            manager.StopAll();
            Fonts::Shutdown();
        }
        Fonts::SetTarget({}, {});
        engine.Shutdown();
        vfs.Unmount("typefaces.zip");
        vfs.Unmount("neighbour.zip");
    });
});
