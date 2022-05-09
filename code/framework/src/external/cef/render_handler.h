/*
 * MafiaHub OSS license
 * Copyright (c) 2022, MafiaHub. All rights reserved.
 *
 * This file comes from MafiaHub, hosted at https://github.com/MafiaHub/Framework.
 * See LICENSE file in the source repository for information regarding licensing.
 */

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
        explicit RenderHandler(Client *);

        ~RenderHandler() override;

        void UpdatePopup();

      protected:
        void GetViewRect(CefRefPtr<CefBrowser>, CefRect &) override;
        void OnPaint(CefRefPtr<CefBrowser>, PaintElementType, const RectList &, const void *, int, int) override;
        void OnPopupShow(CefRefPtr<CefBrowser>, bool) override;
        void OnPopupSize(CefRefPtr<CefBrowser>, const CefRect &) override;
        void OnImeCompositionRangeChanged(CefRefPtr<CefBrowser>, const CefRange &, const RectList &) override;

        IMPLEMENT_REFCOUNTING(RenderHandler);
    };
}; // namespace Framework::External::CEF
