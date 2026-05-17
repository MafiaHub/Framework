/*
 * MafiaHub OSS license
 * Copyright (c) 2021-2026, MafiaHub. All rights reserved.
 *
 * This file comes from MafiaHub, hosted at https://github.com/MafiaHub/Framework.
 * See LICENSE file in the source repository for information regarding licensing.
 */

/*
 * Framework Server Plugin SDK — C ABI (v1)
 * ===========================================================================
 *
 * This header defines the binary contract between the Framework server and
 * a native plugin shared library (.dll / .so) loaded at server startup.
 *
 * It is pure C and self-contained. Plugins may be written in C or C++.
 * C++ plugin authors are encouraged to use the higher-level wrapper in
 * fw_plugin.hpp, which sits entirely on top of this ABI.
 *
 * ---------------------------------------------------------------------------
 * Manifest
 * ---------------------------------------------------------------------------
 *
 * Each plugin ships alongside a JSON manifest (<entry>.module.json) declaring:
 *
 *   {
 *     "name":          "anticheat-vanilla",
 *     "version":       "1.2.0",
 *     "abi_version":   1,
 *     "entry":         "anticheat-vanilla",      // platform suffix appended
 *     "depends_on":    [
 *         { "name": "telemetry-core", "version": ">=0.3.0" }
 *     ],
 *     "capabilities":  ["http_endpoints", "commands"]
 *   }
 *
 * The server's `server.json` enumerates which plugins to load:
 *
 *   { "modules": ["anticheat-vanilla", "telemetry-core"] }
 *
 * There is no filesystem auto-discovery. Plugins not listed are ignored.
 *
 * ---------------------------------------------------------------------------
 * Required exports
 * ---------------------------------------------------------------------------
 *
 *   FW_PLUGIN_EXPORT const FwPluginInfo* fw_plugin_info(void);
 *   FW_PLUGIN_EXPORT int                 fw_plugin_init(FwHost* host);
 *   FW_PLUGIN_EXPORT void                fw_plugin_shutdown(FwHost* host);
 *
 * Optional exports (host uses dlsym and treats missing as no-op):
 *
 *   FW_PLUGIN_EXPORT void fw_plugin_post_script_init(FwHost* host);
 *   FW_PLUGIN_EXPORT void fw_plugin_update(FwHost* host, double dt_seconds);
 *   FW_PLUGIN_EXPORT void fw_plugin_pre_shutdown(FwHost* host);
 *
 * ---------------------------------------------------------------------------
 * Lifecycle (mirrors Framework::Integrations::Server::Instance hooks)
 * ---------------------------------------------------------------------------
 *
 *   load             host validates manifest + abi_version, dlopens the .so
 *   fw_plugin_info   host reads name/version, verifies abi_version
 *   fw_plugin_init   plugin registers commands/endpoints/callbacks via host
 *   fw_plugin_post_script_init   plugin may register scripting builtins
 *   fw_plugin_update (each tick) plugin runs per-tick work, if any
 *   fw_plugin_pre_shutdown       plugin flushes state, closes resources
 *   fw_plugin_shutdown           plugin releases everything; host dlcloses
 *
 * Every host call into the plugin is wrapped in an exception fence by the
 * loader. A throwing/crashing plugin is logged and disabled, not propagated.
 *
 * ---------------------------------------------------------------------------
 * Memory ownership
 * ---------------------------------------------------------------------------
 *
 * - All strings are null-terminated UTF-8 unless explicitly paired with a
 *   length argument.
 * - Pointers handed from host to plugin are valid only for the duration of
 *   the call that received them, unless documented otherwise.
 * - The plugin never returns heap memory to the host. To produce output
 *   (e.g. HTTP response bodies), the plugin writes through host-provided
 *   setter functions that own the allocation.
 *
 * This sidesteps allocator-mismatch UB across CRT boundaries.
 *
 * ---------------------------------------------------------------------------
 * ABI compatibility
 * ---------------------------------------------------------------------------
 *
 * - FW_PLUGIN_ABI_VERSION is bumped on any breaking change to the structs
 *   or callback signatures below. Plugin's FwPluginInfo.abi_version must
 *   match exactly. The host refuses to load otherwise.
 * - The FwHostVTable is append-only across patch versions. New function
 *   pointers go at the end; older plugins ignore them. Removing or
 *   reordering vtable entries requires bumping FW_PLUGIN_ABI_VERSION.
 * ===========================================================================
 */

#ifndef FW_PLUGIN_ABI_H
#define FW_PLUGIN_ABI_H

#include <stddef.h>
#include <stdint.h>

#define FW_PLUGIN_ABI_VERSION 1

#if defined(_WIN32)
#    define FW_PLUGIN_EXPORT __declspec(dllexport)
#else
#    define FW_PLUGIN_EXPORT __attribute__((visibility("default")))
#endif

