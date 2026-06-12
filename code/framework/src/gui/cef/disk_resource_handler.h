/*
 * MafiaHub OSS license
 * Copyright (c) 2021-2024, MafiaHub. All rights reserved.
 *
 * This file comes from MafiaHub, hosted at https://github.com/MafiaHub/Framework.
 * See LICENSE file in the source repository for information regarding licensing.
 */

#pragma once

#include "include/cef_request.h"
#include "include/cef_resource_handler.h"

#include <filesystem>

namespace Framework::GUI::CEF {
    // Resolves the request URL against a directory tree ("/" maps to
    // index.html) and returns a streaming handler for the file, for use with
    // RegisterSchemeHandlerFactory. Anything missing or escaping the root
    // yields a 404 handler (never nullptr — that would fall through to the
    // network stack for standard schemes).
    CefRefPtr<CefResourceHandler> CreateDiskResourceHandler(const std::filesystem::path &root, const CefRefPtr<CefRequest> &request);
} // namespace Framework::GUI::CEF
