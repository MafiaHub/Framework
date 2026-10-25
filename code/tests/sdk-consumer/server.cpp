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
#include <networking/rpc/rpc.h>
#include <utils/version.h>

namespace {
    struct SmokePayload {
        static constexpr auto kIdentifier = FW_RPC_IDENTIFIER("SdkConsumer::Smoke");
        int value = 0;

        void Serialize(MafiaNet::BitStream *bs, bool write) {
            bs->Serialize(write, value);
        }
    };
    static_assert(Framework::Networking::RPC::Payload<SmokePayload>);

    class Server final : public Framework::Integrations::Server::Instance {
      public:
        void PostInit() override {
            Framework::Logging::GetLogger(FRAMEWORK_INNER_SERVER)->info("SDK consumer server {} on Framework {}", GetOptions().modVersion, Framework::Utils::Version::rel);
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
    opts.webBindHost        = "127.0.0.1";
    opts.webBindPort        = 27916;
    opts.maxPlayers         = 1;
    opts.rpcIdentifierSalt  = 0x5DC0FFEEULL;
    opts.argc               = argc;
    opts.argv               = argv;

    Server server;
    return opts.rpcIdentifierSalt == Framework::Networking::RPC::kDefaultIdentifierSalt ? 1 : 0;
}
