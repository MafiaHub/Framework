/*
 * MafiaHub OSS license
 * Copyright (c) 2026, MafiaHub. All rights reserved.
 *
 * This file comes from MafiaHub, hosted at https://github.com/MafiaHub/Framework.
 * See LICENSE file in the source repository for information regarding licensing.
 */

#pragma once

#include "lua_client_ut.h"

#include "core_modules.h"
#include "integrations/client/instance.h"
#include "integrations/client/scripting/builtins/chat.h"
#include "integrations/client/scripting/builtins/discord.h"
#include "integrations/client/scripting/builtins/keybinds.h"
#include "integrations/client/scripting/builtins/voice.h"
#include "integrations/server/scripting/builtins/voice.h"
#include "external/imgui/widgets/nametag_view.h"
#include "integrations/client/scripting/builtins/nametags.h"
#include "integrations/client/ui/nametag_notes.h"
#include "scripting/bind/descriptors.h"
#include "scripting/builtins/chat.h"
#include "scripting/engine_scope.h"
#include "scripting/lua/lua_runtime.h"
#include "scripting/scripting_catalog.h"

#include <v8pp/convert.hpp>

#include <cstdio>
#include <memory>
#include <string>
#include <vector>

// The client's Chat, Discord and Nametags from JavaScript and from Lua, registered the way ClientScriptingModule
// registers them. The instance of the client is the stand-in of client_doubles: a real chat box, the lines a
// script sent, and a Discord wrapper that keeps what it was told to publish.
namespace ClientServicesTest {
    using namespace Framework::Scripting;
    using Framework::Utils::Vfs;
    using LuaClientTest::Mount;
    namespace ClientBuiltins = Framework::Integrations::Client::Scripting::Builtins;

    // The scripting of a client with a JavaScript resource `js` and a Lua resource `lua`, both empty.
    struct Stage final {
        // On the heap: the tests of a module share one stack frame.
        std::unique_ptr<Framework::Integrations::Client::Instance> instance = std::make_unique<Framework::Integrations::Client::Instance>();
        std::unique_ptr<Framework::External::Discord::Wrapper> wrapper      = std::make_unique<Framework::External::Discord::Wrapper>();
        LuaClientTest::Client client;
        bool ready = false;

        // `withInstance` false is scripting before the client is up; `withPresence` false a mod without Discord.
        explicit Stage(bool withInstance = true, bool withPresence = true) {
            ClientDoubles::Presence() = {};
            if (withPresence) {
                (void)wrapper->Init(0);
                instance->presence = wrapper.get();
            }
            instance->GetChatBox().SetSessionActive(true);
            if (withInstance) {
                Framework::CoreModules::SetClientInstance(instance.get());
            }
            client.lua.AddRuntimeSetup([](LuaRuntime &runtime) {
                ClientBuiltins::Chat::RegisterLua(runtime);
                ClientBuiltins::Discord::RegisterLua(runtime);
                ClientBuiltins::Nametags::RegisterLua(runtime);
            });
            if (!client.Init()) {
                return;
            }
            {
                EngineScope scope(&client.v8);
                const auto context = scope.Context();
                ClientBuiltins::Chat::Register(scope.Isolate(), context, context->Global(), client.manager.get());
                ClientBuiltins::Discord::Register(scope.Isolate(), context, context->Global(), client.manager.get());
                ClientBuiltins::Nametags::Register(scope.Isolate(), context, context->Global(), client.manager.get());
            }
            ready = Mount("js", R"("clientScripts":["client.js"])", {{"client.js", ""}}).empty() && client.Start("js").empty() && Mount("lua", R"("clientScripts":["client.lua"])", {{"client.lua", ""}}).empty() && client.Start("lua").empty();
        }

        // In the order of ClientScriptingModule::Shutdown.
        ~Stage() {
            if (client.manager) {
                client.manager->StopAll();
                client.manager.reset();
            }
            client.lua.Shutdown();
            ClientBuiltins::Discord::Shutdown();
            ClientBuiltins::Nametags::Shutdown();
            client.v8.Shutdown();
            Framework::CoreModules::SetClientInstance(nullptr);
            Vfs::Get().Unmount("js.zip");
            Vfs::Get().Unmount("lua.zip");
        }

        // What a JavaScript expression gives, as String() spells it, or "<error> " with the name and the message
        // of what it threw.
        std::string Js(const std::string &expression) {
            if (!client.v8.Execute("globalThis.__result = (() => { try { return String(" + expression + "); } catch (e) { return '<error> ' + e.name + ': ' + e.message; } })();", "/resources/js/client.js")) {
                return "<did not run>";
            }
            EngineScope scope(&client.v8);
            const auto context = scope.Context();
            return v8pp::from_v8<std::string>(scope.Isolate(), context->Global()->Get(context, v8pp::to_v8(scope.Isolate(), "__result")).ToLocalChecked());
        }

        // What a Lua expression gives, as tostring() spells it, or "<error> " and what it raised.
        std::string Lua(const std::string &expression) const {
            return client.Eval("lua", expression);
        }

        // The lines scripts sent, each followed by a bar.
        std::string Sent() const {
            std::string lines;
            for (const std::string &line : instance->sent) {
                lines += line + "|";
            }
            return lines;
        }

        // The last activity the wrapper was told to publish, and the fields a script wrote of it.
        static std::string Published() {
            const ClientDoubles::PresenceLog &log = ClientDoubles::Presence();
            const discord::Activity &activity     = log.activity;
            char text[1024];
            std::snprintf(text, sizeof(text), "type=%d name=%s details=%s state=%s start=%lld end=%lld platforms=%u large=%s/%s small=%s/%s party=%s %d/%d privacy=%d secrets=%s|%s|%s instance=%d written=0x%x", static_cast<int>(activity.GetType()), activity.GetName(),
                activity.GetDetails(), activity.GetState(), static_cast<long long>(activity.GetTimestamps().GetStart()), static_cast<long long>(activity.GetTimestamps().GetEnd()), activity.GetSupportedPlatforms(), activity.GetAssets().GetLargeImage(),
                activity.GetAssets().GetLargeText(), activity.GetAssets().GetSmallImage(), activity.GetAssets().GetSmallText(), activity.GetParty().GetId(), activity.GetParty().GetSize().GetCurrentSize(), activity.GetParty().GetSize().GetMaxSize(),
                static_cast<int>(activity.GetParty().GetPrivacy()), activity.GetSecrets().GetMatch(), activity.GetSecrets().GetJoin(), activity.GetSecrets().GetSpectate(), activity.GetInstance() ? 1 : 0, log.written);
            return text;
        }
    };
} // namespace ClientServicesTest

