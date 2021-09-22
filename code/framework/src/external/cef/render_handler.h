#pragma once

#include "client.h"

#include <include/cef_render_handler.h>

namespace Framework::External::CEF {
    class RenderHandler: public CefRenderHandler {
      private:
        Client *_owner;
        bool _isPaintingPopup;

        CefRect _popupRect;

      public:
        RenderHandler(Client *);

        virtual ~RenderHandler();

        void UpdatePopup();

      protected:
        virtual void GetViewRect(CefRefPtr<CefBrowser>, CefRect &) override;
        virtual void OnPaint(CefRefPtr<CefBrowser>, PaintElementType, const RectList &, const void *, int, int) override;
        virtual void OnPopupShow(CefRefPtr<CefBrowser>, bool) override;
        virtual void OnPopupSize(CefRefPtr<CefBrowser>, const CefRect &) override;
        virtual void OnImeCompositionRangeChanged(CefRefPtr<CefBrowser>, const CefRange &, const RectList &) override;

        IMPLEMENT_REFCOUNTING(RenderHandler);
    };
}; // namespace Framework::External::CEF