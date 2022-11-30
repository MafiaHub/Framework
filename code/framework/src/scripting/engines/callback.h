#pragma once

#include <functional>

namespace Framework::Scripting::Engines {
    using SDKRegisterCallback = std::function<void(void *)>;
}