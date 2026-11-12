/*
 * MafiaHub OSS license
 * Copyright (c) 2026, MafiaHub. All rights reserved.
 *
 * This file comes from MafiaHub, hosted at https://github.com/MafiaHub/Framework.
 * See LICENSE file in the source repository for information regarding licensing.
 */

#pragma once

#include "lua_builtins_core_ut.h"
#include "lua_builtins_player_ut.h"
#include "text_label_ut.h"

#include "core_modules.h"
#include "networking/network_server.h"
#include "networking/rpc/chat_message.h"
#include "scripting/builtins/chat.h"

#include <chrono>
#include <cstdio>
#include <string>
#include <thread>
#include <vector>

// Chat, declared once for JavaScript and Lua: what a Lua script sends with it, held against what a JavaScript one
// sends, as the clients of a real server receive it.
namespace LuaBuiltinsChatTest {
    // Gives the runtimes of a stage of the Player tests the Chat of the server module.
    inline void GiveChat(LuaBuiltinsPlayerTest::JsSide &js, LuaBuiltinsPlayerTest::LuaSide &lua) {
        js.js.Enter([](v8::Isolate *isolate, v8::Local<v8::Context> context) {
            Framework::Scripting::Builtins::Chat::Register(isolate, context->Global());
        });
        Framework::Scripting::Builtins::Chat::RegisterLua(lua.Runtime());
    }

    // A server two clients are connected to over loopback, published as the network peer the builtins send
    // through. Each client has an avatar, `p0` and `p1` to the scripts of both languages, and `e0` is the first
    // of them as a plain Entity.
    struct Wire final {
        TextLabelWorld world;
        Framework::Networking::NetworkServer clients[2];
        std::vector<Framework::Networking::RPC::ChatMessage> received[2];
        Framework::Networking::Replication::NetworkEntity *avatars[2] = {nullptr, nullptr};
        LuaBuiltinsPlayerTest::JsSide js;
        LuaBuiltinsPlayerTest::LuaSide lua;
        bool ready = false;

        Wire(): lua(Framework::Scripting::Bind::Side::Server, &js.manager) {
            GiveChat(js, lua);

            Framework::Networking::NetworkServer &server = world.peer;
            world.Manager()->SetAutoManageConnections(false, true);
            MafiaNet::SocketDescriptor serverSocket(0, "127.0.0.1");
            if (server.GetPeer()->Startup(2, &serverSocket, 1) != MafiaNet::RAKNET_STARTED) {
                return;
            }
            server.GetPeer()->SetMaximumIncomingConnections(2);

            std::vector<Framework::Networking::Replication::NetworkEntity *> connected;
            server.SetOnPlayerConnectCallback([&](MafiaNet::Packet *packet) {
                server.PushReplicationConnection(packet->guid);
                Framework::Networking::Replication::NetworkEntity *avatar = world.Manager()->CreateEntity(world.targetType);
                avatar->ownerGUID     = MafiaNet::ToPeerGuid(packet->guid);
                world.Manager()->SetViewer(avatar->ownerGUID, avatar);
                connected.push_back(avatar);
            });

            const auto port = server.GetPeer()->GetMyBoundAddress().GetPort();
            for (int i = 0; i < 2; ++i) {
                MafiaNet::SocketDescriptor socket(0, "127.0.0.1");
                if (clients[i].GetPeer()->Startup(1, &socket, 1) != MafiaNet::RAKNET_STARTED) {
                    return;
                }
                clients[i].GetReplicationManager()->Init(&clients[i], false);
                clients[i].RegisterRPC<Framework::Networking::RPC::ChatMessage>([this, i](const Framework::Networking::RPC::ChatMessage &message, MafiaNet::Packet *) {
                    received[i].push_back(message);
                });
                if (clients[i].GetPeer()->Connect("127.0.0.1", port, nullptr, 0) != MafiaNet::CONNECTION_ATTEMPT_STARTED) {
                    return;
                }
            }
            const bool joined = PumpUntil([&] {
                return connected.size() == 2;
            });
            server.SetOnPlayerConnectCallback({});
            if (!joined) {
                return;
            }
            for (int i = 0; i < 2; ++i) {
                const auto guid = MafiaNet::ToPeerGuid(clients[i].GetPeer()->GetMyGUID());
                for (Framework::Networking::Replication::NetworkEntity *avatar : connected) {
                    if (avatar->ownerGUID == guid) {
                        avatars[i] = avatar;
                    }
                }
                if (avatars[i] == nullptr) {
                    return;
                }
            }

            const std::string a = std::to_string(avatars[0]->GetNetworkID());
            const std::string b = std::to_string(avatars[1]->GetNetworkID());
            lua.Run("p0 = Player.new(" + a + ") p1 = Player.new(" + b + ") e0 = Entity.new(" + a + ")");
            js.Eval("globalThis.p0 = new Player(" + a + "); globalThis.p1 = new Player(" + b + "); globalThis.e0 = new Entity(" + a + "); 0");

            Framework::CoreModules::SetNetworkPeer(&server);
            ready = lua.ready;
        }

