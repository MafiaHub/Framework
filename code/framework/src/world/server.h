/*
 * MafiaHub OSS license
 * Copyright (c) 2022, MafiaHub. All rights reserved.
 *
 * This file comes from MafiaHub, hosted at https://github.com/MafiaHub/Framework.
 * See LICENSE file in the source repository for information regarding licensing.
 */

#pragma once

#include "engine.h"
#include "errors.h"

#include <flecs/flecs.h>

#include <memory>
#include <string>
#include <function2.hpp>
#include <vector>

namespace Framework::World {
    class ServerEngine: public Engine {
      protected:
        using IsVisibleProc = fu2::function<bool(const flecs::entity streamerEntity, const flecs::entity e, const Modules::Base::Transform &lhsTr, const Modules::Base::Streamer &streamer, const Modules::Base::Streamable &lhsS,
                             const Modules::Base::Transform &rhsTr, const Modules::Base::Streamable rhsS) const>;
        IsVisibleProc _isEntityVisible;
      public:
        EngineError Init(Framework::Networking::NetworkPeer *networkPeer, float tickInterval);

        EngineError Shutdown() override;

        void Update() override;

        flecs::entity CreateEntity(const std::string& name = "");

        static bool IsEntityOwner(flecs::entity e, uint64_t guid) ;
        static void SetOwner(flecs::entity e, uint64_t guid);
        flecs::entity GetOwner(flecs::entity e) const;
        std::vector<flecs::entity> FindVisibleStreamers(flecs::entity e) const;

        static void RemoveEntity(flecs::entity e);
    };
} // namespace Framework::World
