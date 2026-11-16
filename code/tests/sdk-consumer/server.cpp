/*
 * MafiaHub OSS license
 * Copyright (c) 2026, MafiaHub. All rights reserved.
 *
 * This file comes from MafiaHub, hosted at https://github.com/MafiaHub/Framework.
 * See LICENSE file in the source repository for information regarding licensing.
 */

// A server shaped like a mod's, compiled and linked against the installed SDK only. It never runs;
// constructing the instance and filling the options is what exercises the headers and the link.

#include <integrations/server/instance.h>
#include <logging/logger.h>
#include <networking/network_server.h>
#include <networking/replication/entity_registry.h>
#include <networking/replication/network_entity.h>
#include <networking/replication/world.h>
#include <networking/rpc/rpc.h>
#include <networking/sender.h>
#include <utils/version.h>

#include <memory>

namespace {
    using Framework::Networking::Sender;
    using Framework::Networking::Replication::World;

    struct SmokePayload {
        static constexpr auto kIdentifier = FW_RPC_IDENTIFIER("SdkConsumer::Smoke");
        int value = 0;

        void Serialize(MafiaNet::BitStream *bs, bool write) {
            bs->Serialize(write, value);
        }
    };
    static_assert(Framework::Networking::RPC::Payload<SmokePayload>);

    // A mod's avatar, and a piece of its state that is not an entity.
    struct SmokeAvatar final: Framework::Networking::Replication::NetworkEntity {
        static constexpr const char *kTypeName = "SdkConsumer::Avatar";
        int smoked                             = 0;
    };
    struct SmokeLedger final {
        int total = 0;
    };

    class Server final : public Framework::Integrations::Server::Instance {
      public:
        // The server side of the API as a mod uses it: state registered with the world, a handler given
        // the sender and the world, an entity and the state taken in one call, and a reply.
        void PostInit() override {
            Framework::Logging::GetLogger(FRAMEWORK_INNER_SERVER)->info("SDK consumer server {} on Framework {}", GetOptions().modVersion, Framework::Utils::Version::rel);
            Framework::Networking::Replication::EntityRegistry::Get().Register<SmokeAvatar>(SmokeAvatar::kTypeName);

            auto *net = GetNetworkingEngine()->GetNetworkServer();
            net->GetReplicationManager()->GetWorld().RegisterState(std::make_unique<SmokeLedger>());
            net->RegisterRPC<SmokePayload>([](const SmokePayload &payload, Sender sender, World &world) {
                auto [avatar, ledger] = world.Write<SmokeAvatar, SmokeLedger>(sender.Avatar());
                if (!avatar || !ledger) {
                    return;
                }
                avatar->smoked += payload.value;
                ledger->total += payload.value;
                SmokePayload reply;
                reply.value = ledger->total;
                sender.Reply(reply);
            });
        }

        void OnPlayerConnect(const Framework::Integrations::Server::PlayerConnectionData &data, World &world) override {
            auto *replication = GetNetworkingEngine()->GetNetworkServer()->GetReplicationManager();
            if (auto avatar = world.Create<SmokeAvatar>()) {
                replication->SetOwner(avatar, data.guid);
                replication->SetViewer(data.guid, avatar);
            }
        }

        void OnPlayerDisconnect(MafiaNet::PeerGuid guid, World &world) override {
            const uint64_t avatarId = GetNetworkingEngine()->GetNetworkServer()->GetReplicationManager()->GetViewerId(guid);
            if (const auto avatar = world.Read<SmokeAvatar>(avatarId)) {
                Framework::Logging::GetLogger(FRAMEWORK_INNER_SERVER)->info("Avatar {} left having smoked {}", avatar.Id(), avatar->smoked);
            }
        }
    };
} // namespace

int main(int argc, char **argv) {
    Framework::Integrations::Server::InstanceOptions opts;
    opts.modName            = "SdkConsumer";
    opts.modSlug            = "sdk_consumer";
    opts.gameName           = "SDK";
    opts.gameVersion        = "1.0.0";
    opts.modVersion         = "0.0.1";
    opts.bindHost           = "127.0.0.1";
    opts.bindPort           = 27915;
    opts.maxPlayers         = 1;
    opts.rpcIdentifierSalt  = 0x5DC0FFEEULL;
    opts.argc               = argc;
    opts.argv               = argv;

    Server server;
    return opts.rpcIdentifierSalt == Framework::Networking::RPC::kDefaultIdentifierSalt ? 1 : 0;
}