        ~Wire() {
            Framework::CoreModules::SetNetworkPeer(nullptr);
            for (Framework::Networking::NetworkServer &client : clients) {
                client.GetPeer()->Shutdown(0);
                client.GetReplicationManager()->Clear();
            }
            world.peer.GetPeer()->Shutdown(0);
        }

        void Pump() {
            world.peer.Update();
            clients[0].Update();
            clients[1].Update();
        }

        template <typename Done>
        bool PumpUntil(const Done &done) {
            for (int i = 0; i < 2500; ++i) {
                Pump();
                if (done()) {
                    return true;
                }
                std::this_thread::sleep_for(std::chrono::milliseconds(2));
            }
            return false;
        }

        // What the clients were sent since the last call, once `expected` lines arrived and a while after it, in
        // which a line too many would show: "0<text|author|rrggbbaa> 1<...>", a line per client it reached, in
        // the order it reached it.
        std::string Delivered(size_t expected) {
            PumpUntil([&] {
                return received[0].size() + received[1].size() >= expected;
            });
            int passes = 0;
            PumpUntil([&] {
                return ++passes == 40;
            });
            std::string out;
            for (int i = 0; i < 2; ++i) {
                for (const Framework::Networking::RPC::ChatMessage &message : received[i]) {
                    char color[16];
                    std::snprintf(color, sizeof(color), "%08x", message.color);
                    out += (out.empty() ? "" : " ") + std::to_string(i) + "<" + message.text + "|" + message.author + "|" + color + (message.senderNetworkId != 0 ? "|sender" : "") + ">";
                }
                received[i].clear();
            }
            return out;
        }
    };
} // namespace LuaBuiltinsChatTest

