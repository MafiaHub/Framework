#pragma once

#include "masterlist.h"

namespace Framework::Services {
    class MasterlistClient final : public MasterlistBase {
      public:
        MasterlistClient();
        bool Fetch();
    };
}; // namespace Framework::Services
