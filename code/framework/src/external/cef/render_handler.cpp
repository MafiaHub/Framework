/*
 * MafiaHub OSS license
 * Copyright (c) 2022, MafiaHub. All rights reserved.
 *
 * This file comes from MafiaHub, hosted at https://github.com/MafiaHub/Framework.
 * See LICENSE file in the source repository for information regarding licensing.
 */

#include "render_handler.h"

namespace Framework::External::CEF {
    RenderHandler::RenderHandler(Client *client): _owner(client), _isPaintingPopup(false) {}

    RenderHandler::~RenderHandler() {}

    void RenderHandler::UpdatePopup() {
        if (_isPaintingPopup) {
            return;
        }

        if (_popupRect.IsEmpty()) {
            return;
        }

        _isPaintingPopup = true;
        _owner->GetBrowser()->GetHost()->Invalidate(PET_POPUP);
        _isPaintingPopup = false;
    }

    void RenderHandler::GetViewRect(CefRefPtr<CefBrowser> browser, CefRect &rect) {}

    void RenderHandler::OnPaint(CefRefPtr<CefBrowser> browser, PaintElementType type, const RectList &dirtyRects, const void *buffer, int width, int height) {}

    void RenderHandler::OnImeCompositionRangeChanged(CefRefPtr<CefBrowser> browser, const CefRange &selectedRange, const RectList &characterBounds) {}

    void RenderHandler::OnPopupShow(CefRefPtr<CefBrowser> browser, bool show) {}

    void RenderHandler::OnPopupSize(CefRefPtr<CefBrowser> browser, const CefRect &rect) {}
} // namespace Framework::External::CEF