MODULE(lua_builtins_chat, {
    IT("gives a Lua state the Chat of JavaScript, under the same names", {
        EQUALS(LuaBuiltinsCoreTest::Members(Framework::Scripting::Builtins::Chat::Definition()), "sendToAll(),sendToPlayer()");

        LuaBuiltinsPlayerTest::Stage stage;
        EQUALS(stage.lua.ready, true);
        LuaBuiltinsChatTest::GiveChat(stage.js, stage.lua);

        EQUALS(stage.lua.Eval("type(Chat) .. ' ' .. type(Chat.sendToAll) .. ' ' .. type(Chat.sendToPlayer)"), "table function function");
        EQUALS(stage.js.Eval("typeof Chat + ' ' + typeof Chat.sendToAll + ' ' + typeof Chat.sendToPlayer"), "object function function");
        EQUALS(stage.js.Eval("Object.keys(Chat).join()"), "sendToAll,sendToPlayer");

        // A Lua script reads Chat and cannot replace a function of it. A JavaScript one can add to the object, as
        // a project that extends the Chat of the framework does.
        EQUALS(stage.lua.Eval("(function() Chat.sendToAll = print end)()"), "<error> eval:1: attempt to set 'sendToAll' on Chat");
        EQUALS(stage.js.Eval("Chat.setDefaultRelay = () => 'relay'; Chat.setDefaultRelay()"), "relay");
    });

    IT("refuses a call that names no text or no player, in the words of JavaScript", {
        LuaBuiltinsPlayerTest::Stage stage;
        LuaBuiltinsChatTest::GiveChat(stage.js, stage.lua);
        // A handle outlives its entity.
        Framework::Networking::Replication::NetworkEntity *leaver = stage.world.Manager()->CreateEntity(stage.world.targetType);
        const std::string id  = std::to_string(leaver->GetNetworkID());
        stage.lua.Run("stale = Player.new(" + id + ")");
        stage.js.Eval("globalThis.stale = new Player(" + id + "); 0");
        stage.world.Manager()->DestroyEntity(leaver);

        struct Case final {
            const char *lua;
            const char *js;
            const char *kind;
            const char *message;
        };
        const Case cases[] = {
            {"Chat.sendToAll()", "Chat.sendToAll()", "TypeError", "Chat.sendToAll: expected (text, opts?)"},
            {"Chat.sendToAll(5)", "Chat.sendToAll(5)", "TypeError", "Chat.sendToAll: expected (text, opts?)"},
            {"Chat.sendToAll({ author = 'a' })", "Chat.sendToAll({ author: 'a' })", "TypeError", "Chat.sendToAll: expected (text, opts?)"},
            {"Chat.sendToAll(nil, 'text')", "Chat.sendToAll(undefined, 'text')", "TypeError", "Chat.sendToAll: expected (text, opts?)"},
            {"Chat.sendToPlayer()", "Chat.sendToPlayer()", "TypeError", "Chat.sendToPlayer: expected (player, text, opts?)"},
            {"Chat.sendToPlayer(p)", "Chat.sendToPlayer(p)", "TypeError", "Chat.sendToPlayer: expected (player, text, opts?)"},
            {"Chat.sendToPlayer(p, 5)", "Chat.sendToPlayer(p, 5)", "TypeError", "Chat.sendToPlayer: expected (player, text, opts?)"},
            {"Chat.sendToPlayer('text', p)", "Chat.sendToPlayer('text', p)", "TypeError", "Chat.sendToPlayer: expected (player, text, opts?)"},
            // What is not a handle of an entity is the mistake of the caller.
            {"Chat.sendToPlayer(5, 'text')", "Chat.sendToPlayer(5, 'text')", "TypeError", "Chat.sendToPlayer: expected a Player"},
            {"Chat.sendToPlayer(nil, 'text')", "Chat.sendToPlayer(null, 'text')", "TypeError", "Chat.sendToPlayer: expected a Player"},
            {"Chat.sendToPlayer({ id = p.id }, 'text')", "Chat.sendToPlayer({ id: p.id }, 'text')", "TypeError", "Chat.sendToPlayer: expected a Player"},
            {"Chat.sendToPlayer(Color.new(1, 1, 1, 1), 'text')", "Chat.sendToPlayer(new Color(1, 1, 1, 1), 'text')", "TypeError", "Chat.sendToPlayer: expected a Player"},
            // A handle whose entity is gone is the state of the world.
            {"Chat.sendToPlayer(stale, 'text')", "Chat.sendToPlayer(stale, 'text')", "Error", "Chat.sendToPlayer: player has no owning connection"},
            // No test of this stage publishes a network peer.
            {"Chat.sendToAll('text')", "Chat.sendToAll('text')", "Error", "Chat.sendToAll: network peer unavailable"},
            {"Chat.sendToAll('text', { author = 'a', color = 255 })", "Chat.sendToAll('text', { author: 'a', color: 255 })", "Error", "Chat.sendToAll: network peer unavailable"},
            {"Chat.sendToPlayer(p, 'text')", "Chat.sendToPlayer(p, 'text')", "Error", "Chat.sendToPlayer: network peer unavailable"},
            {"Chat.sendToPlayer(e, 'text')", "Chat.sendToPlayer(e, 'text')", "Error", "Chat.sendToPlayer: network peer unavailable"},
        };
        for (const Case &entry : cases) {
            EQUALS(stage.lua.Raised(entry.lua), entry.message);
            EQUALS(stage.js.Thrown(entry.js), std::string(entry.kind) + ": " + entry.message);
        }
    });

    IT("broadcasts a line to every client, with the author and the color of its options", {
        LuaBuiltinsChatTest::Wire wire;
        EQUALS(wire.ready, true);

        // One list of calls for each language: the options are spelled as the language spells an object.
        const char *lua       = R"(
            Chat.sendToAll('plain')
            Chat.sendToAll('', nil)
            Chat.sendToAll('named', { author = 'Server' })
            Chat.sendToAll('packed', { author = 'Server', color = 0xFF8000FF })
            Chat.sendToAll('color', { color = Color.new(1, 0.5, 0, 1) })
            Chat.sendToAll('extra', { color = 0x11223344, unknown = true }, 'ignored')
        )";
        const char *js        = R"(
            Chat.sendToAll('plain');
            Chat.sendToAll('', undefined);
            Chat.sendToAll('named', { author: 'Server' });
            Chat.sendToAll('packed', { author: 'Server', color: 0xFF8000FF });
            Chat.sendToAll('color', { color: new Color(1, 0.5, 0, 1) });
            Chat.sendToAll('extra', { color: 0x11223344, unknown: true }, 'ignored');
            0
        )";
        const std::string one = "<plain||00000000> %<||00000000> %<named|Server|00000000> %<packed|Server|ff8000ff> %<color||ff8000ff> %<extra||11223344>";
        std::string expected;
        for (const char *client : {"0", "1"}) {
            std::string lines = one;
            for (size_t at = lines.find('%'); at != std::string::npos; at = lines.find('%')) {
                lines.replace(at, 1, client);
            }
            expected += (expected.empty() ? "" : " ") + (client + lines);
        }

        EQUALS(wire.lua.Run(lua), "");
        EQUALS(wire.Delivered(12), expected);
        EQUALS(wire.js.Eval(js), "0");
        EQUALS(wire.Delivered(12), expected);
    });

    IT("reads the options as JavaScript always did: what is not an author or a color is left out", {
        LuaBuiltinsChatTest::Wire wire;
        EQUALS(wire.ready, true);

        // Options that are not an object, an author that is not a string and a color that is neither a number
        // nor a Color leave the line as it is without them. A number is taken as 32 bits, as ToUint32 takes it.
        const char *lua            = R"(
            Chat.sendToPlayer(p0, 'a', 'not options')
            Chat.sendToPlayer(p0, 'b', 5)
            Chat.sendToPlayer(p0, 'c', { author = 7, color = 'red' })
            Chat.sendToPlayer(p0, 'd', { color = Vector3.new(1, 2, 3) })
            Chat.sendToPlayer(p0, 'e', { color = -1 })
            Chat.sendToPlayer(p0, 'f', { color = 4294967301 })
            Chat.sendToPlayer(p0, 'g', { color = 1.9 })
            Chat.sendToPlayer(p0, 'h', { color = -1.9 })
            Chat.sendToPlayer(p0, 'i', { color = 0 / 0 })
            Chat.sendToPlayer(p0, 'j', { color = math.huge })
            Chat.sendToPlayer(p0, 'k', { color = 4286578943.0 })
            Chat.sendToPlayer(p0, 'l', {})
        )";
        const char *js             = R"(
            Chat.sendToPlayer(p0, 'a', 'not options');
            Chat.sendToPlayer(p0, 'b', 5);
            Chat.sendToPlayer(p0, 'c', { author: 7, color: 'red' });
            Chat.sendToPlayer(p0, 'd', { color: new Vector3(1, 2, 3) });
            Chat.sendToPlayer(p0, 'e', { color: -1 });
            Chat.sendToPlayer(p0, 'f', { color: 4294967301 });
            Chat.sendToPlayer(p0, 'g', { color: 1.9 });
            Chat.sendToPlayer(p0, 'h', { color: -1.9 });
            Chat.sendToPlayer(p0, 'i', { color: NaN });
            Chat.sendToPlayer(p0, 'j', { color: Infinity });
            Chat.sendToPlayer(p0, 'k', { color: 4286578943.0 });
            Chat.sendToPlayer(p0, 'l', {});
            0
        )";
        const std::string expected = "0<a||00000000> 0<b||00000000> 0<c||00000000> 0<d||00000000> 0<e||ffffffff> 0<f||00000005> 0<g||00000001> 0<h||ffffffff> 0<i||00000000> 0<j||00000000> 0<k||ff8000ff> 0<l||00000000>";

        EQUALS(wire.lua.Run(lua), "");
        EQUALS(wire.Delivered(12), expected);
        EQUALS(wire.js.Eval(js), "0");
        EQUALS(wire.Delivered(12), expected);
    });

    IT("sends a line to the connection of one player and to no other", {
        LuaBuiltinsChatTest::Wire wire;
        EQUALS(wire.ready, true);

        // A Player and a plain Entity both name the connection that owns the entity.
        const char *lua            = R"(
            Chat.sendToPlayer(p0, 'to the first')
            Chat.sendToPlayer(p1, 'to the second', { author = 'Admin', color = Color.new(1, 0, 0, 1) })
            Chat.sendToPlayer(e0, 'by entity', { author = 'Admin', color = 0x00FF00FF })
        )";
        const char *js             = R"(
            Chat.sendToPlayer(p0, 'to the first');
            Chat.sendToPlayer(p1, 'to the second', { author: 'Admin', color: new Color(1, 0, 0, 1) });
            Chat.sendToPlayer(e0, 'by entity', { author: 'Admin', color: 0x00FF00FF });
            0
        )";
        const std::string expected = "0<to the first||00000000> 0<by entity|Admin|00ff00ff> 1<to the second|Admin|ff0000ff>";

        EQUALS(wire.lua.Run(lua), "");
        EQUALS(wire.Delivered(3), expected);
        EQUALS(wire.js.Eval(js), "0");
        EQUALS(wire.Delivered(3), expected);

        // Nothing comes back from a send, in either language.
        EQUALS(wire.lua.Eval("select('#', Chat.sendToAll('once')) + select('#', Chat.sendToPlayer(p0, 'twice'))"), "0");
        EQUALS(wire.js.Eval("String(Chat.sendToAll('once')) + String(Chat.sendToPlayer(p0, 'twice'))"), "undefinedundefined");
        EQUALS(wire.Delivered(6), "0<once||00000000> 0<twice||00000000> 0<once||00000000> 0<twice||00000000> 1<once||00000000> 1<once||00000000>");

        // An entity no connection owns is a player nobody hears: the call is valid and reaches no one.
        Framework::Networking::Replication::NetworkEntity *prop = wire.world.Manager()->CreateEntity(wire.world.targetType);
        const std::string id = std::to_string(prop->GetNetworkID());
        EQUALS(wire.lua.Run("Chat.sendToPlayer(Entity.new(" + id + "), 'to nobody')"), "");
        EQUALS(wire.js.Eval("Chat.sendToPlayer(new Entity(" + id + "), 'to nobody'); 0"), "0");
        EQUALS(wire.Delivered(0), "");
    });
})
