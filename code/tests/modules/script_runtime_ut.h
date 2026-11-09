/*
 * MafiaHub OSS license
 * Copyright (c) 2021-2026, MafiaHub. All rights reserved.
 *
 * This file comes from MafiaHub, hosted at https://github.com/MafiaHub/Framework.
 * See LICENSE file in the source repository for information regarding licensing.
 */

#pragma once

// Uses TestManagerHelper from resource_manager_ut.h, included before this file.
// Declaring a binding once for every language, and running server resources in another language
// (docs/scripting_csharp.md).
#include "scripting/api/binding.h"
#include "scripting/api/native_value_types.h"
#include "scripting/builtins/builtins.h"
#include "scripting/clr/clr_runtime.h"
#include "scripting/clr/value_codec.h"
#include "scripting/engine_scope.h"
#include "scripting/node_engine.h"
#include "scripting/resource/package_manifest.h"
#include "scripting/resource/resource_manager.h"
#include "scripting/script_runtime.h"

#include <v8pp/class.hpp>

#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <functional>
#include <map>
#include <memory>
#include <sstream>
#include <string>
#include <thread>
#include <vector>

namespace ScriptRuntimeTest {
    using Framework::Scripting::TransferredValue;
    namespace Api = Framework::Scripting::Api;

    // A handle type of the tests' own: networking-free, so every path through a binding can be driven here.
    class Widget {
      public:
        explicit Widget(uint64_t id): _id(id) {
            if (id == 0) {
                throw std::runtime_error("Widget 0 is gone");
            }
        }

        uint64_t GetId() const {
            return _id;
        }
        int32_t Add(int32_t a, int32_t b) const {
            return a + b;
        }
        std::string Greet(const std::string &name) const {
            return "hello " + name;
        }
        Framework::Scripting::Builtins::Vector3 Scale(const Framework::Scripting::Builtins::Vector3 &v, float factor) const {
            return Framework::Scripting::Builtins::Vector3(v.vec() * factor);
        }
        void Reject(int32_t) const {
            Api::ThrowArgumentError("Widget.reject: never accepted");
        }
        void Refuse() const {
            Api::ThrowStateError("Widget.refuse: not now");
        }
        bool ServerOnly() const {
            return true;
        }
        uint32_t GetCount() const {
            return Count();
        }
        void SetCount(uint32_t count) {
            Count() = count;
        }
        static uint32_t &Count() {
            static uint32_t count = 0;
            return count;
        }

        static const Api::ClassBinding<Widget> &Binding() {
            using v8pp::metadata::docs;
            using v8pp::metadata::param;
            static const Api::ClassBinding<Widget> &binding = *[] {
                auto *b = new Api::ClassBinding<Widget>("Widget", "A test handle.");
                b->Constructor<uint64_t>(docs("void", {param("id", "number")}, "Wraps an id."))
                    .Method<&Widget::Add>("add", docs("number", {param("a", "number", false, "First."), param("b", "number", false, "Second.")}, "Adds."))
                    .Method<&Widget::Greet>("greet", docs("string", {param("name", "string", true)}, "Greets."))
                    .Method<&Widget::Scale>("scale", docs("Vector3", {param("v", "Vector3"), param("factor", "number")}, "Scales."))
                    .Method<&Widget::Reject>("reject", docs("void", {param("value", "number")}, "Rejects."))
                    .Method<&Widget::Refuse>("refuse", docs("void", {}, "Refuses."))
                    .Method<&Widget::ServerOnly>("serverOnly", docs("boolean", {}, "Server only."), Api::Side::Server)
                    .Property<&Widget::GetCount, &Widget::SetCount>("count", v8pp::metadata::property_docs("number", "A count."));
                return b;
            }();
            return binding;
        }

      private:
        uint64_t _id;
    };

    inline int32_t Call(const char *key, uint64_t self, std::vector<Api::NativeValue> args, Api::NativeValue &result) {
        Widget::Binding();
        Api::NativeThunk thunk = Api::NativeCatalog::Get().Find(Api::NativeCatalog::IdOf(key));
        if (!thunk) {
            return -1;
        }
        return thunk(self, args.data(), static_cast<int32_t>(args.size()), &result);
    }

    inline Api::NativeValue Int(int32_t value) {
        Api::NativeValue v;
        Api::NativeTraits<int32_t>::To(value, v);
        return v;
    }

    inline std::string Text(const Api::NativeValue &value) {
        return value.type == Api::NativeType::String ? std::string(value.text, value.length) : std::string("<not a string>");
    }

    inline TransferredValue Number(double value) {
        TransferredValue v;
        v.kind   = TransferredValue::Kind::Number;
        v.number = value;
        return v;
    }

    inline TransferredValue String(const std::string &text) {
        TransferredValue v;
        v.kind = TransferredValue::Kind::String;
        v.text = text;
        return v;
    }

    inline std::string ReadFile(const std::filesystem::path &path) {
        std::ifstream in(path, std::ios::binary);
        std::stringstream buffer;
        buffer << in.rdbuf();
        return buffer.str();
    }

    // A runtime that records what the resource manager asks of it.
    class FakeRuntime final: public Framework::Scripting::ScriptRuntime {
      public:
        struct Dispatched {
            Framework::Scripting::EventScope scope;
            std::string name;
            std::string target;
            std::vector<TransferredValue> args;
        };

        const std::string &GetName() const override {
            static const std::string name = "fake";
            return name;
        }

