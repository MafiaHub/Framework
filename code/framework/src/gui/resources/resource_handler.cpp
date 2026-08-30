/*
 * MafiaHub OSS license
 * Copyright (c) 2021-2024, MafiaHub. All rights reserved.
 *
 * This file comes from MafiaHub, hosted at https://github.com/MafiaHub/Framework.
 * See LICENSE file in the source repository for information regarding licensing.
 */

#include "resource_handler.h"

#include "mime.h"
#include "scheme.h"

#include <logging/logger.h>

#include "include/cef_request.h"
#include "include/cef_response.h"
#include "include/cef_task.h"

#include <functional>
#include <utility>

namespace Framework::GUI::Resources {
    namespace {
        constexpr const char *kLogger = "Web";

        // CEF's own convention for "the read failed" is a negative count
        // carrying a Chromium net error, of which ERR_FAILED is -2.
        constexpr int kReadFailed = -2;

        bool IsReadOnlyMethod(const std::string &method) {
            return method == "GET" || method == "HEAD";
        }

        // CEF ships base::BindOnce for this, but its bind internals do not
        // compile under the toolchain settings this framework builds with, so
        // the work is wrapped in a plain CefTask instead.
        class CallbackTask final: public CefTask {
          private:
            std::function<void()> _work;

          public:
            explicit CallbackTask(std::function<void()> work): _work(std::move(work)) {}

            void Execute() override {
                _work();
            }

            IMPLEMENT_REFCOUNTING(CallbackTask);
        };

        // TID_FILE_BACKGROUND is CEF's low-priority blocking-IO pool, the one
        // place in the process where a synchronous disk read belongs.
        void PostToFileThread(std::function<void()> work) {
            CefPostTask(TID_FILE_BACKGROUND, CefRefPtr<CefTask>(new CallbackTask(std::move(work))));
        }
    } // namespace

    ResourceHandler::ResourceHandler(std::shared_ptr<ResourceProvider> provider, std::string host, std::string path): _provider(std::move(provider)), _host(std::move(host)), _path(std::move(path)) {}

    ResourceHandler::ResourceHandler(int status, std::string message, std::string host, std::string path): _host(std::move(host)), _path(std::move(path)), _rejectStatus(status), _rejectMessage(std::move(message)) {}

    void ResourceHandler::ServeInline(int status, std::string body) {
        _status    = status;
        _mimeType  = "text/plain";
        _stat      = ResourceStat {};
        _stat.size = body.size();
        _stream    = std::make_unique<MemoryStream>(std::move(body));
    }

    bool ResourceHandler::Open(CefRefPtr<CefRequest> request, bool &handleRequest, CefRefPtr<CefCallback> callback) {
        handleRequest = true;

        const std::string method = request->GetMethod().ToString();
        if (!IsReadOnlyMethod(method)) {
            // A write method against a static root is a bug in the page, not a
            // resource that happens to be missing. Say so rather than 404.
            ServeInline(405, "405 Method Not Allowed");
            return true;
        }

        if (_rejectStatus != 0) {
            ServeInline(_rejectStatus, _rejectMessage);
            return true;
        }

        // The provider is about to touch the disk, so leave the IO thread.
        handleRequest = false;
        PostToFileThread([self = CefRefPtr<ResourceHandler>(this), callback] { self->OpenOnFileThread(callback); });
        return true;
    }

    void ResourceHandler::OpenOnFileThread(CefRefPtr<CefCallback> callback) {
        if (_cancelled) {
            return;
        }

        ResourceStat stat;
        _stream = _provider->Open(_path, stat);
        if (!_stream) {
            ServeInline(404, "404 Not Found");
            Framework::Logging::GetLogger(kLogger)->warn("Resource {}://{}/{} is missing from {}", kResourceScheme, _host, _path, _provider->Describe());
        }
        else {
            _status   = 200;
            _stat     = stat;
            _mimeType = stat.mimeType.empty() ? MimeTypeForPath(_path) : stat.mimeType;
        }

        callback->Continue();
    }

    void ResourceHandler::GetResponseHeaders(CefRefPtr<CefResponse> response, std::int64_t &responseLength, CefString &redirectUrl) {
        response->SetStatus(_status);
        response->SetMimeType(_mimeType);

        CefResponse::HeaderMap headers;
        response->GetHeaderMap(headers);

        // Chromium guesses the encoding from the locale when the charset is
        // absent, so every text type states it.
        headers.emplace("content-type", MimeTypeIsTextual(_mimeType) ? _mimeType + "; charset=utf-8" : _mimeType);

        // The scheme is CORS-enabled, so one root fetching another needs this;
        // there is nothing behind it a page in this process may not read.
        headers.emplace("access-control-allow-origin", "*");
        headers.emplace("access-control-allow-methods", "GET, HEAD, OPTIONS");

        // The MIME type above is authoritative; never let a sniff override it.
        headers.emplace("x-content-type-options", "nosniff");

        if (_status == 200 && _stat.immutable) {
            headers.emplace("cache-control", "public, max-age=31536000, immutable");
        }
        else {
            // Loose files change under the same URL whenever a developer edits
            // one, so the default has to be revalidate-always.
            headers.emplace("cache-control", "no-cache, must-revalidate");
        }

        response->SetHeaderMap(headers);
        responseLength = static_cast<std::int64_t>(_stat.size);
    }

    bool ResourceHandler::Skip(std::int64_t bytesToSkip, std::int64_t &bytesSkipped, CefRefPtr<CefResourceSkipCallback> callback) {
        if (!_stream) {
            bytesSkipped = kReadFailed;
            return false;
        }

        bytesSkipped = 0;
        PostToFileThread([self = CefRefPtr<ResourceHandler>(this), bytesToSkip, callback] { self->SkipOnFileThread(bytesToSkip, callback); });
        return true;
    }

    void ResourceHandler::SkipOnFileThread(std::int64_t bytesToSkip, CefRefPtr<CefResourceSkipCallback> callback) {
        if (_cancelled) {
            return;
        }

        const std::int64_t skipped = _stream->Skip(bytesToSkip);
        callback->Continue(skipped > 0 ? skipped : kReadFailed);
    }

    bool ResourceHandler::Read(void *dataOut, int bytesToRead, int &bytesRead, CefRefPtr<CefResourceReadCallback> callback) {
        if (!_stream || bytesToRead <= 0) {
            bytesRead = kReadFailed;
            return false;
        }

        bytesRead = 0;
        PostToFileThread([self = CefRefPtr<ResourceHandler>(this), dataOut, bytesToRead, callback] { self->ReadOnFileThread(dataOut, bytesToRead, callback); });
        return true;
    }

    void ResourceHandler::ReadOnFileThread(void *dataOut, int bytesToRead, CefRefPtr<CefResourceReadCallback> callback) {
        if (_cancelled) {
            return;
        }

        const std::int64_t read = _stream->Read(dataOut, static_cast<std::size_t>(bytesToRead));
        if (read < 0) {
            Framework::Logging::GetLogger(kLogger)->error("Resource {}://{}/{} failed mid-read", kResourceScheme, _host, _path);
            callback->Continue(kReadFailed);
            return;
        }

        // Zero completes the response; anything positive asks for another Read.
        callback->Continue(static_cast<int>(read));
    }

    void ResourceHandler::Cancel() {
        _cancelled = true;
    }
} // namespace Framework::GUI::Resources