#ifdef __cplusplus
extern "C" {
#endif

/* ------------------------------------------------------------------------- */
/* Opaque handle types                                                        */
/* ------------------------------------------------------------------------- */

typedef struct FwHost FwHost;
typedef struct FwLogger FwLogger;
typedef struct FwPlayer FwPlayer;
typedef struct FwHttpResponse FwHttpResponse;

/* ------------------------------------------------------------------------- */
/* Plugin descriptor                                                          */
/* ------------------------------------------------------------------------- */

typedef struct FwPluginInfo {
    const char *name;        /* matches manifest "name", e.g. "anticheat-vanilla" */
    const char *version;     /* semver string, matches manifest "version"         */
    uint32_t    abi_version; /* must equal FW_PLUGIN_ABI_VERSION                  */
} FwPluginInfo;

/* ------------------------------------------------------------------------- */
/* Callback signatures (host → plugin)                                        */
/* ------------------------------------------------------------------------- */

/*
 * Command invocation. argv has argc entries, all null-terminated UTF-8.
 * Strings remain valid only for the duration of the call.
 */
typedef void (*FwCommandCallback)(int argc, const char *const *argv, void *userdata);

/*
 * HTTP request handler. method/path/body strings remain valid for the call.
 * The plugin writes the reply through response_set_* host functions.
 */
typedef void (*FwHttpCallback)(const char *method,
                               const char *path,
                               const char *body,
                               size_t      body_len,
                               FwHttpResponse *response,
                               void           *userdata);

/*
 * Player event. The FwPlayer pointer is valid for the duration of the call.
 * The guid is also passed inline for convenience and lifetime-safe storage.
 */
typedef void (*FwPlayerEventCallback)(FwPlayer *player, uint64_t guid, void *userdata);

/* ------------------------------------------------------------------------- */
/* Host vtable                                                                */
/* ------------------------------------------------------------------------- */

/*
 * Append-only across patch versions. Adding new function pointers at the
 * end is non-breaking for older plugins (which simply don't call them).
 * Any reordering or removal requires bumping FW_PLUGIN_ABI_VERSION.
 */
typedef struct FwHostVTable {
    uint32_t abi_version; /* set to FW_PLUGIN_ABI_VERSION by the host */

    /* Logging --------------------------------------------------------------- */

    /* Returns a logger scoped to the plugin name. Owned by host; do not free. */
    FwLogger *(*logger_for)(FwHost *host, const char *plugin_name);

    void (*log_debug)(FwLogger *logger, const char *message);
    void (*log_info)(FwLogger *logger, const char *message);
    void (*log_warn)(FwLogger *logger, const char *message);
    void (*log_error)(FwLogger *logger, const char *message);

    /* Commands -------------------------------------------------------------- */

    /*
     * Registers a console command. Returns 0 on success, nonzero on failure
     * (e.g. name already taken). Registration persists for plugin lifetime;
     * commands are removed automatically when the plugin unloads.
     */
    int (*register_command)(FwHost *host, const char *name, const char *description, FwCommandCallback callback, void *userdata);

    /* HTTP endpoints -------------------------------------------------------- */

    /*
     * Registers an HTTP endpoint on the webserver. Returns 0 on success.
     * Path collisions with the Framework's built-in endpoints (e.g. "/")
     * are rejected.
     */
    int (*register_http_endpoint)(FwHost *host, const char *path, FwHttpCallback callback, void *userdata);

    void (*http_response_set_status)(FwHttpResponse *response, int status);
    void (*http_response_set_body)(FwHttpResponse *response, const char *body, size_t body_len);
    void (*http_response_set_header)(FwHttpResponse *response, const char *key, const char *value);

    /* Player events --------------------------------------------------------- */

    int (*on_player_connect)(FwHost *host, FwPlayerEventCallback callback, void *userdata);
    int (*on_player_disconnect)(FwHost *host, FwPlayerEventCallback callback, void *userdata);

    /* Player accessors ------------------------------------------------------ */

    uint64_t (*player_get_guid)(FwPlayer *player);

    /*
     * Copies the player's nickname into buf (null-terminated, truncated to
     * fit). Returns the full length in bytes (excluding null terminator),
     * so caller can detect truncation when (return >= buf_size).
     */
    size_t (*player_get_nickname)(FwPlayer *player, char *buf, size_t buf_size);

    /* --- new entries appended below in future patch versions --- */
} FwHostVTable;

/* ------------------------------------------------------------------------- */
/* Host instance handed to every plugin entry point                           */
/* ------------------------------------------------------------------------- */

struct FwHost {
    const FwHostVTable *vtable;
    void               *internal; /* opaque host implementation; plugin must not touch */
};

#ifdef __cplusplus
} /* extern "C" */
#endif

#endif /* FW_PLUGIN_ABI_H */