        Framework::Utils::Result<void, std::string> StartResource(const Framework::Scripting::Resource &resource, const std::vector<std::string> &scripts) override {
            if (!failStart.empty()) {
                return Framework::Utils::Result<void, std::string>::Err(failStart);
            }
            started.push_back(resource.GetName());
            lastScripts = scripts;
            loaded.insert(resource.GetName());
            return Framework::Utils::Result<void, std::string>::Ok();
        }
        void StopResource(const std::string &resourceName) override {
            if (loaded.erase(resourceName) > 0) {
                stopped.push_back(resourceName);
            }
        }
        bool IsResourceLoaded(const std::string &resourceName) const override {
            return loaded.contains(resourceName);
        }
        void Tick() override {
            ++ticks;
        }
        void Shutdown() override {
            ++shutdowns;
        }
        bool WantsEvent(Framework::Scripting::EventScope, std::string_view eventName) const override {
            return wanted.contains(std::string(eventName));
        }
        bool DispatchEvent(Framework::Scripting::EventScope scope, const std::string &eventName, const std::string &targetResource, const std::vector<TransferredValue> &args) override {
            dispatched.push_back({scope, eventName, targetResource, args});
            return eventName != veto;
        }
        bool HasExport(const std::string &, const std::string &exportName) const override {
            return exports.contains(exportName);
        }
        Framework::Utils::Result<TransferredValue, std::string> CallExport(const std::string &, const std::string &exportName, const std::vector<TransferredValue> &args) override {
            return exports.at(exportName)(args);
        }

        std::vector<std::string> started;
        std::vector<std::string> stopped;
        std::set<std::string> loaded;
        std::vector<std::string> lastScripts;
        std::string failStart;
        std::set<std::string> wanted;
        std::string veto;
        std::vector<Dispatched> dispatched;
        std::map<std::string, std::function<Framework::Utils::Result<TransferredValue, std::string>(const std::vector<TransferredValue> &)>> exports;
        int ticks     = 0;
        int shutdowns = 0;
    };

    // `exports` is the manifest's list, quoted: only declared exports are kept.
    inline void WriteJs(const std::string &name, const std::string &script, const std::string &exports = "") {
        TestManagerHelper::CreateTestResource(name, "{\"name\":\"" + name + "\",\"version\":\"1.0.0\",\"mafiahub\":{\"server\":\"main.js\",\"exports\":[" + exports + "]}}");
        TestManagerHelper::CreateTestScript(name, "main.js", script);
    }

    inline void WriteForeign(const std::string &name, const std::string &runtime, const std::string &script) {
        TestManagerHelper::CreateTestResource(name, "{\"name\":\"" + name + "\",\"version\":\"1.0.0\",\"mafiahub\":{\"serverRuntime\":\"" + runtime + "\",\"serverScripts\":[\"" + script + "\"]}}");
    }

