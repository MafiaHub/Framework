/*
 * MafiaHub OSS license
 * Copyright (c) 2022, MafiaHub. All rights reserved.
 *
 * This file comes from MafiaHub, hosted at https://github.com/MafiaHub/Framework.
 * See LICENSE file in the source repository for information regarding licensing.
 */

#include "wrapper.h"

#include <cppfs/FileHandle.h>
#include <cppfs/FileIterator.h>
#include <cppfs/fs.h>
#include <logging/logger.h>

namespace Framework::External::Sentry {
    SentryError Wrapper::Init(const std::string &key, const std::string &path) {
        // Build the options payload
        sentry_options_t *opts = sentry_options_new();
        sentry_options_set_dsn(opts, key.c_str());

        std::string handlerName = "crashpad_handler.exe";
#if defined(__APPLE__) || defined(__linux__)
        handlerName = "crashpad_handler";
#endif

        // Setup the breakpad path
        cppfs::FileHandle breakpadFile = cppfs::fs::open(path + "/" + handlerName);
        if (!breakpadFile.exists()) {
            Logging::GetLogger(FRAMEWORK_INNER_INTEGRATIONS)->debug("Failed to locate the crashpad handler");
            return SentryError::SENTRY_BREAKPAD_NOT_FOUND;
        }

        cppfs::FileHandle cacheDirectory = cppfs::fs::open(path + "/cache/sentry");
        auto result                      = cacheDirectory.createDirectory();
        if (!result) {
            return SentryError::SENTRY_CACHE_DIRECTORY_CREATION_FAILED;
        }

        sentry_options_set_handler_path(opts, breakpadFile.path().c_str());
        if (sentry_init(opts) > 0) {
            return SentryError::SENTRY_INIT_FAILED;
        }
        _valid = true;
        return SentryError::SENTRY_NONE;
    }

    SentryError Wrapper::Shutdown() {
        if (!_valid) {
            return SentryError::SENTRY_INVALID_INSTANCE;
        }
        sentry_close();
        return SentryError::SENTRY_NONE;
    }

    SentryError Wrapper::SetGameInformation(const GameInformation &infos) {
        if (!_valid) {
            return SentryError::SENTRY_INVALID_INSTANCE;
        }
        sentry_value_t game = sentry_value_new_object();
        sentry_value_set_by_key(game, "title", sentry_value_new_string(infos._title.c_str()));
        sentry_value_set_by_key(game, "version", sentry_value_new_string(infos._version.c_str()));
        sentry_set_extra("game", game);
        return SentryError::SENTRY_NONE;
    }

    SentryError Wrapper::SetScreenInformation(const ScreenInformation &infos) {
        if (!_valid) {
            return SentryError::SENTRY_INVALID_INSTANCE;
        }
        sentry_value_t screen = sentry_value_new_object();
        sentry_value_set_by_key(screen, "width", sentry_value_new_int32(infos._width));
        sentry_value_set_by_key(screen, "height", sentry_value_new_int32(infos._height));
        sentry_value_set_by_key(screen, "fullscreen", sentry_value_new_bool(infos._fullscreen));
        sentry_set_extra("screen", screen);
        return SentryError::SENTRY_NONE;
    }

    SentryError Wrapper::SetSystemInformation(const SystemInformation &infos) {
        if (!_valid) {
            return SentryError::SENTRY_INVALID_INSTANCE;
        }
        sentry_value_t system = sentry_value_new_object();
        sentry_value_set_by_key(system, "cpuBrandString", sentry_value_new_string(infos._cpuBrand.c_str()));
        sentry_value_set_by_key(system, "cpuProcessors", sentry_value_new_int32(infos._cpuProcessorsCount));

        // OS
        if (!infos._osVersion.empty()) {
            sentry_value_set_by_key(system, "osVersion", sentry_value_new_string(infos._osVersion.c_str()));
            sentry_value_set_by_key(system, "osMajorVersion", sentry_value_new_int32(infos._osMajorVersion));
            sentry_value_set_by_key(system, "osMinorVersion", sentry_value_new_int32(infos._osMinorVersion));
            sentry_value_set_by_key(system, "osBuildNumber", sentry_value_new_int32(infos._osBuildNumber));
        }

        sentry_set_extra("system", system);
        return SentryError::SENTRY_NONE;
    }

    SentryError Wrapper::SetUserInformation(const UserInformation &infos) {
        if (!_valid) {
            return SentryError::SENTRY_INVALID_INSTANCE;
        }
        sentry_value_t user = sentry_value_new_object();
        if (!infos._userId.empty()) {
            sentry_value_set_by_key(user, "id", sentry_value_new_string(infos._userId.c_str()));
        }
        if (!infos._fullName.empty()) {
            sentry_value_set_by_key(user, "fullName", sentry_value_new_string(infos._fullName.c_str()));
        }
        if (!infos._name.empty()) {
            sentry_value_set_by_key(user, "name", sentry_value_new_string(infos._name.c_str()));
        }
        sentry_set_user(user);
        return SentryError::SENTRY_NONE;
    }

    SentryError Wrapper::CaptureEventException(const std::string &type, const std::string &message) {
        if (!_valid) {
            return SentryError::SENTRY_INVALID_INSTANCE;
        }
        sentry_value_t exc = sentry_value_new_object();
        sentry_value_set_by_key(exc, "type", sentry_value_new_string(type.c_str()));
        sentry_value_set_by_key(exc, "value", sentry_value_new_string(message.c_str()));
        sentry_value_t event = sentry_value_new_event();
        sentry_value_set_by_key(event, "exception", exc);
        sentry_capture_event(event);
        return SentryError::SENTRY_NONE;
    }

    SentryError Wrapper::CaptureEventMessage(int32_t level, const std::string &logger, const std::string &payload) {
        if (!_valid) {
            return SentryError::SENTRY_INVALID_INSTANCE;
        }
        sentry_capture_event(sentry_value_new_message_event(static_cast<sentry_level_e>(level), logger.c_str(), payload.c_str()));
        return SentryError::SENTRY_NONE;
    }
} // namespace Framework::External::Sentry
