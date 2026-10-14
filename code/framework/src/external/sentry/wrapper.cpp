/*
 * MafiaHub OSS license
 * Copyright (c) 2021-2023, MafiaHub. All rights reserved.
 *
 * This file comes from MafiaHub, hosted at https://github.com/MafiaHub/Framework.
 * See LICENSE file in the source repository for information regarding licensing.
 */

#include "wrapper.h"

#include "crash_trail.h"

#include <sentry.h>

#include <cppfs/FileHandle.h>
#include <cppfs/FileIterator.h>
#include <cppfs/fs.h>
#include <logging/logger.h>

#include <filesystem>
#include <memory>
#include <system_error>

namespace Framework::External::Sentry {
    namespace {
        sentry_value_t ToSentryValue(const ContextValue &value) {
            switch (value.type) {
            case ContextValue::Type::Int: return sentry_value_new_int32(value.integer);
            case ContextValue::Type::Double: return sentry_value_new_double(value.number);
            case ContextValue::Type::Bool: return sentry_value_new_bool(value.boolean);
            case ContextValue::Type::String:
            default: return sentry_value_new_string(value.string.c_str());
            }
        }

        sentry_value_t ToSentryObject(const ContextFields &fields) {
            const sentry_value_t object = sentry_value_new_object();
            for (const auto &[key, value] : fields) {
                sentry_value_set_by_key(object, key.c_str(), ToSentryValue(value));
            }
            return object;
        }

        const char *LevelToString(Level level) {
            switch (level) {
            case Level::Trace:
            case Level::Debug: return "debug";
            case Level::Warning: return "warning";
            case Level::Error: return "error";
            case Level::Fatal: return "fatal";
            case Level::Info:
            default: return "info";
            }
        }

        void AddOptionsAttachment(sentry_options_t *options, const std::filesystem::path &path) {
#ifdef _WIN32
            sentry_options_add_attachmentw(options, path.c_str());
#else
            sentry_options_add_attachment(options, path.c_str());
#endif
        }
    } // namespace

    Utils::Result<void, Framework::Error> Wrapper::Init(const InitOptions &options) try {
        // sentry_init takes ownership below; every path that bails out before that
        // has to free the payload itself, so let a guard own it until then.
        std::unique_ptr<sentry_options_t, decltype(&sentry_options_free)> opts(sentry_options_new(), sentry_options_free);
        sentry_options_set_dsn(opts.get(), options.dsn.c_str());

        if (!options.release.empty()) {
            sentry_options_set_release(opts.get(), options.release.c_str());
        }
        if (!options.environment.empty()) {
            sentry_options_set_environment(opts.get(), options.environment.c_str());
        }
        sentry_options_set_max_breadcrumbs(opts.get(), static_cast<size_t>(options.maxBreadcrumbs));

        std::string handlerName = "crashpad_handler.exe";
#if defined(__APPLE__) || defined(__linux__)
        handlerName = "crashpad_handler";
#endif

        // Setup the breakpad path
        const cppfs::FileHandle breakpadFile = cppfs::fs::open(options.handlerPath + "/" + handlerName);
        if (!breakpadFile.exists()) {
            return Framework::Error("Failed to locate the crashpad handler at " + breakpadFile.path());
        }

        cppfs::FileHandle cacheDirectory = cppfs::fs::open(options.handlerPath + "/cache/sentry");
        if (!cacheDirectory.isDirectory()) {
            cppfs::FileHandle cacheRoot = cppfs::fs::open(options.handlerPath + "/cache");
            if (!cacheRoot.isDirectory()) {
                cacheRoot.createDirectory();
            }
            if (!cacheDirectory.createDirectory()) {
                return Framework::Error("Failed to create the Sentry cache directory");
            }
        }

        // The framework accepts native filesystem strings; Sentry's narrow Windows
        // API requires UTF-8. Preserve the native path through its wide API instead.
        const std::filesystem::path handlerPath(breakpadFile.path());
        const std::filesystem::path databasePath(cacheDirectory.path());
#ifdef _WIN32
        sentry_options_set_handler_pathw(opts.get(), handlerPath.c_str());
        sentry_options_set_database_pathw(opts.get(), databasePath.c_str());
#else
        sentry_options_set_handler_path(opts.get(), handlerPath.c_str());
        sentry_options_set_database_path(opts.get(), databasePath.c_str());
#endif

        // Crashpad reads attachments lazily at crash time, so cef.log carries the CHECK/FATAL
        // line CEF writes before it fast-fails. Registered before init so the handler knows it.
        const auto cefLog = std::filesystem::absolute(std::filesystem::path(options.handlerPath) / "logs" / "cef.log");
        AddOptionsAttachment(opts.get(), cefLog);

        for (const auto &attachment : options.attachments) {
            AddOptionsAttachment(opts.get(), std::filesystem::absolute(std::filesystem::path(attachment)));
        }

        if (sentry_init(opts.release()) != 0) {
            return Framework::Error("Failed to initialize Sentry");
        }
        _initialized = true;

        // In front of crashpad, so a crash on a thread nothing else wraps still
        // leaves its location in the log the report carries.
        CrashTrail::Install();
        return {};
    }
    catch (const std::system_error &error) {
        return Framework::Error("Failed to configure Sentry paths: " + std::string(error.what()), error.code().value());
    }

