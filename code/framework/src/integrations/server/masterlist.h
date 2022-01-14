/*
 * MafiaHub OSS license
 * Copyright (c) 2022, MafiaHub. All rights reserved.
 *
 * This file comes from MafiaHub, hosted at https://github.com/MafiaHub/Framework.
 * See LICENSE file in the source repository for information regarding licensing.
 */

#pragma once

#include "services/masterlist.h"

#include <string>

namespace Framework::Integrations::Server {
    class Instance;
    class Masterlist final: public Services::Masterlist {
      private:
        Instance *_instance = nullptr;

      public:
        Masterlist(Instance *instance);

      protected:
        virtual std::string GetGame() override;

        virtual std::string GetMap() override;

        virtual std::string GetSecretKey() override;

        virtual std::string GetServerName() override;

        virtual std::string GetHost() override;

        virtual int32_t GetPort() override;

        virtual std::string GetVersion() override;

        virtual bool IsPassworded() override;

        virtual int32_t GetMaxPlayers() override;

        virtual int32_t GetPlayers() override;
    };
} // namespace Framework::Integrations::Server