MODULE(lua_client_services, {
    using namespace Framework::Scripting;
    using ClientServicesTest::Stage;
    namespace ClientBuiltins = Framework::Integrations::Client::Scripting::Builtins;

    // What Published() gives for an activity nobody wrote a field of.
    const std::string blank = "type=0 name= details= state= start=0 end=0 platforms=0 large=/ small=/ party= 0/0 privacy=0 secrets=|| instance=0 written=0x0";

    // ==================== Surface ====================

    IT("gives both languages the same Chat and Discord, read-only in Lua", {
        Stage stage;
        EQUALS(stage.ready, true);

        const std::string chat    = "send,setUIVisible,isUIVisible,open,close,isOpen";
        const std::string discord = "setType,setName,setDetails,setState,setStartTimestamp,setEndTimestamp,setSupportedPlatforms,setLargeImage,setLargeText,setSmallImage,setSmallText,setAssets,setPartyId,setPartySize,setPartyPrivacy,setParty,"
                                    "setMatchSecret,setJoinSecret,setSpectateSecret,setSecrets,setInstance,setPresence,update,clear,reset,getUserId,getUsername,isAvailable";
        EQUALS(stage.Js("Object.keys(Chat).join()"), chat);
        EQUALS(stage.Js("Object.keys(Discord).join()"), discord);
        EQUALS(stage.Js("Object.keys(Chat).concat(Object.keys(Discord)).every(name => typeof (Chat[name] || Discord[name]) === 'function')"), "true");

        // A namespace of Lua cannot be listed with pairs: every name JavaScript has is asked for.
        const auto missing = [&stage](const std::string &global, const std::string &names) {
            return stage.Lua("(function() local missing = {} for name in ('" + names + "'):gmatch('[^,]+') do if type(" + global + "[name]) ~= 'function' then missing[#missing + 1] = name end end return table.concat(missing, ',') end)()");
        };
        EQUALS(missing("Chat", chat), "");
        EQUALS(missing("Discord", discord), "");
        EQUALS(stage.Lua("Chat.sendToAll"), "nil");
        EQUALS(stage.Lua("pcall(function() Chat.send = print end)"), "false");
        EQUALS(stage.Lua("pcall(function() Discord.update = nil end)"), "false");
        EQUALS(stage.Lua("pcall(function() Discord.extra = 1 end)"), "false");
        EQUALS(stage.Lua("type(Chat.send)"), "function");
    });

    // The server has a Chat too: another definition of the same name, with other members.
    IT("keeps the Chat of a client and the Chat of a server apart", {
        Stage stage;
        EQUALS(stage.ready, true);
        EQUALS(stage.Lua("type(Chat.send) .. ',' .. type(Chat.sendToAll)"), "function,nil");
        EQUALS(stage.Js("typeof Chat.send + ',' + typeof Chat.sendToAll"), "function,undefined");

        // A state of the server, with its Chat registered as ServerScriptingModule registers it.
        LuaRuntime server(LuaRuntimeTest::Options());
        EQUALS(server.Options().side == Bind::Side::Server, true);
        Builtins::Chat::RegisterLua(server);
        EQUALS(LuaRuntimeTest::Eval(server, "type(Chat.send) .. ',' .. type(Chat.sendToAll) .. ',' .. type(Chat.sendToPlayer)"), "nil,function,function");
        // And the client state still has its own.
        EQUALS(stage.Lua("type(Chat.send) .. ',' .. type(Chat.sendToAll)"), "function,nil");

        // Registering a Chat in a state of the other side does nothing, so the order of registration cannot
        // matter: the Chat of the server never shows in a client state, nor replaces the one that is there.
        LuaRuntimeOptions options = LuaRuntimeTest::Options();
        options.side              = Bind::Side::Client;
        {
            LuaRuntime client(options);
            Builtins::Chat::RegisterLua(client);
            EQUALS(LuaRuntimeTest::Eval(client, "type(Chat)"), "nil");
            ClientBuiltins::Chat::RegisterLua(client);
            EQUALS(LuaRuntimeTest::Eval(client, "type(Chat.send) .. ',' .. type(Chat.sendToAll)"), "function,nil");
        }
        {
            LuaRuntime client(options);
            ClientBuiltins::Chat::RegisterLua(client);
            Builtins::Chat::RegisterLua(client);
            EQUALS(LuaRuntimeTest::Eval(client, "type(Chat.send) .. ',' .. type(Chat.sendToAll)"), "function,nil");
        }
        ClientBuiltins::Chat::RegisterLua(server);
        EQUALS(LuaRuntimeTest::Eval(server, "type(Chat.send) .. ',' .. type(Chat.sendToAll)"), "nil,function");
        {
            LuaRuntime fresh(LuaRuntimeTest::Options());
            ClientBuiltins::Chat::RegisterLua(fresh);
            EQUALS(LuaRuntimeTest::Eval(fresh, "type(Chat)"), "nil");
        }

        // The same in JavaScript: materialised for the other side, a Chat sets nothing and records nothing.
        {
            EngineScope scope(&stage.client.v8);
            const auto context = scope.Context();
            const auto before  = v8pp::metadata::to_json(GetScriptingCatalog(scope.Isolate()));
            EQUALS(Builtins::Chat::Definition().MaterialiseV8(scope.Isolate(), context, context->Global(), GetScriptingCatalog(scope.Isolate()), Bind::Side::Client).IsEmpty(), true);
            EQUALS(ClientBuiltins::Chat::Definition().MaterialiseV8(scope.Isolate(), context, context->Global(), GetScriptingCatalog(scope.Isolate()), Bind::Side::Server).IsEmpty(), true);
            EQUALS(v8pp::metadata::to_json(GetScriptingCatalog(scope.Isolate())) == before, true);
        }
        EQUALS(stage.Js("typeof Chat.send + ',' + typeof Chat.sendToAll"), "function,undefined");

        const Bind::ClassDesc &ofClient = ClientBuiltins::Chat::Definition().Describe();
        const Bind::ClassDesc &ofServer = Builtins::Chat::Definition().Describe();
        EQUALS(&ofClient != &ofServer, true);
        EQUALS(ofClient.name, ofServer.name);
        EQUALS(Bind::ApiCatalog::Get().Find("Chat", Bind::Side::Client) == &ofClient, true);
        EQUALS(Bind::ApiCatalog::Get().Find("Chat", Bind::Side::Server) == &ofServer, true);
        EQUALS(ofClient.side == Bind::Side::Client && ofServer.side == Bind::Side::Server, true);
    });

    // Voice is the other name both sides have. Each definition declares its side, as every builtin of one side does.
    IT("keeps the Voice of a client and the Voice of a server apart", {
        namespace ServerBuiltins = Framework::Integrations::Server::Scripting::Builtins;
        Stage stage;
        EQUALS(stage.ready, true);
        const Bind::ClassDesc &ofClient = ClientBuiltins::Voice::Definition().Describe();
        const Bind::ClassDesc &ofServer = ServerBuiltins::Voice::Definition().Describe();
        EQUALS(&ofClient != &ofServer, true);
        EQUALS(ofClient.name, ofServer.name);
        EQUALS(ofClient.side == Bind::Side::Client && ofServer.side == Bind::Side::Server, true);
        EQUALS(Bind::ApiCatalog::Get().Find("Voice", Bind::Side::Client) == &ofClient, true);
        EQUALS(Bind::ApiCatalog::Get().Find("Voice", Bind::Side::Server) == &ofServer, true);

        // setTier is the client's, setRange the server's.
        const std::string members = "type(Voice.setTier) .. ',' .. type(Voice.setRange)";
        LuaRuntimeOptions options = LuaRuntimeTest::Options();
        options.side              = Bind::Side::Client;
        {
            LuaRuntime client(options);
            ServerBuiltins::Voice::RegisterLua(client);
            EQUALS(LuaRuntimeTest::Eval(client, "type(Voice)"), "nil");
            ClientBuiltins::Voice::RegisterLua(client);
            EQUALS(LuaRuntimeTest::Eval(client, members), "function,nil");
        }
        {
            LuaRuntime client(options);
            ClientBuiltins::Voice::RegisterLua(client);
            ServerBuiltins::Voice::RegisterLua(client);
            EQUALS(LuaRuntimeTest::Eval(client, members), "function,nil");
        }
        {
            LuaRuntime server(LuaRuntimeTest::Options());
            ClientBuiltins::Voice::RegisterLua(server);
            EQUALS(LuaRuntimeTest::Eval(server, "type(Voice)"), "nil");
            ServerBuiltins::Voice::RegisterLua(server);
            EQUALS(LuaRuntimeTest::Eval(server, members), "nil,function");
        }
        {
            LuaRuntime server(LuaRuntimeTest::Options());
            ServerBuiltins::Voice::RegisterLua(server);
            ClientBuiltins::Voice::RegisterLua(server);
            EQUALS(LuaRuntimeTest::Eval(server, members), "nil,function");
        }

        // In JavaScript each materialised for the other side sets nothing and records nothing, before and after
        // the one of the runtime is there.
        {
            EngineScope scope(&stage.client.v8);
            const auto context = scope.Context();
            auto &catalog      = GetScriptingCatalog(scope.Isolate());
            const auto before  = v8pp::metadata::to_json(catalog);
            EQUALS(ServerBuiltins::Voice::Definition().MaterialiseV8(scope.Isolate(), context, context->Global(), catalog, Bind::Side::Client).IsEmpty(), true);
            EQUALS(ClientBuiltins::Voice::Definition().MaterialiseV8(scope.Isolate(), context, context->Global(), catalog, Bind::Side::Server).IsEmpty(), true);
            EQUALS(v8pp::metadata::to_json(catalog) == before, true);
        }
        EQUALS(stage.Js("typeof Voice"), "undefined");
        {
            EngineScope scope(&stage.client.v8);
            const auto context = scope.Context();
            ClientBuiltins::Voice::Register(scope.Isolate(), context, context->Global(), stage.client.manager.get());
            EQUALS(ServerBuiltins::Voice::Definition().MaterialiseV8(scope.Isolate(), context, context->Global(), GetScriptingCatalog(scope.Isolate()), Bind::Side::Client).IsEmpty(), true);
        }
        EQUALS(stage.Js("typeof Voice.setTier + ',' + typeof Voice.setRange"), "function,undefined");

        // What only a client has is nowhere on a server, whatever the sides of its members.
        for (const Bind::ClassDesc *desc : {&ClientBuiltins::Keybinds::Definition().Describe(), &ClientBuiltins::Discord::Definition().Describe(), &ClientBuiltins::Chat::Definition().Describe(), &ofClient}) {
            EQUALS(Bind::ExistsIn(*desc, Bind::Side::Client), true);
            EQUALS(Bind::ExistsIn(*desc, Bind::Side::Server), false);
            EQUALS(Bind::ApiCatalog::Get().Find(desc->name, Bind::Side::Server) != desc, true);
        }
        EQUALS(Bind::ApiCatalog::Get().Find("Key", Bind::Side::Server) == nullptr, true);
        EQUALS(Bind::ApiCatalog::Get().Find("Discord", Bind::Side::Server) == nullptr, true);
        EQUALS(Bind::ApiCatalog::Get().Find("Nametags", Bind::Side::Server) == nullptr, true);
        EQUALS(Bind::ApiCatalog::Get().Find("Nametags", Bind::Side::Client) == &ClientBuiltins::Nametags::Definition().Describe(), true);
    });

    // ==================== Chat ====================

    IT("sends a chat line and refuses what is not text, in both languages", {
        Stage stage;
        EQUALS(stage.ready, true);

        EQUALS(stage.Js("Chat.send('hello')"), "undefined");
        EQUALS(stage.Lua("Chat.send('from lua')"), "nil");
        EQUALS(stage.Js("Chat.send('héllo ✓', 'ignored')"), "undefined");
        EQUALS(stage.Lua("Chat.send('')"), "nil");
        EQUALS(stage.Lua("select('#', Chat.send('counted'))"), "0");
        EQUALS(stage.Sent(), "hello|from lua|héllo ✓||counted|");

        for (const char *wrong : {"", "5", "null", "{}", "undefined, 'text'"}) {
            EQUALS(stage.Js(std::string("Chat.send(") + wrong + ")"), "<error> Error: Chat.send: expected (text)");
        }
        // A number is not a string to Lua here either, though it would print as one.
        for (const char *wrong : {"", "5", "nil", "{}", "nil, 'text'"}) {
            EQUALS(stage.Lua(std::string("Chat.send(") + wrong + ")"), "<error> eval:1: Chat.send: expected (text)");
        }
        EQUALS(stage.Sent(), "hello|from lua|héllo ✓||counted|");
    });

    IT("shows, hides, opens and closes the chat box from both languages", {
        Stage stage;
        EQUALS(stage.ready, true);
        auto &box = stage.instance->GetChatBox();

        EQUALS(stage.Js("Chat.isUIVisible()"), "true");
        EQUALS(stage.Lua("Chat.isUIVisible()"), "true");
        EQUALS(stage.Js("Chat.setUIVisible(false)"), "undefined");
        EQUALS(box.IsVisible(), false);
        EQUALS(stage.Lua("Chat.isUIVisible()"), "false");
        EQUALS(stage.Lua("Chat.setUIVisible(true)"), "nil");
        EQUALS(stage.Js("Chat.isUIVisible()"), "true");
        EQUALS(stage.Lua("Chat.setUIVisible(false)"), "nil");
        EQUALS(stage.Js("Chat.isUIVisible()"), "false");

        // The flag is what the language takes for a condition.
        for (const char *falsy : {"0", "''", "null", "undefined", "NaN", "-0", "0n", "false"}) {
            EQUALS(stage.Js("Chat.setUIVisible(true)"), "undefined");
            EQUALS(stage.Js(std::string("(Chat.setUIVisible(") + falsy + "), Chat.isUIVisible())"), "false");
        }
        for (const char *truthy : {"1", "'x'", "{}", "[]", "() => 1", "1n", "Symbol()", "true"}) {
            EQUALS(stage.Js("Chat.setUIVisible(false)"), "undefined");
            EQUALS(stage.Js(std::string("(Chat.setUIVisible(") + truthy + "), Chat.isUIVisible())"), "true");
        }
        for (const char *falsy : {"nil", "false"}) {
            EQUALS(stage.Lua("Chat.setUIVisible(true)"), "nil");
            EQUALS(stage.Lua(std::string("(function() Chat.setUIVisible(") + falsy + ") return Chat.isUIVisible() end)()"), "false");
        }
        for (const char *truthy : {"0", "''", "{}", "print", "true"}) {
            EQUALS(stage.Lua("Chat.setUIVisible(false)"), "nil");
            EQUALS(stage.Lua(std::string("(function() Chat.setUIVisible(") + truthy + ") return Chat.isUIVisible() end)()"), "true");
        }
        EQUALS(stage.Js("Chat.setUIVisible()"), "<error> Error: Chat.setUIVisible: expected (visible)");
        EQUALS(stage.Lua("Chat.setUIVisible()"), "<error> eval:1: Chat.setUIVisible: expected (visible)");
        EQUALS(box.IsVisible(), true);

        EQUALS(stage.Js("Chat.isOpen()"), "false");
        EQUALS(stage.Js("Chat.open()"), "undefined");
        EQUALS(box.IsInputActive(), true);
        EQUALS(stage.Lua("Chat.isOpen()"), "true");
        EQUALS(stage.Lua("Chat.close()"), "nil");
        EQUALS(stage.Js("Chat.isOpen()"), "false");
        EQUALS(stage.Lua("Chat.open()"), "nil");
        EQUALS(stage.Js("Chat.isOpen()"), "true");
        EQUALS(stage.Js("Chat.close('ignored')"), "undefined");
        EQUALS(stage.Lua("Chat.isOpen()"), "false");
        // Closing from a script never submits what was typed.
        EQUALS(stage.Sent(), "");
    });

    IT("leaves Chat and Discord silent while there is no client instance", {
        Stage stage(/*withInstance*/ false);
        EQUALS(stage.ready, true);
        for (const bool lua : {false, true}) {
            const auto run = [&](const std::string &expression) {
                return lua ? stage.Lua(expression) : stage.Js(expression);
            };
            const std::string nothing = lua ? "nil" : "undefined";
            EQUALS(run("Chat.send('lost')"), nothing);
            EQUALS(run("Chat.setUIVisible(false)"), nothing);
            EQUALS(run("Chat.isUIVisible()"), "false");
            EQUALS(run("Chat.open()"), nothing);
            EQUALS(run("Chat.isOpen()"), "false");
            EQUALS(run("Chat.close()"), nothing);
            EQUALS(run("Discord.isAvailable()"), "false");
            const std::string names = run(lua ? "#Discord.getUserId() + #Discord.getUsername()" : "Discord.getUserId().length + Discord.getUsername().length");
            EQUALS(names, "0");
            EQUALS(run("Discord.setName('staged')"), nothing);
            EQUALS(run("Discord.update()"), "false");
            const std::string presence = run(lua ? "Discord.setPresence({ name = 'x' })" : "Discord.setPresence({ name: 'x' })");
            EQUALS(presence, "false");
            EQUALS(run("Discord.clear()"), "false");
        }
        // The arguments are checked all the same.
        EQUALS(stage.Js("Chat.send()"), "<error> Error: Chat.send: expected (text)");
        EQUALS(stage.Lua("Chat.setUIVisible()"), "<error> eval:1: Chat.setUIVisible: expected (visible)");
        EQUALS(stage.Sent(), "");
        EQUALS(stage.instance->GetChatBox().IsVisible(), true);
        EQUALS(ClientDoubles::Presence().published, 0);
        EQUALS(ClientDoubles::Presence().cleared, 0);
    });

    // ==================== Discord ====================

    IT("stages one activity for both languages, and publishes it on update", {
        Stage stage;
        EQUALS(stage.ready, true);
        ClientDoubles::Presence().userId   = "1234567890123456789";
        ClientDoubles::Presence().username = "tester";

        EQUALS(stage.Js("Discord.isAvailable()"), "true");
        EQUALS(stage.Lua("Discord.isAvailable()"), "true");
        EQUALS(stage.Js("Discord.getUserId() + '/' + Discord.getUsername()"), "1234567890123456789/tester");
        EQUALS(stage.Lua("Discord.getUserId() .. '/' .. Discord.getUsername()"), "1234567890123456789/tester");

        // Half of the fields from each language. Nothing is published before update.
        EQUALS(stage.Js("[Discord.setType('streaming'), Discord.setName('N'), Discord.setState('S'), Discord.setEndTimestamp(20), Discord.setLargeImage('LI'), Discord.setSmallImage('SI'), Discord.setPartyId('P'), Discord.setPartyPrivacy('public'), "
                        "Discord.setJoinSecret('j'), Discord.setInstance(true)].every(result => result === undefined)"),
            "true");
        EQUALS(stage.Lua("(function() Discord.setDetails('D') Discord.setStartTimestamp(10) Discord.setSupportedPlatforms(5) Discord.setLargeText('LT') Discord.setSmallText('ST') Discord.setPartySize(2, 8) Discord.setMatchSecret('m') "
                         "return select('#', Discord.setSpectateSecret('s')) end)()"),
            "0");
        EQUALS(ClientDoubles::Presence().published, 0);
        EQUALS(stage.Lua("Discord.update()"), "true");
        EQUALS(ClientDoubles::Presence().published, 1);
        const std::string all = "type=1 name=N details=D state=S start=10 end=20 platforms=5 large=LI/LT small=SI/ST party=P 2/8 privacy=1 secrets=m|j|s instance=1 written=0x7ff";
        EQUALS(Stage::Published(), all);
        EQUALS(stage.Js("Discord.update(1, 2)"), "true");
        EQUALS(ClientDoubles::Presence().published, 2);
        EQUALS(Stage::Published(), all);

        // reset forgets what was staged without publishing; clear also takes back what was published.
        EQUALS(stage.Lua("Discord.reset()"), "nil");
        EQUALS(ClientDoubles::Presence().published, 2);
        EQUALS(stage.Js("Discord.update()"), "true");
        EQUALS(Stage::Published(), blank);
        EQUALS(stage.Js("Discord.setName('again')"), "undefined");
        EQUALS(stage.Lua("Discord.clear()"), "true");
        EQUALS(ClientDoubles::Presence().cleared, 1);
        EQUALS(stage.Js("Discord.clear()"), "true");
        EQUALS(ClientDoubles::Presence().cleared, 2);
        EQUALS(stage.Lua("Discord.update()"), "true");
        EQUALS(Stage::Published(), blank);
    });

    IT("reads the type, the privacy, the numbers and the flag of an activity the same way in both languages", {
        Stage stage;
        EQUALS(stage.ready, true);
        struct Case final {
            const char *js;
            const char *lua;
            std::string field; // what Published() has for it afterwards
        };
        const Case cases[] = {
            {"Discord.setType('LISTENING')", "Discord.setType('LISTENING')", "type=2 "},
            {"Discord.setType('Watching')", "Discord.setType('Watching')", "type=3 "},
            {"Discord.setType('playing')", "Discord.setType('playing')", "type=0 "},
            {"Discord.setType(2)", "Discord.setType(2)", "type=2 "},
            {"Discord.setType('bogus')", "Discord.setType('bogus')", "type=0 "},
            {"Discord.setType(2.9)", "Discord.setType(2.9)", "type=2 "},
            {"Discord.setType(4294967299)", "Discord.setType(4294967299)", "type=3 "},
            {"Discord.setType(-1)", "Discord.setType(-1)", "type=-1 "},
            {"Discord.setType(true)", "Discord.setType(true)", "type=0 "},
            {"Discord.setType({})", "Discord.setType({})", "type=0 "},
            {"Discord.setType(1)", "Discord.setType(1)", "type=1 "},
            {"Discord.setType(null)", "Discord.setType(nil)", "type=0 "},
            {"Discord.setPartyPrivacy('PUBLIC')", "Discord.setPartyPrivacy('PUBLIC')", "privacy=1 "},
            {"Discord.setPartyPrivacy('private')", "Discord.setPartyPrivacy('private')", "privacy=0 "},
            {"Discord.setPartyPrivacy(1.9)", "Discord.setPartyPrivacy(1.9)", "privacy=1 "},
            {"Discord.setPartyPrivacy('friends')", "Discord.setPartyPrivacy('friends')", "privacy=0 "},
            {"Discord.setPartyPrivacy(1)", "Discord.setPartyPrivacy(1)", "privacy=1 "},
            {"Discord.setPartyPrivacy(null)", "Discord.setPartyPrivacy(nil)", "privacy=0 "},
            {"Discord.setStartTimestamp(1700000000)", "Discord.setStartTimestamp(1700000000)", " start=1700000000 "},
            {"Discord.setStartTimestamp(1.9)", "Discord.setStartTimestamp(1.9)", " start=1 "},
            {"Discord.setStartTimestamp(-1.9)", "Discord.setStartTimestamp(-1.9)", " start=-1 "},
            {"Discord.setStartTimestamp(NaN)", "Discord.setStartTimestamp(0/0)", " start=0 "},
            {"Discord.setStartTimestamp(Infinity)", "Discord.setStartTimestamp(math.huge)", " start=9223372036854775807 "},
            {"Discord.setStartTimestamp(-1e30)", "Discord.setStartTimestamp(-1e30)", " start=-9223372036854775808 "},
            {"Discord.setEndTimestamp(42)", "Discord.setEndTimestamp(42)", " end=42 "},
            {"Discord.setEndTimestamp(0)", "Discord.setEndTimestamp(0)", " end=0 "},
            {"Discord.setSupportedPlatforms(7)", "Discord.setSupportedPlatforms(7)", " platforms=7 "},
            {"Discord.setSupportedPlatforms(-1)", "Discord.setSupportedPlatforms(-1)", " platforms=4294967295 "},
            {"Discord.setSupportedPlatforms(4294967298)", "Discord.setSupportedPlatforms(4294967298)", " platforms=2 "},
            {"Discord.setSupportedPlatforms(2.5)", "Discord.setSupportedPlatforms(2.5)", " platforms=2 "},
            {"Discord.setPartySize(2.9, -1.9)", "Discord.setPartySize(2.9, -1.9)", " 2/-1 "},
            {"Discord.setPartySize(4294967297, 2147483648)", "Discord.setPartySize(4294967297, 2147483648)", " 1/-2147483648 "},
            {"Discord.setPartySize(NaN, Infinity)", "Discord.setPartySize(0/0, math.huge)", " 0/0 "},
            {"Discord.setInstance(true)", "Discord.setInstance(true)", " instance=1 "},
            {"Discord.setInstance(false)", "Discord.setInstance(false)", " instance=0 "},
            {"Discord.setInstance('x')", "Discord.setInstance('x')", " instance=1 "},
            {"Discord.setInstance(null)", "Discord.setInstance(nil)", " instance=0 "},
            {"Discord.setName('x'.repeat(200))", "Discord.setName(('x'):rep(200))", " name=" + std::string(127, 'x') + " details="},
            {"Discord.setName('')", "Discord.setName('')", " name= details="},
        };
        for (const bool lua : {false, true}) {
            for (const Case &entry : cases) {
                const std::string result  = lua ? stage.Lua(entry.lua) : stage.Js(entry.js);
                const std::string nothing = lua ? "nil" : "undefined";
                EQUALS(result, nothing);
                // Published from the other language.
                const std::string updated = lua ? stage.Js("Discord.update()") : stage.Lua("Discord.update()");
                EQUALS(updated, "true");
                const bool staged = LuaClientTest::Contains(Stage::Published(), entry.field);
                if (!staged) {
                    EQUALS(std::string(lua ? entry.lua : entry.js) + " -> " + Stage::Published(), entry.field);
                }
            }
            EQUALS(LuaClientTest::Contains(Stage::Published(), "written=0x693"), true);
            EQUALS(stage.Js("Discord.reset()"), "undefined");
        }

        // A flag that is 0 or empty is false to JavaScript and true to Lua.
        EQUALS(stage.Js("(Discord.setInstance(0), Discord.update())"), "true");
        EQUALS(LuaClientTest::Contains(Stage::Published(), " instance=0 "), true);
        EQUALS(stage.Lua("(function() Discord.setInstance(0) return Discord.update() end)()"), "true");
        EQUALS(LuaClientTest::Contains(Stage::Published(), " instance=1 "), true);

        // A whole number of Lua is read whole, where the same digits are a double to JavaScript.
        EQUALS(stage.Lua("(function() Discord.setStartTimestamp(9007199254740993) return Discord.update() end)()"), "true");
        EQUALS(LuaClientTest::Contains(Stage::Published(), " start=9007199254740993 "), true);
        EQUALS(stage.Js("(Discord.setStartTimestamp(9007199254740993), Discord.update())"), "true");
        EQUALS(LuaClientTest::Contains(Stage::Published(), " start=9007199254740992 "), true);
    });

    IT("merges assets, a party, secrets and a whole presence from an object or a table", {
        const std::string full = "type=3 name=N details=D state=S start=10 end=20 platforms=3 large=LI/LT small=SI/ST party=P 1/2 privacy=1 secrets=m|j|s instance=1 written=0x7ff";
        {
            Stage stage;
            EQUALS(stage.ready, true);
            EQUALS(stage.Js("Discord.setPresence({type: 'watching', name: 'N', details: 'D', state: 'S', startTimestamp: 10, endTimestamp: 20.7, supportedPlatforms: 3, largeImage: 'LI', largeText: 'LT', smallImage: 'SI', smallText: 'ST', instance: true, "
                            "party: {id: 'P', size: [1, 2], privacy: 'public'}, secrets: {match: 'm', join: 'j', spectate: 's'}})"),
                "true");
            EQUALS(Stage::Published(), full);
            EQUALS(ClientDoubles::Presence().published, 1);
        }
        {
            Stage stage;
            EQUALS(stage.ready, true);
            EQUALS(stage.Lua("Discord.setPresence({type = 'watching', name = 'N', details = 'D', state = 'S', startTimestamp = 10, endTimestamp = 20.7, supportedPlatforms = 3, largeImage = 'LI', largeText = 'LT', smallImage = 'SI', smallText = 'ST', "
                             "instance = true, party = {id = 'P', size = {1, 2}, privacy = 'public'}, secrets = {match = 'm', join = 'j', spectate = 's'}})"),
                "true");
            EQUALS(Stage::Published(), full);
            EQUALS(ClientDoubles::Presence().published, 1);
        }

        Stage stage;
        EQUALS(stage.ready, true);
        struct Case final {
            const char *js;
            const char *lua;
            const char *published;
        };
        // Each starts from nothing staged. A member of another type is left out, and so is a size that is not
        // a list of two; an empty object or table changes nothing.
        const Case cases[] = {
            {"Discord.setAssets({largeImage: 'A', largeText: 5, smallImage: 'B', smallText: null})", "Discord.setAssets({largeImage = 'A', largeText = 5, smallImage = 'B'})", "large=A/ small=B/ party= 0/0 privacy=0 secrets=|| instance=0 written=0x60"},
            {"Discord.setAssets({largeText: 'T', other: 1})", "Discord.setAssets({largeText = 'T', other = 1})", "large=/T small=/ party= 0/0 privacy=0 secrets=|| instance=0 written=0x20"},
            {"Discord.setAssets({})", "Discord.setAssets({})", "written=0x0"},
            {"Discord.setAssets([])", "Discord.setAssets({'a', 'b'})", "written=0x0"},
            {"Discord.setAssets(() => 1)", "Discord.setAssets(print)", "written=0x0"},
            {"Discord.setAssets(new Vector3(1, 2, 3))", "Discord.setAssets(Vector3.new(1, 2, 3))", "written=0x0"},
            {"Discord.setParty({id: 'party', size: [3, 9], privacy: 'public'})", "Discord.setParty({id = 'party', size = {3, 9}, privacy = 'public'})", "party=party 3/9 privacy=1 secrets=|| instance=0 written=0x80"},
            {"Discord.setParty({id: 5, size: [1]})", "Discord.setParty({id = 5, size = {1}})", "party= 0/0 privacy=0 secrets=|| instance=0 written=0x0"},
            {"Discord.setParty({size: [4, 5, 6]})", "Discord.setParty({size = {4, 5, 6}})", "party= 4/5 privacy=0 secrets=|| instance=0 written=0x80"},
            {"Discord.setParty({size: 'no'})", "Discord.setParty({size = 'no'})", "party= 0/0 privacy=0 secrets=|| instance=0 written=0x0"},
            {"Discord.setParty({size: {0: 1, 1: 2, length: 2}})", "Discord.setParty({size = {a = 1, b = 2}})", "party= 0/0 privacy=0 secrets=|| instance=0 written=0x0"},
            {"Discord.setParty({size: ['6', ' 7 ']})", "Discord.setParty({size = {'6', ' 7 '}})", "party= 6/7 privacy=0 secrets=|| instance=0 written=0x80"},
            {"Discord.setParty({size: [true, 'x']})", "Discord.setParty({size = {true, 'x'}})", "party= 1/0 privacy=0 secrets=|| instance=0 written=0x80"},
            // A string is a decimal number or nothing, whatever the locale of the process says of a comma.
            {"Discord.setParty({size: ['1.5', ' 2 ']})", "Discord.setParty({size = {'1.5', ' 2 '}})", "party= 1/2 privacy=0 secrets=|| instance=0 written=0x80"},
            {"Discord.setParty({size: ['0x1p3', '1,5']})", "Discord.setParty({size = {'0x1p3', '1,5'}})", "party= 0/0 privacy=0 secrets=|| instance=0 written=0x80"},
            {"Discord.setParty({size: ['+7', '-3e1']})", "Discord.setParty({size = {'+7', '-3e1'}})", "party= 7/-30 privacy=0 secrets=|| instance=0 written=0x80"},
            {"Discord.setParty({size: ['+-7', '']})", "Discord.setParty({size = {'+-7', ''}})", "party= 0/0 privacy=0 secrets=|| instance=0 written=0x80"},
            {"Discord.setParty({size: [1.9, -1.9]})", "Discord.setParty({size = {1.9, -1.9}})", "party= 1/-1 privacy=0 secrets=|| instance=0 written=0x80"},
            {"Discord.setParty({privacy: 1})", "Discord.setParty({privacy = 1})", "party= 0/0 privacy=1 secrets=|| instance=0 written=0x80"},
            {"Discord.setParty({privacy: false})", "Discord.setParty({privacy = false})", "party= 0/0 privacy=0 secrets=|| instance=0 written=0x80"},
            {"Discord.setParty({})", "Discord.setParty({})", "written=0x0"},
            {"Discord.setSecrets({match: 'M', join: 1, spectate: 'S'})", "Discord.setSecrets({match = 'M', join = 1, spectate = 'S'})", "secrets=M||S instance=0 written=0x100"},
            {"Discord.setSecrets({})", "Discord.setSecrets({})", "written=0x0"},
            {"Discord.setPresence({})", "Discord.setPresence({})", "written=0x0"},
            {"Discord.setPresence({type: 3, instance: false})", "Discord.setPresence({type = 3, instance = false})", "type=3 name= details= state= start=0 end=0 platforms=0 large=/ small=/ party= 0/0 privacy=0 secrets=|| instance=0 written=0x201"},
            {"Discord.setPresence({name: 5, details: null, startTimestamp: '1', instance: 1})", "Discord.setPresence({name = 5, startTimestamp = '1', instance = 1})", "written=0x0"},
            {"Discord.setPresence({party: 'x', secrets: 5})", "Discord.setPresence({party = 'x', secrets = 5})", "written=0x0"},
            {"Discord.setPresence({party: {size: [7]}})", "Discord.setPresence({party = {size = {7}}})", "written=0x0"},
            {"Discord.setPresence({party: {size: [7, 8, 9], id: 'Q'}})", "Discord.setPresence({party = {size = {7, 8, 9}, id = 'Q'}})", "party=Q 7/8 privacy=0 secrets=|| instance=0 written=0x80"},
            {"Discord.setPresence({secrets: {match: 5, join: 'JJ'}})", "Discord.setPresence({secrets = {match = 5, join = 'JJ'}})", "secrets=|JJ| instance=0 written=0x100"},
            {"Discord.setPresence([])", "Discord.setPresence({'a'})", "written=0x0"},
        };
        for (const bool lua : {false, true}) {
            for (const Case &entry : cases) {
                EQUALS(stage.Js("Discord.reset()"), "undefined");
                const std::string code   = lua ? entry.lua : entry.js;
                const bool publishes     = code.find("setPresence") != std::string::npos;
                const std::string result = lua ? stage.Lua(code) : stage.Js(code);
                const std::string wanted = publishes ? "true" : lua ? "nil" : "undefined";
                EQUALS(result, wanted);
                if (!publishes) {
                    EQUALS(stage.Lua("Discord.update()"), "true");
                }
                if (!LuaClientTest::Contains(Stage::Published(), entry.published)) {
                    EQUALS(code + " -> " + Stage::Published(), entry.published);
                }
            }
        }

        // What merges adds to what is staged; it replaces nothing it does not name.
        EQUALS(stage.Js("Discord.reset()"), "undefined");
        EQUALS(stage.Js("Discord.setPresence({name: 'kept', party: {id: 'kept'}})"), "true");
        EQUALS(stage.Lua("Discord.setPresence({details = 'added', party = {size = {1, 4}}})"), "true");
        EQUALS(LuaClientTest::Contains(Stage::Published(), "name=kept details=added "), true);
        EQUALS(LuaClientTest::Contains(Stage::Published(), "party=kept 1/4 "), true);

        // null is a value JavaScript can give where Lua has none to give: it is "playing" and "private".
        EQUALS(stage.Js("Discord.setPresence({type: 2, party: {privacy: 1}})"), "true");
        EQUALS(stage.Js("Discord.setPresence({type: null, party: {privacy: null}})"), "true");
        EQUALS(LuaClientTest::Contains(Stage::Published(), "type=0 "), true);
        EQUALS(LuaClientTest::Contains(Stage::Published(), "privacy=0 "), true);
        EQUALS(stage.Js("Discord.setPresence({type: 2, party: {privacy: 1}})"), "true");
        EQUALS(stage.Js("Discord.setPresence({type: undefined, party: {privacy: undefined}})"), "true");
        EQUALS(LuaClientTest::Contains(Stage::Published(), "type=2 "), true);
        EQUALS(LuaClientTest::Contains(Stage::Published(), "privacy=1 "), true);
    });

    IT("throws the same errors for a wrong Discord call in both languages, and stages nothing", {
        Stage stage;
        EQUALS(stage.ready, true);
        struct Case final {
            const char *js;
            const char *lua;
            const char *message;
        };
        const Case cases[] = {
            {"Discord.setType()", "Discord.setType()", "Discord.setType: expected (type)"},
            {"Discord.setName()", "Discord.setName()", "Discord: expected a string argument"},
            {"Discord.setName(5)", "Discord.setName(5)", "Discord: expected a string argument"},
            {"Discord.setDetails(null)", "Discord.setDetails(nil)", "Discord: expected a string argument"},
            {"Discord.setState({})", "Discord.setState({})", "Discord: expected a string argument"},
            {"Discord.setLargeImage(1)", "Discord.setLargeImage(1)", "Discord: expected a string argument"},
            {"Discord.setLargeText()", "Discord.setLargeText()", "Discord: expected a string argument"},
            {"Discord.setSmallImage(true)", "Discord.setSmallImage(true)", "Discord: expected a string argument"},
            {"Discord.setSmallText(null)", "Discord.setSmallText(nil)", "Discord: expected a string argument"},
            {"Discord.setPartyId(1)", "Discord.setPartyId(1)", "Discord: expected a string argument"},
            {"Discord.setMatchSecret()", "Discord.setMatchSecret()", "Discord: expected a string argument"},
            {"Discord.setJoinSecret(5)", "Discord.setJoinSecret(5)", "Discord: expected a string argument"},
            {"Discord.setSpectateSecret([])", "Discord.setSpectateSecret({})", "Discord: expected a string argument"},
            {"Discord.setStartTimestamp()", "Discord.setStartTimestamp()", "Discord: expected a numeric argument"},
            {"Discord.setStartTimestamp('5')", "Discord.setStartTimestamp('5')", "Discord: expected a numeric argument"},
            {"Discord.setEndTimestamp(null)", "Discord.setEndTimestamp(nil)", "Discord: expected a numeric argument"},
            {"Discord.setSupportedPlatforms('1')", "Discord.setSupportedPlatforms('1')", "Discord: expected a numeric argument"},
            {"Discord.setPartySize()", "Discord.setPartySize()", "Discord.setPartySize: expected (current, max)"},
            {"Discord.setPartySize(1)", "Discord.setPartySize(1)", "Discord.setPartySize: expected (current, max)"},
            {"Discord.setPartySize('1', 2)", "Discord.setPartySize('1', 2)", "Discord.setPartySize: expected (current, max)"},
            {"Discord.setPartySize(1, '2')", "Discord.setPartySize(1, '2')", "Discord.setPartySize: expected (current, max)"},
            {"Discord.setPartyPrivacy()", "Discord.setPartyPrivacy()", "Discord.setPartyPrivacy: expected (privacy)"},
            {"Discord.setInstance()", "Discord.setInstance()", "Discord.setInstance: expected (bool)"},
            {"Discord.setAssets()", "Discord.setAssets()", "Discord.setAssets: expected an object"},
            {"Discord.setAssets(5)", "Discord.setAssets(5)", "Discord.setAssets: expected an object"},
            {"Discord.setAssets('str')", "Discord.setAssets('str')", "Discord.setAssets: expected an object"},
            {"Discord.setAssets(null)", "Discord.setAssets(nil)", "Discord.setAssets: expected an object"},
            {"Discord.setAssets(true)", "Discord.setAssets(true)", "Discord.setAssets: expected an object"},
            {"Discord.setParty()", "Discord.setParty()", "Discord.setParty: expected an object"},
            {"Discord.setParty('x')", "Discord.setParty('x')", "Discord.setParty: expected an object"},
            {"Discord.setSecrets()", "Discord.setSecrets()", "Discord.setSecrets: expected an object"},
            {"Discord.setSecrets(1)", "Discord.setSecrets(1)", "Discord.setSecrets: expected an object"},
            {"Discord.setPresence()", "Discord.setPresence()", "Discord.setPresence: expected an options object"},
            {"Discord.setPresence('x')", "Discord.setPresence('x')", "Discord.setPresence: expected an options object"},
            {"Discord.setPresence(null)", "Discord.setPresence(nil)", "Discord.setPresence: expected an options object"},
        };
        for (const Case &entry : cases) {
            EQUALS(stage.Js(entry.js), std::string("<error> Error: ") + entry.message);
            EQUALS(stage.Lua(entry.lua), std::string("<error> eval:1: ") + entry.message);
        }
        EQUALS(ClientDoubles::Presence().published, 0);
        EQUALS(stage.Js("Discord.update()"), "true");
        EQUALS(Stage::Published(), blank);
    });

    // Reading a member may run the script: nothing of Discord is locked while it does.
    IT("lets a getter of an options object call Discord, and passes on what it throws", {
        Stage stage;
        EQUALS(stage.ready, true);
        EQUALS(stage.Js("Discord.setPresence({get name() { Discord.setDetails('from the getter'); return 'outer'; }})"), "true");
        EQUALS(LuaClientTest::Contains(Stage::Published(), "name=outer details=from the getter "), true);
        EQUALS(stage.Js("Discord.setAssets(Object.create({largeImage: 'inherited'}))"), "undefined");

        // The members read after the one that threw are staged all the same, as they always were. Nothing is
        // published by the call that throws.
        const int published = ClientDoubles::Presence().published;
        EQUALS(stage.Js("Discord.setAssets({get largeText() { throw new RangeError('boom'); }, smallImage: 'after'})"), "<error> RangeError: boom");
        EQUALS(ClientDoubles::Presence().published, published);
        EQUALS(stage.Lua("Discord.update()"), "true");
        EQUALS(LuaClientTest::Contains(Stage::Published(), "large=inherited/ small=after/ "), true);
    });

    IT("answers false and an empty name while the mod has no Discord, and stages all the same", {
        Stage stage(/*withInstance*/ true, /*withPresence*/ false);
        EQUALS(stage.ready, true);
        EQUALS(stage.Js("Discord.isAvailable()"), "false");
        EQUALS(stage.Lua("Discord.isAvailable()"), "false");
        EQUALS(stage.Js("Discord.getUserId() + Discord.getUsername()"), "");
        EQUALS(stage.Lua("Discord.getUserId() .. Discord.getUsername()"), "");
        EQUALS(stage.Js("Discord.setName('early')"), "undefined");
        EQUALS(stage.Lua("Discord.update()"), "false");
        EQUALS(stage.Js("Discord.setPresence({details: 'early too'})"), "false");
        EQUALS(stage.Lua("Discord.setPresence()"), "<error> eval:1: Discord.setPresence: expected an options object");
        EQUALS(ClientDoubles::Presence().published, 0);

        // Discord connects: what was staged is still there.
        (void)stage.wrapper->Init(0);
        stage.instance->presence = stage.wrapper.get();
        EQUALS(stage.Lua("Discord.update()"), "true");
        EQUALS(LuaClientTest::Contains(Stage::Published(), "name=early details=early too "), true);

        // And goes: clear forgets what was staged, though nothing can be taken back.
        stage.wrapper->Shutdown();
        EQUALS(stage.Js("Discord.clear()"), "false");
        EQUALS(ClientDoubles::Presence().cleared, 0);
        (void)stage.wrapper->Init(0);
        EQUALS(stage.Js("Discord.update()"), "true");
        EQUALS(Stage::Published(), blank);
    });

    // What ClientScriptingModule does when the client disconnects or closes.
    IT("forgets the staged activity and takes back the published one on a shutdown", {
        Stage stage;
        EQUALS(stage.ready, true);
        EQUALS(stage.Js("Discord.setPresence({name: 'js'})"), "true");
        EQUALS(stage.Lua("Discord.setDetails('lua')"), "nil");
        EQUALS(ClientDoubles::Presence().cleared, 0);

        ClientBuiltins::Discord::Shutdown();
        EQUALS(ClientDoubles::Presence().cleared, 1);
        EQUALS(stage.Lua("Discord.update()"), "true");
        EQUALS(Stage::Published(), blank);

        // A resource that stops takes nothing with it: the activity is the client's, not a resource's.
        EQUALS(stage.Lua("Discord.setName('of lua')"), "nil");
        EQUALS(static_cast<bool>(stage.client.manager->StopResource("lua")), true);
        EQUALS(stage.Js("Discord.update()"), "true");
        EQUALS(LuaClientTest::Contains(Stage::Published(), "name=of lua "), true);
    });

    // ==================== Nametags ====================

    IT("gives both languages the same Nametags, read-only in Lua", {
        Stage stage;
        EQUALS(stage.ready, true);
        const std::string names = "setVisible,isVisible,setHealthVisible,isHealthVisible,setSelfVisible,isSelfVisible,setLabel,clearLabel,clearLabels";
        EQUALS(stage.Js("Object.keys(Nametags).join()"), names);
        EQUALS(stage.Lua("(function() local missing = {} for name in ('" + names + "'):gmatch('[^,]+') do if type(Nametags[name]) ~= 'function' then missing[#missing + 1] = name end end return table.concat(missing, ',') end)()"), "");
        EQUALS(stage.Lua("pcall(function() Nametags.setVisible = print end)"), "false");
    });

    IT("switches the nametags, their health bars and the player's own from both languages", {
        using Framework::External::ImGUI::Widgets::NameTagView;
        Stage stage;
        EQUALS(stage.ready, true);
        NameTagView::showTags   = true;
        NameTagView::showHealth = true;
        NameTagView::showSelf   = false;
        struct Switch final {
            const char *set;
            const char *is;
            bool *value;
        };
        const Switch switches[] = {{"Nametags.setVisible", "Nametags.isVisible", &NameTagView::showTags}, {"Nametags.setHealthVisible", "Nametags.isHealthVisible", &NameTagView::showHealth}, {"Nametags.setSelfVisible", "Nametags.isSelfVisible", &NameTagView::showSelf}};
        for (const Switch &entry : switches) {
            const std::string set = entry.set;
            const std::string is  = std::string(entry.is) + "()";
            EQUALS(stage.Js(set + "(false)"), "undefined");
            EQUALS(*entry.value, false);
            EQUALS(stage.Lua(is), "false");
            EQUALS(stage.Lua(set + "(true)"), "nil");
            EQUALS(*entry.value, true);
            EQUALS(stage.Js(is), "true");
            EQUALS(stage.Lua(set + "(false)"), "nil");
            EQUALS(stage.Js(is), "false");
            EQUALS(stage.Js(set + "(1)"), "undefined");
            EQUALS(stage.Lua(is), "true");

            // The flag is what the language takes for a condition.
            EQUALS(stage.Js("(" + set + "(0), " + is + ")"), "false");
            EQUALS(stage.Js("(" + set + "('x'), " + is + ")"), "true");
            EQUALS(stage.Js("(" + set + "(''), " + is + ")"), "false");
            EQUALS(stage.Js("(" + set + "({}), " + is + ")"), "true");
            EQUALS(stage.Js("(" + set + "(undefined), " + is + ")"), "false");
            EQUALS(stage.Lua("(function() " + set + "(0) return " + is + " end)()"), "true");
            EQUALS(stage.Lua("(function() " + set + "(nil) return " + is + " end)()"), "false");
            EQUALS(stage.Lua("(function() " + set + "('') return " + is + " end)()"), "true");

            const std::string expected = std::string(entry.set) + ": expected (visible)";
            EQUALS(stage.Js(set + "()"), "<error> Error: " + expected);
            EQUALS(stage.Lua(set + "()"), "<error> eval:1: " + expected);
            EQUALS(*entry.value, true);
        }
        // Each switch is its own.
        EQUALS(stage.Js("Nametags.setHealthVisible(false)"), "undefined");
        EQUALS(stage.Lua("tostring(Nametags.isVisible()) .. tostring(Nametags.isHealthVisible()) .. tostring(Nametags.isSelfVisible())"), "truefalsetrue");
        NameTagView::showTags   = true;
        NameTagView::showHealth = true;
        NameTagView::showSelf   = false;
    });

    IT("hangs a label on a nametag and takes it off, from both languages", {
        Stage stage;
        EQUALS(stage.ready, true);
        auto &notes = Framework::Integrations::Client::UI::Nametags::Notes();
        notes.ClearAll();
        // What the note of an entity is: its text, whether it is timed, and its colour.
        const auto note = [&notes](uint64_t id) {
            const auto *found = notes.Find(id);
            return found != nullptr ? found->text + (found->timed ? " timed " : " held ") + std::to_string(found->color) : std::string("none");
        };

        for (const bool lua : {false, true}) {
            const auto run = [&](const std::string &expression) {
                return lua ? stage.Lua(expression) : stage.Js(expression);
            };
            const std::string nothing = lua ? "nil" : "undefined";
            const std::string absent  = lua ? "nil" : "null";
            EQUALS(run("Nametags.setLabel(7, 'hello')"), nothing);
            EQUALS(note(7), "hello timed 0");
            EQUALS(run("Nametags.setLabel(7, 'held', 0)"), nothing);
            EQUALS(note(7), "held held 0");
            EQUALS(run("Nametags.setLabel(7, 'held', -5)"), nothing);
            EQUALS(note(7), "held held 0");
            EQUALS(run("Nametags.setLabel(7, 'default', 'x')"), nothing);
            EQUALS(note(7), "default timed 0");
            EQUALS(run("Nametags.setLabel(7, 'coloured', " + absent + ", 5)"), nothing);
            EQUALS(note(7), "coloured timed 5");
            EQUALS(run("Nametags.setLabel(7, 'coloured', 100, 0xFF00FF00)"), nothing);
            EQUALS(note(7), "coloured timed 4278255360");
            EQUALS(run("Nametags.setLabel(7, 'coloured', 100, 1.9)"), nothing);
            EQUALS(note(7), "coloured timed 1");
            EQUALS(run("Nametags.setLabel(7, 'coloured', 100, -1)"), nothing);
            EQUALS(note(7), "coloured timed 4294967295");
            EQUALS(run("Nametags.setLabel(7, 'coloured', 100, 'x')"), nothing);
            EQUALS(note(7), "coloured timed 0");
            // An id is its whole part; an empty text takes the label off.
            EQUALS(run("Nametags.setLabel(8.9, 'whole part')"), nothing);
            EQUALS(note(8), "whole part timed 0");
            EQUALS(run("Nametags.setLabel(-1, 'last')"), nothing);
            EQUALS(note(~uint64_t {0}), "last timed 0");
            EQUALS(run("Nametags.setLabel(7, '')"), nothing);
            EQUALS(note(7), "none");
            EQUALS(run("Nametags.clearLabel(8.5)"), nothing);
            EQUALS(note(8), "none");
            EQUALS(notes.Empty(), false);
            EQUALS(run("Nametags.clearLabels('ignored')"), nothing);
            EQUALS(notes.Empty(), true);

            const std::string thrown = lua ? "<error> eval:1: " : "<error> Error: ";
            const char *label        = "Nametags.setLabel: expected (entityId, text, durationMs?, color?)";
            for (const char *wrong : {"", "7", "'7', 'x'", "7, 5", "{}, 'x'"}) {
                EQUALS(run(std::string("Nametags.setLabel(") + wrong + ")"), thrown + label);
            }
            for (const char *wrong : {"", "'8'", "{}"}) {
                EQUALS(run(std::string("Nametags.clearLabel(") + wrong + ")"), thrown + "Nametags.clearLabel: expected (entityId)");
            }
            EQUALS(notes.Empty(), true);
        }

        // A label is cut at 512 bytes, and never inside a character.
        EQUALS(stage.Js("Nametags.setLabel(1, 'x'.repeat(600))"), "undefined");
        EQUALS(notes.Find(1)->text.size(), 512u);
        EQUALS(stage.Lua("Nametags.setLabel(2, ('x'):rep(511) .. 'é')"), "nil");
        EQUALS(notes.Find(2)->text, std::string(511, 'x'));
        EQUALS(stage.Js("Nametags.setLabel(3, 'x'.repeat(510) + 'é✓')"), "undefined");
        EQUALS(notes.Find(3)->text, std::string(510, 'x') + "é");
        EQUALS(stage.Lua("Nametags.setLabel(4, ('x'):rep(512))"), "nil");
        EQUALS(notes.Find(4)->text.size(), 512u);

        // One store for both languages; a whole number of Lua is an id as it stands.
        EQUALS(stage.Lua("Nametags.clearLabel(1)"), "nil");
        EQUALS(notes.Find(1) == nullptr, true);
        EQUALS(stage.Lua("Nametags.setLabel(9007199254740993, 'exact')"), "nil");
        EQUALS(note(9007199254740993ull), "exact timed 0");
        EQUALS(stage.Js("Nametags.clearLabels()"), "undefined");
        EQUALS(notes.Empty(), true);

        // What ClientScriptingModule does when the client disconnects or closes.
        EQUALS(stage.Js("Nametags.setLabel(1, 'js')"), "undefined");
        EQUALS(stage.Lua("Nametags.setLabel(2, 'lua')"), "nil");
        ClientBuiltins::Nametags::Shutdown();
        EQUALS(notes.Empty(), true);
    });
})
