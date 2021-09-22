#include "instance.h"

namespace Framework::Integrations::Server {
    Instance::Instance(): _alive(false) {
        _scriptingEngine = new Scripting::Engine;
    }

    Instance::~Instance() {}

    ServerError Instance::Init(InstanceOptions &opts) {
        // Initialize the logging instance with the mod slug name
        Logging::GetInstance()->SetLogName(opts.modSlug);

        // Initialize the scripting engine
        if (_scriptingEngine->Init() != Scripting::ENGINE_NONE) {
            Logging::GetLogger(FRAMEWORK_INNER_SERVER)->critical("Failed to initialize the scripting engine");
            return ServerError::SERVER_SCRIPTING_INIT_FAILED;
        }

        _alive = true;
        Logging::GetLogger(FRAMEWORK_INNER_SERVER)->info("{} Server successfully started", opts.modName);
        Logging::GetLogger(FRAMEWORK_INNER_SERVER)->info("Name:\t{}", opts.bindName);
        Logging::GetLogger(FRAMEWORK_INNER_SERVER)->info("Host:\t{}", opts.bindHost);
        Logging::GetLogger(FRAMEWORK_INNER_SERVER)->info("Port:\t{}", opts.bindPort);
        Logging::GetLogger(FRAMEWORK_INNER_SERVER)->info("Max Players:\t{}", opts.maxPlayers);
        return ServerError::SERVER_NONE;
    }

    ServerError Instance::Shutdown(){
        if(_scriptingEngine){
            _scriptingEngine->Shutdown();
        }

        _alive = false;
        return ServerError::SERVER_NONE;
    }

    void Instance::Update(){
        const auto start = std::chrono::high_resolution_clock::now();
        if (_nextTick <= start) {
            if (_scriptingEngine) {
                _scriptingEngine->Update();
            }
        }

        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
} // namespace Framework::Integrations::Server