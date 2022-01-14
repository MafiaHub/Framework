/*
 * MafiaHub OSS license
 * Copyright (c) 2022, MafiaHub. All rights reserved.
 *
 * This file comes from MafiaHub, hosted at https://github.com/MafiaHub/Framework.
 * See LICENSE file in the source repository for information regarding licensing.
 */

#pragma once

#include <include/cef_scheme.h>
#include <regex>
#include <vector>

namespace Framework::External::CEF {
    class SchemeHandlerFactory: public CefSchemeHandlerFactory {
      private:
        std::vector<std::regex> _requestsBlacklist;

      public:
        virtual CefRefPtr<CefResourceHandler> Create(CefRefPtr<CefBrowser>, CefRefPtr<CefFrame>, const CefString &, CefRefPtr<CefRequest>);

        void SetRequestBlacklist(const std::vector<std::regex> &);

        IMPLEMENT_REFCOUNTING(SchemeHandlerFactory);
    };
} // namespace Framework::External::CEF
