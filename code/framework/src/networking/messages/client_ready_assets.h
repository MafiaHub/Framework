/*
 * MafiaHub OSS license
 * Copyright (c) 2021-2023, MafiaHub. All rights reserved.
 *
 * This file comes from MafiaHub, hosted at https://github.com/MafiaHub/Framework.
 * See LICENSE file in the source repository for information regarding licensing.
 */

#pragma once

#include "messages.h"

#include <mafianet/BitStream.h>
#include <mafianet/string.h>
#include <string>
#include <vector>

namespace Framework::Networking::Messages {

    /**
     * Information about a single resource for synchronization.
     */
    struct ResourceInfo {
        std::string name;
        std::string version;
        uint32_t hash = 0; // Content hash for cache invalidation

        void Serialize(MafiaNet::BitStream *bs, bool write) {
            if (write) {
                // Write name
                uint16_t nameLen = static_cast<uint16_t>(name.length());
                bs->Write(nameLen);
                if (nameLen > 0) {
                    bs->Write(name.c_str(), nameLen);
                }

                // Write version
                uint16_t versionLen = static_cast<uint16_t>(version.length());
                bs->Write(versionLen);
                if (versionLen > 0) {
                    bs->Write(version.c_str(), versionLen);
                }

                // Write hash
                bs->Write(hash);
            }
            else {
                // Read name
                uint16_t nameLen = 0;
                bs->Read(nameLen);
                if (nameLen > 0 && nameLen < 256) {
                    name.resize(nameLen);
                    bs->Read(&name[0], nameLen);
                }

                // Read version
                uint16_t versionLen = 0;
                bs->Read(versionLen);
                if (versionLen > 0 && versionLen < 64) {
                    version.resize(versionLen);
                    bs->Read(&version[0], versionLen);
                }

                // Read hash
                bs->Read(hash);
            }
        }
    };

    /**
     * Message sent from server to client when assets are ready for download.
     * Contains the client entry point script and list of resources to load.
     */
    class ClientReadyAssets final: public IMessage {
      private:
        MafiaNet::RakString _clientEntryPoint;
        std::vector<ResourceInfo> _resources;

      public:
        uint8_t GetMessageID() const override {
            return GAME_CONNECTION_READY_ASSETS;
        }

        void FromParameters(const std::string& clientEntryPoint) {
            _clientEntryPoint = clientEntryPoint.c_str();
        }

        void AddResource(const std::string &name, const std::string &version, uint32_t hash) {
            _resources.push_back({name, version, hash});
        }

        void Serialize(MafiaNet::BitStream *bs, bool write) override {
            bs->Serialize(write, _clientEntryPoint);

            if (write) {
                uint16_t count = static_cast<uint16_t>(_resources.size());
                bs->Write(count);
                for (auto &resource : _resources) {
                    resource.Serialize(bs, true);
                }
            }
            else {
                uint16_t count = 0;
                bs->Read(count);
                _resources.clear();
                _resources.reserve(count);
                for (uint16_t i = 0; i < count && i < 1000; ++i) { // Limit to 1000 resources
                    ResourceInfo info;
                    info.Serialize(bs, false);
                    _resources.push_back(info);
                }
            }
        }

        const std::string GetClientEntryPoint() const {
            return _clientEntryPoint.C_String();
        }

        const std::vector<ResourceInfo> &GetResources() const {
            return _resources;
        }

        size_t GetResourceCount() const {
            return _resources.size();
        }

        bool Valid() const override {
            return true;
        }
    };
} // namespace Framework::Networking::Messages
