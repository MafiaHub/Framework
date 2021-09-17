#pragma once

#include <functional>

namespace Framework::Scripting {
    class SDK;
    using SDKRegisterCallback = std::function<void(SDK*)>;
}