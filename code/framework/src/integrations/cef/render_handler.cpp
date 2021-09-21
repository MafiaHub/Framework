#include "render_handler.h"

namespace Framework::Integrations::CEF {
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
} // namespace Framework::Integrations::CEF