    bool Wrapper::Flush(uint32_t timeoutMs) const {
        if (!_initialized) {
            return false;
        }
        return sentry_flush(static_cast<uint64_t>(timeoutMs)) == 0;
    }

    void Wrapper::Shutdown() {
        if (!_initialized) {
            return;
        }
        Logging::GetInstance()->SetLogForwarder(nullptr);
        sentry_close();
        Lifecycle::Shutdown();
    }

    Utils::Result<void, Framework::Error> Wrapper::AddAttachment(const std::string &path) const try {
        if (!_initialized) {
            return Framework::Error {"Sentry is not initialized"};
        }
        const auto absolute = std::filesystem::absolute(std::filesystem::path(path));
#ifdef _WIN32
        sentry_attach_filew(absolute.c_str());
#else
        sentry_attach_file(absolute.c_str());
#endif
        return {};
    }
    catch (const std::system_error &error) {
        return Framework::Error("Failed to configure Sentry attachment path: " + std::string(error.what()), error.code().value());
    }

    void Wrapper::SetTag(const std::string &key, const std::string &value) const {
        if (!_initialized) {
            return;
        }
        sentry_set_tag(key.c_str(), value.c_str());
    }

    void Wrapper::RemoveTag(const std::string &key) const {
        if (!_initialized) {
            return;
        }
        sentry_remove_tag(key.c_str());
    }

    void Wrapper::SetContext(const std::string &name, const ContextFields &fields) const {
        if (!_initialized) {
            return;
        }
        sentry_set_context(name.c_str(), ToSentryObject(fields));
    }

    void Wrapper::AddBreadcrumb(const std::string &category, const std::string &message, Level level, const ContextFields &data) const {
        if (!_initialized) {
            return;
        }
        const sentry_value_t crumb = sentry_value_new_breadcrumb("default", message.c_str());
        sentry_value_set_by_key(crumb, "category", sentry_value_new_string(category.c_str()));
        sentry_value_set_by_key(crumb, "level", sentry_value_new_string(LevelToString(level)));
        if (!data.empty()) {
            sentry_value_set_by_key(crumb, "data", ToSentryObject(data));
        }
        sentry_add_breadcrumb(crumb);
    }

    Utils::Result<void, Framework::Error> Wrapper::SetGameInformation(const GameInformation &infos) const {
        if (!_initialized) {
            return Framework::Error {"Sentry is not initialized"};
        }
        SetContext("game", {{"title", infos._title}, {"version", infos._version}});
        return {};
    }

    Utils::Result<void, Framework::Error> Wrapper::SetScreenInformation(const ScreenInformation &infos) const {
        if (!_initialized) {
            return Framework::Error {"Sentry is not initialized"};
        }
        SetContext("display", {{"width", infos._width}, {"height", infos._height}, {"fullscreen", infos._fullscreen}});
        return {};
    }

    Utils::Result<void, Framework::Error> Wrapper::SetSystemInformation(const SystemInformation &infos) const {
        if (!_initialized) {
            return Framework::Error {"Sentry is not initialized"};
        }
        ContextFields fields {
            {"cpuBrandString", infos._cpuBrand},
            {"cpuProcessors", static_cast<int32_t>(infos._cpuProcessorsCount)},
        };
        if (!infos._osVersion.empty()) {
            fields.emplace("osVersion", infos._osVersion);
            fields.emplace("osMajorVersion", infos._osMajorVersion);
            fields.emplace("osMinorVersion", infos._osMinorVersion);
            fields.emplace("osBuildNumber", infos._osBuildNumber);
        }
        SetContext("system", fields);
        return {};
    }

    Utils::Result<void, Framework::Error> Wrapper::SetUserInformation(const UserInformation &infos) const {
        if (!_initialized) {
            return Framework::Error {"Sentry is not initialized"};
        }
        const sentry_value_t user = sentry_value_new_object();
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
        return {};
    }

    Utils::Result<void, Framework::Error> Wrapper::CaptureEventException(const std::string &type, const std::string &message) const {
        if (!_initialized) {
            return Framework::Error {"Sentry is not initialized"};
        }
        const sentry_value_t event = sentry_value_new_event();
        const sentry_value_t exc   = sentry_value_new_exception(type.c_str(), message.c_str());
        sentry_event_add_exception(event, exc);
        sentry_capture_event(event);
        return {};
    }

    Utils::Result<void, Framework::Error> Wrapper::CaptureEventMessage(int32_t level, const std::string &logger, const std::string &payload) const {
        if (!_initialized) {
            return Framework::Error {"Sentry is not initialized"};
        }
        sentry_capture_event(sentry_value_new_message_event(static_cast<sentry_level_e>(level), logger.c_str(), payload.c_str()));
        return {};
    }

    Wrapper &GetCrashReporter() {
        // Leaked: must outlive every static destructor, or a fault during teardown races the
        // handler being freed.
        static Wrapper *reporter = new Wrapper();
        return *reporter;
    }

    Utils::Result<void, Framework::Error> InitCrashReporter(const InitOptions &options) {
        Wrapper &reporter = GetCrashReporter();
        if (reporter.IsInitialized()) {
            return {};
        }
        return reporter.Init(options);
    }

    bool IsCrashReporterReady() {
        return GetCrashReporter().IsInitialized();
    }

    void ShutdownCrashReporter() {
        Wrapper &reporter = GetCrashReporter();
        if (reporter.IsInitialized()) {
            reporter.Shutdown();
        }
    }
} // namespace Framework::External::Sentry