    // Ticks the engine and the runtimes the way the server does until `key` is recorded or time runs out.
    inline bool TickUntil(Framework::Scripting::NodeEngine &engine, Framework::Scripting::ResourceManager &manager, const std::function<bool()> &done, int timeoutMs = 3000) {
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(timeoutMs);
        while (std::chrono::steady_clock::now() < deadline) {
            engine.Tick();
            manager.TickRuntimes();
            if (done()) {
                return true;
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
        return false;
    }

    inline bool Recorded(const std::string &key, int32_t value) {
        return TestManagerHelper::RecordedValue(key) == value;
    }
} // namespace ScriptRuntimeTest

MODULE(script_bindings, {
    using namespace ScriptRuntimeTest;
    using Api::NativeStatus;
    using Api::NativeValue;

    IT("generates the committed C# API from the bindings", {
        std::ostringstream generated;
        Api::NativeCatalog::WriteCSharp(generated, Framework::Scripting::Builtins::GetNativeBindingClasses());
        const std::filesystem::path committed = FW_CSHARP_GENERATED_FILE;
        if (std::getenv("FW_UPDATE_GENERATED") != nullptr) {
            std::ofstream(committed, std::ios::binary) << generated.str();
        }
        // Run with FW_UPDATE_GENERATED=1 to regenerate after changing a binding.
        EQUALS(ReadFile(committed) == generated.str(), true);
    });

    IT("calls a member through its thunk", {
        NativeValue result;
        EQUALS(Call("Widget.add", 1, {Int(40), Int(2)}, result), static_cast<int32_t>(NativeStatus::Ok));
        EQUALS(result.type, Api::NativeType::Int32);
        EQUALS(result.i64, 42);

        NativeValue name;
        Api::NativeTraits<std::string>::To(std::string("world"), name);
        const std::string text = Text(name); // the thunk reuses the result buffer, so copy the argument first
        Api::NativeValue argument;
        argument.type   = Api::NativeType::String;
        argument.text   = text.data();
        argument.length = static_cast<uint32_t>(text.size());
        EQUALS(Call("Widget.greet", 1, {argument}, result), static_cast<int32_t>(NativeStatus::Ok));
        STREQUALS(Text(result).c_str(), "hello world");

        NativeValue vector;
        Api::NativeTraits<Framework::Scripting::Builtins::Vector3>::To(Framework::Scripting::Builtins::Vector3(1, 2, 3), vector);
        NativeValue factor;
        Api::NativeTraits<float>::To(2.0f, factor);
        EQUALS(Call("Widget.scale", 1, {vector, factor}, result), static_cast<int32_t>(NativeStatus::Ok));
        EQUALS(result.type, Api::NativeType::Vector3);
        EQUALS(result.f32[2], 6.0f);
    });

    IT("reads and writes a property through its getter and setter thunks", {
        NativeValue count;
        Api::NativeTraits<uint32_t>::To(7u, count);
        NativeValue result;
        EQUALS(Call("Widget.count=", 1, {count}, result), static_cast<int32_t>(NativeStatus::Ok));
        EQUALS(Call("Widget.count", 1, {}, result), static_cast<int32_t>(NativeStatus::Ok));
        EQUALS(result.u64, 7u);
    });

    IT("answers a stale receiver without calling the member", {
        NativeValue result;
        EQUALS(Call("Widget.add", 0, {Int(1), Int(2)}, result), static_cast<int32_t>(NativeStatus::StaleReceiver));
        EQUALS(result.type, Api::NativeType::Void);
    });

    IT("reports a wrong argument as an argument error", {
        NativeValue result;
        NativeValue flag;
        Api::NativeTraits<bool>::To(true, flag);
        EQUALS(Call("Widget.add", 1, {Int(1), flag}, result), static_cast<int32_t>(NativeStatus::ArgumentError));
        EQUALS(Text(result).find("argument 1 must be int") != std::string::npos, true);
        EQUALS(Call("Widget.add", 1, {Int(1)}, result), static_cast<int32_t>(NativeStatus::ArgumentError));
        EQUALS(Text(result).find("expects 2") != std::string::npos, true);
    });

    IT("reports the kind a binding throws", {
        NativeValue result;
        EQUALS(Call("Widget.reject", 1, {Int(1)}, result), static_cast<int32_t>(NativeStatus::ArgumentError));
        STREQUALS(Text(result).c_str(), "Widget.reject: never accepted");
        EQUALS(Call("Widget.refuse", 1, {}, result), static_cast<int32_t>(NativeStatus::StateError));
        STREQUALS(Text(result).c_str(), "Widget.refuse: not now");
    });

    IT("refuses two members that hash to the same id", {
        bool threw = false;
        try {
            Api::NativeCatalog::Get().AddThunk(Api::NativeCatalog::IdOf("Widget.add"), "Widget.somethingElse", nullptr);
        }
        catch (const std::logic_error &) {
            threw = true;
        }
        EQUALS(threw, true);
    });

    IT("describes a class for the generated API", {
        const auto &native = Widget::Binding().GetNative();
        STREQUALS(native.name.c_str(), "Widget");
        EQUALS(native.members.size(), 7u);
        STREQUALS(native.members[0].name.c_str(), "add");
        STREQUALS(native.members[0].csharpType.c_str(), "int");
        STREQUALS(native.members[0].parameters[1].name.c_str(), "b");
        STREQUALS(native.members[0].parameters[1].description.c_str(), "Second.");
        EQUALS(native.members[5].side, Api::Side::Server);
        EQUALS(native.members[6].kind, Api::NativeMember::Kind::Property);
        EQUALS(native.members[6].setterId != 0, true);

        std::ostringstream out;
        Api::NativeCatalog::WriteCSharp(out, {&native});
        const std::string code = out.str();
        EQUALS(code.find("public partial class Widget") != std::string::npos, true);
        EQUALS(code.find("public int Add(int a, int b)") != std::string::npos, true);
        EQUALS(code.find("using var s0 = NativeString.Create(name, out a[0]);") != std::string::npos, true);
        EQUALS(code.find("public uint Count") != std::string::npos, true);
    });

    // One engine for the JavaScript side of the same declaration.
    Framework::Scripting::NodeEngine engine({});
    const bool engineReady = engine.Init() == Framework::Scripting::ScriptingError::SCRIPTING_NONE;
    IT("brings up an isolate for the JavaScript cases", { EQUALS(engineReady, true); });

    if (engineReady) {
        std::unique_ptr<v8pp::class_<Widget>> widgetClass;
        {
            Framework::Scripting::EngineScope scope(&engine);
            v8::Isolate *isolate           = scope.Isolate();
            v8::Local<v8::Context> context = scope.Context();
            Framework::Scripting::Builtins::RegisterValueTypes(isolate, context->Global());
            widgetClass = Widget::Binding().Build(isolate);
            context->Global()->Set(context, v8::String::NewFromUtf8Literal(isolate, "Widget"), widgetClass->js_function_template()->GetFunction(context).ToLocalChecked()).Check();
        }
        const auto eval = [&engine](const char *source) {
            return TestManagerHelper::EvalInt(engine, source);
        };

        IT("binds the same declaration for JavaScript", {
            EQUALS(eval("new Widget(1).add(40, 2)"), 42);
            EQUALS(eval("new Widget(1).greet('you') === 'hello you' ? 1 : 0"), 1);
            EQUALS(eval("new Widget(1).scale(new Vector3(1, 2, 3), 2).z"), 6);
            EQUALS(eval("const w = new Widget(1); w.count = 9; w.count"), 9);
            EQUALS(eval("new Widget(1).serverOnly() ? 1 : 0"), 1);
        });

        IT("lets a trailing string argument be left out", { EQUALS(eval("new Widget(1).greet() === 'hello ' ? 1 : 0"), 1); });

        IT("throws a TypeError for a bad argument and an Error for a refused call", {
            EQUALS(eval("try { new Widget(1).add('x', 1); 0 } catch (e) { e instanceof TypeError && e.message.includes(\"argument 'a' must be number\") ? 1 : 0 }"), 1);
            EQUALS(eval("try { new Widget(1).add(1); 0 } catch (e) { e instanceof TypeError && e.message.includes(\"missing argument 'b'\") ? 1 : 0 }"), 1);
            EQUALS(eval("try { new Widget(1).reject(1); 0 } catch (e) { e instanceof TypeError ? 1 : 0 }"), 1);
            EQUALS(eval("try { new Widget(1).refuse(); 0 } catch (e) { !(e instanceof TypeError) && e.message === 'Widget.refuse: not now' ? 1 : 0 }"), 1);
        });

        IT("ignores a call on a receiver that is not a Widget", { EQUALS(eval("Object.getPrototypeOf(new Widget(1)).add.call({}, 1, 2) === undefined ? 1 : 0"), 1); });

        IT("records the declaration for the TypeScript contract", {
            Framework::Scripting::EngineScope scope(&engine);
            const auto &symbols = Framework::Scripting::GetScriptingCatalog(scope.Isolate()).symbols();
            const auto it       = std::find_if(symbols.begin(), symbols.end(), [](const auto &symbol) {
                return symbol.name == "Widget";
            });
            EQUALS(it != symbols.end(), true);
            EQUALS(it->functions.size(), 6u);
            STREQUALS(it->functions[0].name.c_str(), "add");
            STREQUALS(it->functions[0].call_signature.parameters[0].name.c_str(), "a");
            EQUALS(it->properties.size(), 1u);
            EQUALS(it->properties[0].readonly, false);
        });

        {
            Framework::Scripting::EngineScope scope(&engine);
            widgetClass.reset();
            Framework::Scripting::Builtins::UnregisterAll(scope.Isolate());
        }
        engine.Shutdown();
    }

    // v8pp binds a type once per isolate, so the client's view of the class gets an engine of its own.
    Framework::Scripting::NodeEngine clientEngine({});
    if (clientEngine.Init() == Framework::Scripting::ScriptingError::SCRIPTING_NONE) {
        IT("leaves server members off a client's class", {
            Framework::Scripting::EngineScope scope(&clientEngine);
            v8::Isolate *isolate           = scope.Isolate();
            v8::Local<v8::Context> context = scope.Context();
            Framework::Scripting::SetScriptingEnvironment(isolate, true);
            auto cls = Widget::Binding().Build(isolate);
            Framework::Scripting::SetScriptingEnvironment(isolate, false);
            v8::Local<v8::Object> instance = cls->class_function_template()->GetFunction(context).ToLocalChecked()->NewInstance(context).ToLocalChecked();
            const bool hasShared           = instance->Has(context, v8::String::NewFromUtf8Literal(isolate, "add")).FromMaybe(false);
            const bool hasServer           = instance->Has(context, v8::String::NewFromUtf8Literal(isolate, "serverOnly")).FromMaybe(true);
            cls.reset();
            Framework::Scripting::Builtins::UnregisterAll(isolate);
            EQUALS(hasShared, true);
            EQUALS(hasServer, false);
        });
        clientEngine.Shutdown();
    }
});

MODULE(clr_value_codec, {
    using namespace ScriptRuntimeTest;
    using Framework::Scripting::Clr::ValueCodec;
    using Kind = TransferredValue::Kind;

    const auto roundTrip = [](const TransferredValue &value) {
        std::vector<uint8_t> bytes;
        auto encoded = ValueCodec::Encode(value, bytes);
        if (!encoded) {
            return Framework::Utils::Result<TransferredValue, std::string>::Err(encoded.GetError());
        }
        return ValueCodec::Decode(bytes.data(), bytes.size());
    };

    IT("carries plain values both ways", {
        TransferredValue object;
        object.kind = Kind::Object;
        TransferredValue list;
        list.kind = Kind::Array;
        list.items.push_back(Number(1.5));
        TransferredValue flag;
        flag.kind    = Kind::Boolean;
        flag.boolean = true;
        list.items.push_back(flag);
        list.items.push_back(TransferredValue {});
        object.properties.emplace_back("list", list);
        object.properties.emplace_back("name", String("caf\xC3\xA9"));

        auto decoded = roundTrip(object);
        EQUALS((bool)decoded, true);
        const TransferredValue &value = decoded.GetValue();
        EQUALS(value.kind, Kind::Object);
        STREQUALS(value.properties[1].second.text.c_str(), "caf\xC3\xA9");
        EQUALS(value.properties[0].second.items[0].number, 1.5);
        EQUALS(value.properties[0].second.items[1].boolean, true);
        EQUALS(value.properties[0].second.items[2].kind, Kind::Null);
    });

    IT("carries the value types and handles as themselves", {
        TransferredValue quaternion;
        quaternion.kind = Kind::HostObject;
        quaternion.text = "Quaternion";
        const glm::quat q(0.5f, 0.1f, 0.2f, 0.3f);
        quaternion.bytes.resize(sizeof(q));
        std::memcpy(quaternion.bytes.data(), &q, sizeof(q));
        auto decoded = roundTrip(quaternion);
        EQUALS((bool)decoded, true);
        EQUALS(decoded.GetValue().bytes == quaternion.bytes, true);
        STREQUALS(decoded.GetValue().text.c_str(), "Quaternion");

        TransferredValue player;
        player.kind       = Kind::HostObject;
        player.text       = "Player";
        const uint64_t id = 0x1122334455667788ull;
        player.bytes.resize(sizeof(id));
        std::memcpy(player.bytes.data(), &id, sizeof(id));
        decoded = roundTrip(player);
        EQUALS((bool)decoded, true);
        STREQUALS(decoded.GetValue().text.c_str(), "Player");
        EQUALS(decoded.GetValue().bytes == player.bytes, true);
    });

    IT("converts what C# has no counterpart for", {
        TransferredValue map;
        map.kind = Kind::Map;
        map.items.push_back(String("k"));
        map.items.push_back(Number(3));
        auto decoded = roundTrip(map);
        EQUALS((bool)decoded, true);
        EQUALS(decoded.GetValue().kind, Kind::Object);
        STREQUALS(decoded.GetValue().properties[0].first.c_str(), "k");

        TransferredValue date;
        date.kind   = Kind::Date;
        date.number = 1234;
        decoded     = roundTrip(date);
        EQUALS(decoded.GetValue().kind, Kind::Number);
    });

    IT("refuses what cannot cross, naming where", {
        TransferredValue function;
        function.kind = Kind::Function;
        TransferredValue object;
        object.kind = Kind::Object;
        object.properties.emplace_back("callback", function);
        auto encoded = ValueCodec::EncodeList({Number(1), object});
        EQUALS((bool)encoded, false);
        EQUALS(encoded.GetError().find("argument 2.callback") != std::string::npos, true);
        EQUALS(encoded.GetError().find("function") != std::string::npos, true);

        TransferredValue map;
        map.kind = Kind::Map;
        map.items.push_back(Number(1));
        map.items.push_back(Number(2));
        std::vector<uint8_t> bytes;
        EQUALS((bool)ValueCodec::Encode(map, bytes), false);
    });

    IT("rejects malformed data", {
        const uint8_t truncated[] = {4, 10, 0, 0, 0, 'a'};
        EQUALS((bool)ValueCodec::Decode(truncated, sizeof(truncated)), false);
        const uint8_t unknown[] = {99};
        EQUALS((bool)ValueCodec::Decode(unknown, sizeof(unknown)), false);
        const uint8_t trailing[] = {0, 0};
        EQUALS((bool)ValueCodec::Decode(trailing, sizeof(trailing)), false);
        const uint8_t hugeCount[] = {5, 0xFF, 0xFF, 0xFF, 0x7F};
        EQUALS((bool)ValueCodec::Decode(hugeCount, sizeof(hugeCount)), false);

        std::vector<uint8_t> deep;
        for (int i = 0; i < 100; ++i) {
            deep.insert(deep.end(), {5, 1, 0, 0, 0});
        }
        deep.push_back(0);
        EQUALS((bool)ValueCodec::Decode(deep.data(), deep.size()), false);
    });
});

MODULE(script_runtime, {
    using namespace ScriptRuntimeTest;
    using namespace Framework::Scripting;

    IT("reads the server runtime from the manifest", {
        PackageManifest manifest;
        EQUALS(manifest.ParseJson(nlohmann::json::parse(R"({"name":"a","mafiahub":{"server":"main.js"}})")), true);
        STREQUALS(manifest.GetMafiaHubConfig().serverRuntime.c_str(), "js");
        EQUALS(manifest.ParseJson(nlohmann::json::parse(R"({"name":"a","mafiahub":{"serverRuntime":"csharp","serverScripts":["A.dll"],"clientScripts":["c.js"]}})")), true);
        STREQUALS(manifest.GetMafiaHubConfig().serverRuntime.c_str(), "csharp");
        EQUALS(manifest.ParseJson(nlohmann::json::parse(R"({"name":"a","mafiahub":{"serverRuntime":3}})")), false);
        EQUALS(manifest.ParseJson(nlohmann::json::parse(R"({"name":"a","mafiahub":{"serverRuntime":"csharp","sharedScripts":["s.js"]}})")), false);
        EQUALS(manifest.GetError().find("sharedScripts") != std::string::npos, true);
    });

    // A JavaScript engine, a manager with the fake runtime, and the resources a case writes first.
    struct Setup {
        NodeEngine engine;
        std::unique_ptr<ResourceManager> manager;
        std::shared_ptr<FakeRuntime> runtime = std::make_shared<FakeRuntime>();

        bool Start() {
            if (engine.Init() != ScriptingError::SCRIPTING_NONE) {
                return false;
            }
            ResourceManagerConfig config;
            config.resourcesPath = TestManagerHelper::GetTestResourcePath();
            manager              = std::make_unique<ResourceManager>(&engine, config);
            manager->AddRuntime(runtime);
            TestManagerHelper::RegisterEvents(engine, *manager);
            return manager->DiscoverResources() > 0;
        }

        ~Setup() {
            manager.reset();
            engine.Shutdown();
            TestManagerHelper::Cleanup();
        }
    };

    IT("starts and stops a resource through the runtime its manifest names", {
        TestManagerHelper::Cleanup();
        WriteForeign("foreign", "fake", "lib/A.dll");
        TestManagerHelper::CreateTestScript("foreign", "lib/A.dll", "not really an assembly");
        Setup setup;
        EQUALS(setup.Start(), true);
        EQUALS((bool)setup.manager->StartResource("foreign"), true);
        EQUALS(setup.runtime->started.size(), 1u);
        EQUALS(setup.runtime->lastScripts.size(), 1u);
        EQUALS(std::filesystem::path(setup.runtime->lastScripts[0]).is_absolute(), true);
        EQUALS(setup.runtime->lastScripts[0].ends_with("A.dll"), true);
        EQUALS(setup.manager->GetRuntimeOf("foreign") == setup.runtime.get(), true);
        setup.manager->TickRuntimes();
        EQUALS(setup.runtime->ticks, 1);
        EQUALS((bool)setup.manager->StopResource("foreign"), true);
        EQUALS(setup.runtime->stopped.size(), 1u);
        EQUALS(setup.manager->GetResourceState("foreign"), ResourceState::Stopped);
    });

    IT("fails a resource whose runtime the server does not have", {
        TestManagerHelper::Cleanup();
        WriteForeign("orphan", "cobol", "A.dll");
        TestManagerHelper::CreateTestScript("orphan", "A.dll", "");
        Setup setup;
        EQUALS(setup.Start(), true);
        auto result = setup.manager->StartResource("orphan");
        EQUALS((bool)result, false);
        EQUALS(result.GetError().find("'cobol'") != std::string::npos, true);
        EQUALS(setup.manager->GetResourceState("orphan"), ResourceState::Error);
    });

    IT("reports the runtime's reason when it refuses a start", {
        TestManagerHelper::Cleanup();
        WriteForeign("refused", "fake", "A.dll");
        TestManagerHelper::CreateTestScript("refused", "A.dll", "");
        Setup setup;
        setup.runtime->failStart = "no entry point";
        EQUALS(setup.Start(), true);
        auto result = setup.manager->StartResource("refused");
        EQUALS((bool)result, false);
        STREQUALS(result.GetError().c_str(), "no entry point");
    });

    IT("refuses a script outside the resource before the runtime sees it", {
        TestManagerHelper::Cleanup();
        WriteForeign("escape", "fake", "../escape-target.dll");
        Setup setup;
        EQUALS(setup.Start(), true);
        EQUALS((bool)setup.manager->StartResource("escape"), false);
        EQUALS(setup.runtime->started.empty(), true);
    });

    IT("delivers JavaScript events the runtime listens for, with their arguments", {
        TestManagerHelper::Cleanup();
        WriteJs("emitter", "Events.emit('wanted', 1, 'two', { list: [3] }); Events.emit('ignored', 4);");
        Setup setup;
        setup.runtime->wanted = {"wanted"};
        EQUALS(setup.Start(), true);
        EQUALS((bool)setup.manager->StartResource("emitter"), true);
        EQUALS(setup.runtime->dispatched.size(), 1u);
        const auto &event = setup.runtime->dispatched[0];
        STREQUALS(event.name.c_str(), "wanted");
        EQUALS(event.args.size(), 3u);
        EQUALS(event.args[0].number, 1.0);
        STREQUALS(event.args[1].text.c_str(), "two");
        STREQUALS(event.args[2].properties[0].first.c_str(), "list");
        EQUALS(event.args[2].properties[0].second.items[0].number, 3.0);
    });

    IT("lets the runtime veto a synchronous server event", {
        TestManagerHelper::Cleanup();
        WriteJs("idle", "");
        Setup setup;
        setup.runtime->wanted = {"playerConnecting", "other"};
        setup.runtime->veto   = "playerConnecting";
        EQUALS(setup.Start(), true);
        EngineScope scope(&setup.engine);
        EQUALS(setup.manager->GetEvents().EmitReservedSync(scope.Isolate(), scope.Context(), "playerConnecting", {}), false);
        EQUALS(setup.manager->GetEvents().EmitReservedSync(scope.Isolate(), scope.Context(), "other", {}), true);
        setup.manager->GetEvents().EmitClient(scope.Isolate(), scope.Context(), "other", {})->MarkAsHandled();
        EQUALS(setup.runtime->dispatched.back().scope, EventScope::Client);
    });

    IT("raises the runtime's events for JavaScript handlers", {
        TestManagerHelper::Cleanup();
        WriteJs("listener", "Events.on('fromRuntime', (n, s) => __record('fromRuntime', n + s.length));");
        WriteJs("bystander", "Events.on('fromRuntime', () => __record('bystander', 1));");
        Setup setup;
        EQUALS(setup.Start(), true);
        EQUALS((bool)setup.manager->StartResource("listener"), true);
        EQUALS((bool)setup.manager->StartResource("bystander"), true);
        EQUALS((bool)setup.manager->EmitFromRuntime("fromRuntime", "listener", {Number(40), String("ab")}), true);
        EQUALS(Recorded("fromRuntime", 42), true);
        EQUALS(Recorded("bystander", -1), true);
        EQUALS((bool)setup.manager->EmitFromRuntime("fromRuntime", "", {Number(1), String("")}), true);
        EQUALS(Recorded("bystander", 1), true);
        EQUALS((bool)setup.manager->EmitFromRuntime("", "", {}), false);
    });

    IT("calls JavaScript exports for the runtime", {
        TestManagerHelper::Cleanup();
        WriteJs("exporter", R"(
            Exports.register('mul', (a, b) => a * b);
            Exports.register('answer', 7);
            Exports.register('settled', async () => 5);
            Exports.register('never', () => new Promise(() => {}));
            Exports.register('boom', () => { throw new Error('exploded'); });
        )",
            R"("mul","answer","settled","never","boom")");
        Setup setup;
        EQUALS(setup.Start(), true);
        EQUALS((bool)setup.manager->StartResource("exporter"), true);

        auto product = setup.manager->CallExport("exporter", "mul", {Number(6), Number(7)});
        EQUALS((bool)product, true);
        EQUALS(product.GetValue().number, 42.0);
        EQUALS(setup.manager->CallExport("exporter", "answer", {}).GetValue().number, 7.0);
        EQUALS(setup.manager->CallExport("exporter", "settled", {}).GetValue().number, 5.0);

        auto never = setup.manager->CallExport("exporter", "never", {});
        EQUALS((bool)never, false);
        EQUALS(never.GetError().find("has not settled") != std::string::npos, true);
        auto boom = setup.manager->CallExport("exporter", "boom", {});
        EQUALS((bool)boom, false);
        EQUALS(boom.GetError().find("exploded") != std::string::npos, true);
        auto missing = setup.manager->CallExport("exporter", "nothing", {});
        EQUALS(missing.GetError().find("not found") != std::string::npos, true);
        EQUALS(setup.manager->CallExport("nobody", "mul", {}).GetError().find("not found") != std::string::npos, true);
    });

    IT("gives JavaScript a callable stand-in for the runtime's exports", {
        TestManagerHelper::Cleanup();
        WriteForeign("foreign", "fake", "A.dll");
        TestManagerHelper::CreateTestScript("foreign", "A.dll", "");
        WriteJs("caller", R"(
            const sum = Exports.get('foreign', 'sum');
            __record('sum', sum(20, 22));
            try { Exports.get('foreign', 'fails')(); } catch (e) { __record('failed', e.message.includes('declined') ? 1 : 0); }
            try { Exports.get('foreign', 'missing'); } catch (e) { __record('missing', e.message.includes('not found') ? 1 : 0); }
            try { sum(() => 1); } catch (e) { __record('function', e instanceof TypeError ? 1 : 0); }
        )");
        Setup setup;
        setup.runtime->exports["sum"] = [](const std::vector<TransferredValue> &args) {
            double total = 0;
            for (const auto &arg : args) {
                total += arg.number;
            }
            return Framework::Utils::Result<TransferredValue, std::string>::Ok(Number(total));
        };
        setup.runtime->exports["fails"] = [](const std::vector<TransferredValue> &) {
            return Framework::Utils::Result<TransferredValue, std::string>::Err("declined");
        };
        EQUALS(setup.Start(), true);
        EQUALS((bool)setup.manager->StartResource("foreign"), true);
        EQUALS((bool)setup.manager->StartResource("caller"), true);
        EQUALS(Recorded("sum", 42), true);
        EQUALS(Recorded("failed", 1), true);
        EQUALS(Recorded("missing", 1), true);
        EQUALS(Recorded("function", 1), true);
    });

    IT("shuts its runtimes down with the manager", {
        TestManagerHelper::Cleanup();
        WriteJs("idle", "");
        auto runtime = std::make_shared<FakeRuntime>();
        {
            Setup setup;
            setup.runtime = runtime;
            EQUALS(setup.Start(), true);
        }
        EQUALS(runtime->shutdowns, 1);
    });
});

#ifdef FW_TEST_CSHARP
MODULE(csharp_scripting, {
    using namespace ScriptRuntimeTest;
    using namespace Framework::Scripting;

    IT("finds an installed .NET", {
        auto located = Clr::ClrRuntime::LocateHostFxr("");
        EQUALS((bool)located, true);
        EQUALS((bool)Clr::ClrRuntime::LocateHostFxr("/nonexistent/dotnet"), false);
    });

    const auto writeResources = [] {
        TestManagerHelper::Cleanup();
        WriteJs("jsres", R"(
            Exports.register('mul', (a, b) => a * b);
            Exports.register('settled', async () => 5);
            Exports.register('unsettled', () => new Promise(() => {}));
            const keep = (event, key) => Events.on(event, (v) => __record(key, v));
            keep('cs:started', 'started');
            keep('cs:stopped', 'stopped');
            keep('cs:called', 'called');
            keep('cs:calledAsync', 'calledAsync');
            keep('cs:pendingRefused', 'pendingRefused');
            keep('cs:delayed', 'delayed');
            keep('cs:ticked', 'ticked');
            keep('cs:fromClient', 'fromClient');
            keep('cs:workerRefused', 'workerRefused');
            keep('cs:counted', 'counted');
            Events.on('cs:pong', (text, n) => __record('pong', text === 'hi!' ? n : -n));
            Events.on('cs:once', () => __record('onceCalls', __recorded('onceCalls') + 1));
            Events.on('cs:entity', (state) => __record('entity', state));
        )",
            R"("mul","settled","unsettled")");
        TestManagerHelper::CreateTestResource("csres", R"({"name":"csres","version":"1.0.0","mafiahub":{"serverRuntime":"csharp","serverScripts":["TestResource.dll"],"resourceDependencies":["jsres"]}})");
        std::filesystem::copy_file(std::filesystem::path(FW_TEST_CSHARP_RESOURCE_DIR) / "TestResource.dll", std::filesystem::path(TestManagerHelper::GetTestResourcePath()) / "csres" / "TestResource.dll", std::filesystem::copy_options::overwrite_existing);
    };

    // A server's scripting with the C# runtime, and both test resources running.
    struct Setup {
        NodeEngine engine;
        std::unique_ptr<ResourceManager> manager;
        std::shared_ptr<Clr::ClrRuntime> runtime;

        bool Start() {
            if (engine.Init() != ScriptingError::SCRIPTING_NONE) {
                return false;
            }
            ResourceManagerConfig config;
            config.resourcesPath = TestManagerHelper::GetTestResourcePath();
            manager              = std::make_unique<ResourceManager>(&engine, config);
            runtime              = std::make_shared<Clr::ClrRuntime>(manager.get(), Clr::ClrRuntimeConfig {FW_TEST_CSHARP_HOST_DIR, ""});
            manager->AddRuntime(runtime);
            TestManagerHelper::RegisterEvents(engine, *manager);
            TestManagerHelper::Recorded()["onceCalls"] = 0;
            return manager->DiscoverResources() >= 2 && manager->StartResource("csres");
        }

        int32_t Eval(const char *source) {
            return TestManagerHelper::EvalIntIn(engine, "jsres", source);
        }

        bool Until(const std::function<bool()> &done) {
            return TickUntil(engine, *manager, done);
        }

        ~Setup() {
            manager.reset();
            runtime.reset();
            engine.Shutdown();
            TestManagerHelper::Cleanup();
        }
    };

    IT("starts a C# resource after the JavaScript one it depends on", {
        writeResources();
        Setup setup;
        EQUALS(setup.Start(), true);
        EQUALS(setup.runtime->IsAttached(), true);
        EQUALS(setup.manager->IsResourceRunning("jsres"), true);
        EQUALS(Recorded("started", 5), true);
    });

    IT("hands JavaScript events to C# handlers and their answers back", {
        writeResources();
        Setup setup;
        EQUALS(setup.Start(), true);
        setup.Eval("Events.emit('js:ping', 'hi', 21); 0");
        EQUALS(Recorded("pong", 42), true);
    });

    IT("keeps the bus going after a C# handler throws", {
        writeResources();
        Setup setup;
        EQUALS(setup.Start(), true);
        setup.Eval("Events.emit('js:throw'); Events.emit('js:ping', 'hi', 1); 0");
        EQUALS(Recorded("pong", 2), true);
    });

    IT("calls C# exports from JavaScript", {
        writeResources();
        Setup setup;
        EQUALS(setup.Start(), true);
        EQUALS(setup.Eval("Exports.get('csres', 'add')(40, 2)"), 42);
        EQUALS(setup.Eval("Exports.get('csres', 'echo')({ a: [1, 2, 3] }).a[2]"), 3);
        EQUALS(setup.Eval("Exports.get('csres', 'describe')({ x: 1, y: 2 }, [10, 20, 30]) === '2:3:30' ? 1 : 0"), 1);
        EQUALS(setup.Eval("Exports.get('csres', 'ready')()"), 42);
        EQUALS(setup.Eval("try { Exports.get('csres', 'fail')(); 0 } catch (e) { e.message.includes('refused on purpose') ? 1 : 0 }"), 1);
        EQUALS(setup.Eval("try { Exports.get('csres', 'pending')(); 0 } catch (e) { e.message.includes('has not completed') ? 1 : 0 }"), 1);
        EQUALS(setup.Eval("try { Exports.get('csres', 'add')('x', 1); 0 } catch (e) { e.message.includes('cannot become a Double') ? 1 : 0 }"), 1);
    });

    IT("calls JavaScript exports from C#", {
        writeResources();
        Setup setup;
        EQUALS(setup.Start(), true);
        setup.Eval("Events.emit('js:callJs'); Events.emit('js:callJsAsync'); Events.emit('js:callJsPending'); 0");
        EQUALS(Recorded("called", 42), true);
        EQUALS(Recorded("calledAsync", 5), true);
        EQUALS(Recorded("pendingRefused", 1), true);
    });

    IT("resumes awaits on the scripting thread at a later tick", {
        writeResources();
        Setup setup;
        EQUALS(setup.Start(), true);
        setup.Eval("Events.emit('js:delay'); 0");
        EQUALS(Recorded("delayed", -1), true);
        EQUALS(setup.Until([] {
            return Recorded("delayed", 1);
        }),
            true);
        EQUALS(setup.Until([] {
            return Recorded("ticked", 3);
        }),
            true);
    });

    IT("refuses scripting calls from worker threads", {
        writeResources();
        Setup setup;
        EQUALS(setup.Start(), true);
        setup.Eval("Events.emit('js:emitFromWorker'); 0");
        EQUALS(setup.Until([] {
            return Recorded("workerRefused", 1);
        }),
            true);
    });

    IT("removes once and disposed handlers", {
        writeResources();
        Setup setup;
        EQUALS(setup.Start(), true);
        setup.Eval("Events.emit('js:once'); Events.emit('js:once'); 0");
        EQUALS(Recorded("onceCalls", 1), true);
        setup.Eval("Events.emit('js:counted'); Events.emit('js:unsubscribe'); Events.emit('js:counted'); 0");
        EQUALS(Recorded("counted", 2), true);
    });

    IT("lets a C# handler veto and keeps client events apart", {
        writeResources();
        Setup setup;
        EQUALS(setup.Start(), true);
        EngineScope scope(&setup.engine);
        EQUALS(setup.manager->GetEvents().EmitReservedSync(scope.Isolate(), scope.Context(), "js:veto", {}), false);
        EQUALS(setup.manager->GetEvents().EmitReservedSync(scope.Isolate(), scope.Context(), "js:fromClient", {v8::String::NewFromUtf8Literal(scope.Isolate(), "abc")}), true);
        EQUALS(Recorded("fromClient", -1), true);
        setup.manager->GetEvents().EmitClient(scope.Isolate(), scope.Context(), "js:fromClient", {v8::String::NewFromUtf8Literal(scope.Isolate(), "abcd")})->MarkAsHandled();
        EQUALS(Recorded("fromClient", 4), true);
    });

    IT("passes a missing handle as null", {
        writeResources();
        Setup setup;
        EQUALS(setup.Start(), true);
        setup.Eval("Events.emit('js:entity', null); 0");
        EQUALS(Recorded("entity", -1), true);
    });

    IT("stops, unloads and starts a C# resource again", {
        writeResources();
        Setup setup;
        EQUALS(setup.Start(), true);
        EQUALS((bool)setup.manager->StopResource("csres"), true);
        EQUALS(Recorded("stopped", 5), true);
        EQUALS(setup.runtime->IsResourceLoaded("csres"), false);
        EQUALS(setup.Eval("try { Exports.get('csres', 'add'); 0 } catch (e) { e.message.includes('not running') ? 1 : 0 }"), 1);
        // Its handlers went with it.
        TestManagerHelper::Recorded()["pong"] = 0;
        setup.Eval("Events.emit('js:ping', 'hi', 21); 0");
        EQUALS(Recorded("pong", 0), true);

        TestManagerHelper::Recorded()["started"] = 0;
        EQUALS((bool)setup.manager->StartResource("csres"), true);
        EQUALS(Recorded("started", 5), true);
        EQUALS(setup.Eval("Exports.get('csres', 'add')(1, 2)"), 3);
    });

    IT("reports why a C# resource could not start", {
        writeResources();
        TestManagerHelper::CreateTestResource("broken", R"({"name":"broken","version":"1.0.0","mafiahub":{"serverRuntime":"csharp","serverScripts":["notes.txt"]}})");
        TestManagerHelper::CreateTestScript("broken", "notes.txt", "hello");
        Setup setup;
        EQUALS(setup.Start(), true);
        auto result = setup.manager->StartResource("broken");
        EQUALS((bool)result, false);
        EQUALS(result.GetError().find("is not an assembly") != std::string::npos, true);
        EQUALS(setup.manager->IsResourceRunning("csres"), true);
    });
});
#endif
