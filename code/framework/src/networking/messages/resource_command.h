/*
 * MafiaHub OSS license
 * Copyright (c) 2021-2023, MafiaHub. All rights reserved.
 *
 * This file comes from MafiaHub, hosted at https://github.com/MafiaHub/Framework.
 * See LICENSE file in the source repository for information regarding licensing.
 */

#pragma once

#include "messages.h"

#include <BitStream.h>
#include <string>

namespace Framework::Networking::Messages {

    /**
     * Command types for resource control.
     */
    enum class ResourceCommandType : uint8_t {
        Start   = 0, // Start a resource
        Stop    = 1, // Stop a resource
        Restart = 2, // Restart a resource (stop + start)
        Reload  = 3  // Hot-reload a resource
    };

    /**
     * Message sent from server to client to control resources mid-session.
     * Allows server to dynamically start/stop resources on connected clients.
     */
    class ResourceCommandMessage final: public IMessage {
      private:
        ResourceCommandType _command = ResourceCommandType::Start;
        std::string _resourceName;
        std::string _version;   // Optional: version for Start command
        uint32_t _hash = 0;     // Optional: content hash for cache validation

      public:
        uint8_t GetMessageID() const override {
            return GAME_RESOURCE_COMMAND;
        }

        void FromParameters(ResourceCommandType command, const std::string &resourceName, const std::string &version = "", uint32_t hash = 0) {
            _command      = command;
            _resourceName = resourceName;
            _version      = version;
            _hash         = hash;
        }

        void Serialize(SLNet::BitStream *bs, bool write) override {
            if (write) {
                // Write command type
                uint8_t cmdType = static_cast<uint8_t>(_command);
                bs->Write(cmdType);

                // Write resource name
                uint16_t nameLen = static_cast<uint16_t>(_resourceName.length());
                bs->Write(nameLen);
                if (nameLen > 0) {
                    bs->Write(_resourceName.c_str(), nameLen);
                }

                // Write version (for Start command)
                uint16_t versionLen = static_cast<uint16_t>(_version.length());
                bs->Write(versionLen);
                if (versionLen > 0) {
                    bs->Write(_version.c_str(), versionLen);
                }

                // Write hash
                bs->Write(_hash);
            }
            else {
                // Read command type
                uint8_t cmdType = 0;
                bs->Read(cmdType);
                _command = static_cast<ResourceCommandType>(cmdType);

                // Read resource name
                uint16_t nameLen = 0;
                bs->Read(nameLen);
                if (nameLen > 0 && nameLen < 256) {
                    _resourceName.resize(nameLen);
                    bs->Read(&_resourceName[0], nameLen);
                }

                // Read version
                uint16_t versionLen = 0;
                bs->Read(versionLen);
                if (versionLen > 0 && versionLen < 64) {
                    _version.resize(versionLen);
                    bs->Read(&_version[0], versionLen);
                }

                // Read hash
                bs->Read(_hash);
            }
        }

        bool Valid() const override {
            return !_resourceName.empty() && _command <= ResourceCommandType::Reload;
        }

        ResourceCommandType GetCommand() const {
            return _command;
        }

        const std::string &GetResourceName() const {
            return _resourceName;
        }

        const std::string &GetVersion() const {
            return _version;
        }

        uint32_t GetHash() const {
            return _hash;
        }

        const char *GetCommandString() const {
            switch (_command) {
            case ResourceCommandType::Start: return "Start";
            case ResourceCommandType::Stop: return "Stop";
            case ResourceCommandType::Restart: return "Restart";
            case ResourceCommandType::Reload: return "Reload";
            default: return "Unknown";
            }
        }
    };

} // namespace Framework::Networking::Messages
