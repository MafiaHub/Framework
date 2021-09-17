#pragma once

#include <include/cef_scheme.h>
#include <regex>
#include <vector>

namespace Framework::Integrations::CEF {
    class SchemeHandlerFactory: public CefSchemeHandlerFactory {
      private:
        std::vector<std::regex> _requestsBlacklist;

      public:
        virtual CefRefPtr<CefResourceHandler> Create(CefRefPtr<CefBrowser>, CefRefPtr<CefFrame>, const CefString &,
                                                     CefRefPtr<CefRequest>);

        void SetRequestBlacklist(const std::vector<std::regex> &);

        IMPLEMENT_REFCOUNTING(SchemeHandlerFactory);
    };
} // namespace Framework::Integrations::CEF