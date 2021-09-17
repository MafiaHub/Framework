#include "config.h"

#include <nlohmann/json.hpp>

namespace Framework::Utils {
    Config::Config() {
        _document = new nlohmann::json();
    }
    Config::~Config() {
        delete _document;
        _document = nullptr;
    }
    bool Config::Parse(const std::string &content) {
        try {
            const auto doc = nlohmann::json::parse(content, nullptr, true, true);
            *_document     = doc;
        } catch (nlohmann::json::exception &e) {
            _lastError = e.what();
            return false;
        }

        _lastError = "";
        return true;
    }
    const char *Config::GetDefaultConfig() {
        return "{}";
    }
    std::string Config::ToString() {
        if (!_lastError.empty())
            return "";
        return _document->dump();
    }
} // namespace Framework::Utils
