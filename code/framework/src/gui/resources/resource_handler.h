/*
 * MafiaHub OSS license
 * Copyright (c) 2021-2024, MafiaHub. All rights reserved.
 *
 * This file comes from MafiaHub, hosted at https://github.com/MafiaHub/Framework.
 * See LICENSE file in the source repository for information regarding licensing.
 */

#pragma once

#include "resource_provider.h"

#include "include/cef_callback.h"
#include "include/cef_resource_handler.h"

#include <atomic>
#include <cstdint>
#include <memory>
#include <string>

namespace Framework::GUI::Resources {
    // Serves one response for one resource URL.
    //
    // Every method of a CefResourceHandler runs on the CEF IO thread, which
    // dispatches loads for every browser in the process and must never block.
    // So this class implements the Open/Skip/Read trio rather than the
    // deprecated ProcessRequest/ReadResponse pair: only the newer three can
    // defer, and each of them hands the actual disk access to a file-thread
    // task and returns immediately. CEF guarantees the |dataOut| buffer stays
    // valid until the read callback fires, which is what makes filling it from
    // another thread safe.
    class ResourceHandler final: public CefResourceHandler {
      private:
        std::shared_ptr<ResourceProvider> _provider;
        std::string _host;
        std::string _path;

        // Non-zero when the request never reaches a provider: an unclaimed host
        // or a path that could only be an escape attempt. Answering it with a
        // status is what keeps a bad URL a 404 instead of a scheme-level load
        // failure with no explanation in the network panel.
        int _rejectStatus = 0;
        std::string _rejectMessage;

        int _status = 200;
        std::string _mimeType;
        ResourceStat _stat;
        std::unique_ptr<ResourceStream> _stream;

        std::atomic<bool> _cancelled {false};

        // Replaces the body with bytes held here and gives it a status.
        void ServeInline(int status, std::string body);

        void OpenOnFileThread(CefRefPtr<CefCallback> callback);
        void ReadOnFileThread(void *dataOut, int bytesToRead, CefRefPtr<CefResourceReadCallback> callback);
        void SkipOnFileThread(std::int64_t bytesToSkip, CefRefPtr<CefResourceSkipCallback> callback);

      public:
        ResourceHandler(std::shared_ptr<ResourceProvider> provider, std::string host, std::string path);

        // Answers |status| without consulting any provider.
        ResourceHandler(int status, std::string message, std::string host, std::string path);

        bool Open(CefRefPtr<CefRequest> request, bool &handleRequest, CefRefPtr<CefCallback> callback) override;
        void GetResponseHeaders(CefRefPtr<CefResponse> response, std::int64_t &responseLength, CefString &redirectUrl) override;
        bool Skip(std::int64_t bytesToSkip, std::int64_t &bytesSkipped, CefRefPtr<CefResourceSkipCallback> callback) override;
        bool Read(void *dataOut, int bytesToRead, int &bytesRead, CefRefPtr<CefResourceReadCallback> callback) override;
        void Cancel() override;

        IMPLEMENT_REFCOUNTING(ResourceHandler);
    };
} // namespace Framework::GUI::Resources
