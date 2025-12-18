#pragma once

#include "core_modules.h"

#include "../resource/environment_sandbox.h"
#include "../resource/resource_manager.h"

namespace Framework::Scripting::Builtins {

    /**
     * Message builtin for async inter-resource communication.
     */
    class Message final {
        /**
         * Send a fire-and-forget message to a resource.
         *
         * @param targetResource Name of the target resource
         * @param messageType Type of the message
         * @param payload Message payload (table or any Lua value)
         */
        static void Send(const std::string &targetResource, const std::string &messageType, sol::object payload) {
            auto *manager = Framework::CoreModules::GetResourceManager();
            if (manager) {
                manager->SendMessage(targetResource, messageType, payload);
            }
        }

        /**
         * Send a request message with callback for the response.
         *
         * @param targetResource Name of the target resource
         * @param messageType Type of the message
         * @param payload Message payload (table or any Lua value)
         * @param callback Callback function to invoke with the response
         */
        static void Request(const std::string &targetResource, const std::string &messageType, sol::object payload, sol::protected_function callback) {
            auto *manager = Framework::CoreModules::GetResourceManager();
            if (manager) {
                manager->SendRequest(targetResource, messageType, payload, callback);
            }
        }

        /**
         * Register a message handler for the current resource.
         *
         * @param messageType Type of message to handle
         * @param handler Handler function that receives (request, reply)
         *                The handler should call reply(response) to send a response
         */
        static void Handle(const std::string &messageType, sol::protected_function handler) {
            auto *manager = Framework::CoreModules::GetResourceManager();
            if (manager) {
                manager->RegisterMessageHandler(messageType, handler);
            }
        }

      public:
        static void Register(sol::state *luaEngine) {
            sol::usertype<Message> cls = luaEngine->new_usertype<Message>("Message");
            cls["send"]                = &Message::Send;
            cls["request"]             = &Message::Request;
            cls["handle"]              = &Message::Handle;
            EnvironmentSandbox::RegisterBuiltinName("Message");
        }
    };

} // namespace Framework::Scripting::Builtins
