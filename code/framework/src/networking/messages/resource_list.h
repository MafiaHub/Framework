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
#include <vector>

namespace Framework::Networking::Messages {

    /**
     * Information about a single resource for synchronization.
     */
    struct ResourceInfo {
        std::string name;
        std::string version;
        uint32_t hash = 0; // Content hash for cache invalidation

        void Serialize(SLNet::BitStream *bs, bool write) {
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
     * Message sent from server to client containing the list of resources to load.
     * Sent during the connection flow after handshake, before asset download.
     */
    class ResourceListMessage final: public IMessage {
      private:
        std::vector<ResourceInfo> _resources;

      public:
        uint8_t GetMessageID() const override {
            return GAME_RESOURCE_LIST;
        }

        void FromParameters(const std::vector<ResourceInfo> &resources) {
            _resources = resources;
        }

        void AddResource(const std::string &name, const std::string &version, uint32_t hash) {
            _resources.push_back({name, version, hash});
        }

        void Serialize(SLNet::BitStream *bs, bool write) override {
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

        bool Valid() const override {
            return true; // Empty list is valid (no resources to load)
        }

        const std::vector<ResourceInfo> &GetResources() const {
            return _resources;
        }

        size_t GetResourceCount() const {
            return _resources.size();
        }
    };

} // namespace Framework::Networking::Messages
