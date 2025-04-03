#include "module.h"

#include "core_modules.h"

namespace Framework::Integrations::Client::Scripting {
    ClientScriptingModule::ClientScriptingModule(std::shared_ptr<World::ClientEngine> world): _world(world) {
        _clientEngine = std::make_shared<Framework::Scripting::ClientEngine>();
        CoreModules::SetScriptingEngine(_clientEngine.get());
    }

    bool ClientScriptingModule::Init(Framework::Scripting::SDKRegisterCallback cb) {
        if (_clientEngine->Init(cb) != Framework::Scripting::EngineError::ENGINE_NONE) {
            _clientEngine.reset();
            return false;
        }
        return true;
    }

    bool ClientScriptingModule::Shutdown() {
        _clientEngine->Shutdown();
        return true;
    }
}
