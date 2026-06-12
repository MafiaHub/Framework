/*
 * MafiaHub OSS license
 * Copyright (c) 2021-2024, MafiaHub. All rights reserved.
 *
 * This file comes from MafiaHub, hosted at https://github.com/MafiaHub/Framework.
 * See LICENSE file in the source repository for information regarding licensing.
 */

#include "disk_resource_handler.h"

#include "include/cef_parser.h"
#include "include/cef_stream.h"
#include "include/wrapper/cef_stream_resource_handler.h"

#include <utils/path.h>

#include <algorithm>

namespace Framework::GUI::CEF {
    namespace {
        CefRefPtr<CefResourceHandler> NotFound() {
            static const char kBody[] = "Not Found";
            // CreateForData does not copy — the static literal outlives the handler
            return new CefStreamResourceHandler(404, "Not Found", "text/plain", {}, CefStreamReader::CreateForData(const_cast<char *>(kBody), sizeof(kBody) - 1));
        }
    } // namespace

    CefRefPtr<CefResourceHandler> CreateDiskResourceHandler(const std::filesystem::path &root, const CefRefPtr<CefRequest> &request) {
        CefURLParts urlParts;
        if (!request || !CefParseURL(request->GetURL(), urlParts)) {
            return NotFound();
        }

        const std::string urlPath = CefURIDecode(CefString(&urlParts.path), false, UU_SPACES).ToString();

        // Empty = rejected: the path escaped the root (e.g. via "..")
        const auto file = Framework::Utils::ResolvePathUnderRoot(root, urlPath);
        if (file.empty()) {
            return NotFound();
        }

        // wstring(): path::string() narrows to the ANSI codepage on Windows but
        // CefString decodes std::string as UTF-8 — non-ASCII paths would 404
        CefRefPtr<CefStreamReader> stream = CefStreamReader::CreateForFile(file.wstring());
        if (!stream) {
            return NotFound();
        }

        std::string ext = file.extension().string();
        if (!ext.empty() && ext.front() == '.') {
            ext.erase(0, 1);
        }
        std::transform(ext.begin(), ext.end(), ext.begin(), [](unsigned char c) {
            return static_cast<char>(std::tolower(c));
        });
        std::string mime = CefGetMimeType(ext).ToString();
        if (mime.empty()) {
            mime = "application/octet-stream";
        }

        // Local files change while iterating on UIs — never serve a stale copy
        CefResponse::HeaderMap headers;
        headers.emplace("Cache-Control", "no-store");

        return new CefStreamResourceHandler(200, "OK", mime, headers, stream);
    }
} // namespace Framework::GUI::CEF
