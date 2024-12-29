#include "view_d3d11.h"
#include "logging/logger.h"

// DirectX
#include <d3d11.h>
#include <d3dcompiler.h>
#include <stdio.h>
#ifdef _MSC_VER
#pragma comment(lib, "d3dcompiler") // Automatically link with d3dcompiler.lib as we are using D3DCompile() below.
#endif

#include <unordered_map>

struct CursorData {
    void *pixels;
    int w, h;
};

struct D3D11WebData {
    ID3D11Buffer *pVB {};
    ID3D11Buffer *pIB {};
    ID3D11VertexShader *pVertexShader {};
    ID3D11InputLayout *pInputLayout {};
    ID3D11PixelShader *pPixelShader {};
    ID3D11SamplerState *pSampler {};
    ID3D11RasterizerState *pRasterizerState {};
    ID3D11BlendState *pBlendState {};
    ID3D11DepthStencilState *pDepthStencilState {};
    std::unordered_map<ultralight::Cursor, CursorData> _cursors;
};

static D3D11WebData *bd = new D3D11WebData;

struct VERTEX {
    float x, y;
    float u, v;
};

namespace Framework::GUI {
    ViewD3D11::ViewD3D11(ultralight::RefPtr<ultralight::Renderer> renderer, Graphics::Renderer *graphicsRenderer): View(renderer, graphicsRenderer) {
        _sdk = new SDK;
    }

    bool ViewD3D11::Init(std::string &path, int width, int height) {
        // Load default cursor
        LoadCursorData(ultralight::kCursor_Pointer);

        return View::Init(path, width, height);
    }

    void ViewD3D11::InitD3D() {
        // No need to initialize twice
        if (_d3dInitialized) {
            return;
        }

        // Acquire the rendering devices from the application
        const auto device    = _graphicsRenderer->GetD3D11Backend()->GetDevice();
        const auto renderCtx = _graphicsRenderer->GetD3D11Backend()->GetContext();
        if (!device || !renderCtx) {
            return;
        }

        {
            static const char *vertexShader = "struct VS_INPUT\
            {\
              float2 pos : POSITION;\
              float2 uv  : TEXCOORD0;\
            };\
            \
            struct PS_INPUT\
            {\
              float4 pos : SV_POSITION;\
              float2 uv  : TEXCOORD0;\
            };\
            \
            PS_INPUT main(VS_INPUT input)\
            {\
              PS_INPUT output;\
              output.pos = float4(input.pos.xy, 0.f, 1.f);\
              output.uv  = input.uv;\
              return output;\
            }";

            ID3DBlob *vertexShaderBlob;
            if (FAILED(D3DCompile(vertexShader, strlen(vertexShader), NULL, NULL, NULL, "main", "vs_4_0", 0, 0, &vertexShaderBlob, NULL)))
                return; // NB: Pass ID3DBlob* pErrorBlob to D3DCompile() to get error showing in (const char*)pErrorBlob->GetBufferPointer(). Make sure to Release() the blob!
            if (device->CreateVertexShader(vertexShaderBlob->GetBufferPointer(), vertexShaderBlob->GetBufferSize(), NULL, &bd->pVertexShader) != S_OK) {
                vertexShaderBlob->Release();
                return;
            }

            // Create the input layout
            D3D11_INPUT_ELEMENT_DESC local_layout[] = {
                {"POSITION", 0, DXGI_FORMAT_R32G32_FLOAT, 0, (UINT)0, D3D11_INPUT_PER_VERTEX_DATA, 0},
                {"TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT, 0, (UINT)8, D3D11_INPUT_PER_VERTEX_DATA, 0},
            };
            if (device->CreateInputLayout(local_layout, 2, vertexShaderBlob->GetBufferPointer(), vertexShaderBlob->GetBufferSize(), &bd->pInputLayout) != S_OK) {
                vertexShaderBlob->Release();
                return;
            }
            vertexShaderBlob->Release();
        }

        // Create the pixel shader
        {
            static const char *pixelShader = "struct PS_INPUT\
            {\
            float4 pos : SV_POSITION;\
            float2 uv  : TEXCOORD0;\
            };\
            sampler sampler0;\
            Texture2D texture0;\
            \
            float4 main(PS_INPUT input) : SV_Target\
            {\
            float4 out_col = texture0.Sample(sampler0, input.uv); \
            return out_col; \
            }";

            ID3DBlob *pixelShaderBlob;
            if (FAILED(D3DCompile(pixelShader, strlen(pixelShader), NULL, NULL, NULL, "main", "ps_4_0", 0, 0, &pixelShaderBlob, NULL)))
                return; // NB: Pass ID3DBlob* pErrorBlob to D3DCompile() to get error showing in (const char*)pErrorBlob->GetBufferPointer(). Make sure to Release() the blob!
            if (device->CreatePixelShader(pixelShaderBlob->GetBufferPointer(), pixelShaderBlob->GetBufferSize(), NULL, &bd->pPixelShader) != S_OK) {
                pixelShaderBlob->Release();
                return;
            }
            pixelShaderBlob->Release();
        }

        // Create the blending setup
        {
            D3D11_BLEND_DESC desc;
            ZeroMemory(&desc, sizeof(desc));
            desc.AlphaToCoverageEnable                 = false;
            desc.RenderTarget[0].BlendEnable           = true;
            desc.RenderTarget[0].SrcBlend              = D3D11_BLEND_SRC_ALPHA;
            desc.RenderTarget[0].DestBlend             = D3D11_BLEND_INV_SRC_ALPHA;
            desc.RenderTarget[0].BlendOp               = D3D11_BLEND_OP_ADD;
            desc.RenderTarget[0].SrcBlendAlpha         = D3D11_BLEND_ONE;
            desc.RenderTarget[0].DestBlendAlpha        = D3D11_BLEND_INV_SRC_ALPHA;
            desc.RenderTarget[0].BlendOpAlpha          = D3D11_BLEND_OP_ADD;
            desc.RenderTarget[0].RenderTargetWriteMask = D3D11_COLOR_WRITE_ENABLE_ALL;
            device->CreateBlendState(&desc, &bd->pBlendState);
        }

        // Create the rasterizer state
        {
            D3D11_RASTERIZER_DESC desc;
            ZeroMemory(&desc, sizeof(desc));
            desc.FillMode        = D3D11_FILL_SOLID;
            desc.CullMode        = D3D11_CULL_NONE;
            desc.ScissorEnable   = true;
            desc.DepthClipEnable = true;
            device->CreateRasterizerState(&desc, &bd->pRasterizerState);
        }

        // Create depth-stencil State
        {
            D3D11_DEPTH_STENCIL_DESC desc;
            ZeroMemory(&desc, sizeof(desc));
            desc.DepthEnable             = false;
            desc.DepthWriteMask          = D3D11_DEPTH_WRITE_MASK_ALL;
            desc.DepthFunc               = D3D11_COMPARISON_ALWAYS;
            desc.StencilEnable           = false;
            desc.FrontFace.StencilFailOp = desc.FrontFace.StencilDepthFailOp = desc.FrontFace.StencilPassOp = D3D11_STENCIL_OP_KEEP;
            desc.FrontFace.StencilFunc                                                                      = D3D11_COMPARISON_ALWAYS;
            desc.BackFace                                                                                   = desc.FrontFace;
            device->CreateDepthStencilState(&desc, &bd->pDepthStencilState);
        }

        // Create vertex/index buffers
        D3D11_BUFFER_DESC desc;
        memset(&desc, 0, sizeof(D3D11_BUFFER_DESC));
        desc.Usage          = D3D11_USAGE_DYNAMIC;
        desc.ByteWidth      = 6 * sizeof(VERTEX);
        desc.BindFlags      = D3D11_BIND_VERTEX_BUFFER;
        desc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
        desc.MiscFlags      = 0;
        if (device->CreateBuffer(&desc, NULL, &bd->pVB) < 0)
            return;

        memset(&desc, 0, sizeof(D3D11_BUFFER_DESC));
        desc.Usage          = D3D11_USAGE_DYNAMIC;
        desc.ByteWidth      = 6 * sizeof(unsigned);
        desc.BindFlags      = D3D11_BIND_INDEX_BUFFER;
        desc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
        if (device->CreateBuffer(&desc, NULL, &bd->pIB) < 0)
            return;

        // Upload vertex/index data into a single contiguous GPU buffer
        D3D11_MAPPED_SUBRESOURCE vtx_resource, idx_resource;
        if (renderCtx->Map(bd->pVB, 0, D3D11_MAP_WRITE_DISCARD, 0, &vtx_resource) != S_OK)
            return;
        if (renderCtx->Map(bd->pIB, 0, D3D11_MAP_WRITE_DISCARD, 0, &idx_resource) != S_OK)
            return;

        static VERTEX vertices[] = {
            // First triangle
            {-1.0f, -1.0f, 0.0f, 1.0f}, // Bottom left
            {1.0f, -1.0f, 1.0f, 1.0f},  // Bottom right
            {-1.0f, 1.0f, 0.0f, 0.0f},  // Top left

            // Second triangle
            {1.0f, -1.0f, 1.0f, 1.0f}, // Bottom right
            {1.0f, 1.0f, 1.0f, 0.0f},  // Top right
            {-1.0f, 1.0f, 0.0f, 0.0f}, // Top left
        };

        static unsigned short indices[] = {
            0, 1, 2, // Indices for the first triangle
            3, 4, 5  // Indices for the second triangle
        };

        VERTEX *vtx_dst   = (VERTEX *)vtx_resource.pData;
        unsigned *idx_dst = (unsigned *)idx_resource.pData;
        memcpy(vtx_dst, vertices, 6 * sizeof(VERTEX));
        memcpy(idx_dst, indices, 6 * sizeof(unsigned short));
        renderCtx->Unmap(bd->pVB, 0);
        renderCtx->Unmap(bd->pIB, 0);

        // Create texture sampler
        {
            D3D11_SAMPLER_DESC desc;
            ZeroMemory(&desc, sizeof(desc));
            desc.Filter         = D3D11_FILTER_MIN_MAG_MIP_LINEAR;
            desc.AddressU       = D3D11_TEXTURE_ADDRESS_WRAP;
            desc.AddressV       = D3D11_TEXTURE_ADDRESS_WRAP;
            desc.AddressW       = D3D11_TEXTURE_ADDRESS_WRAP;
            desc.MipLODBias     = 0.f;
            desc.ComparisonFunc = D3D11_COMPARISON_ALWAYS;
            desc.MinLOD         = 0.f;
            desc.MaxLOD         = 0.f;
            device->CreateSamplerState(&desc, &bd->pSampler);
        }

        _d3dInitialized = true;
    }

    void ViewD3D11::ResetTextures() {
        std::lock_guard lock(_renderMutex);

        // Grab the device & context
        const auto device    = _graphicsRenderer->GetD3D11Backend()->GetDevice();
        const auto renderCtx = _graphicsRenderer->GetD3D11Backend()->GetContext();

        if (!device || !renderCtx) {
            return;
        }

        // TODO: Destroy the texture if window size has changed
        if (!_texture) {
            D3D11_TEXTURE2D_DESC textureDesc;
            ZeroMemory(&textureDesc, sizeof(textureDesc));
            textureDesc.Width              = _width;  // width of the texture
            textureDesc.Height             = _height; // height of the texture
            textureDesc.MipLevels          = 1;
            textureDesc.ArraySize          = 1;
            textureDesc.Format             = DXGI_FORMAT_B8G8R8A8_UNORM; // texture format
            textureDesc.SampleDesc.Count   = 1;
            textureDesc.SampleDesc.Quality = 0;
            textureDesc.Usage              = D3D11_USAGE_DEFAULT;
            textureDesc.BindFlags          = D3D11_BIND_SHADER_RESOURCE; // bind to shader
            textureDesc.CPUAccessFlags     = 0;
            textureDesc.MiscFlags          = 0;

            // Step 2: Create the texture
            device->CreateTexture2D(&textureDesc, nullptr, &_texture);

            // Step 3: Create a shader-resource view
            if (FAILED(device->CreateShaderResourceView(_texture, nullptr, &_textureView))) {
                Framework::Logging::GetLogger("Web")->error("Failed to create texture view");
                return;
            }
        }

        D3D11_BOX destRegion;
        destRegion.left       = 0;
        destRegion.right      = _width;
        destRegion.top        = 0;
        destRegion.bottom     = _height;
        destRegion.front      = 0;
        destRegion.back       = 1;
        const auto rowPitch   = _width * 4;
        const auto depthPitch = rowPitch * _height;

        renderCtx->UpdateSubresource(_texture, 0, &destRegion, _pixelData, rowPitch, depthPitch);
    }

    void ViewD3D11::LoadCursorData(ultralight::Cursor cursor) {
        using namespace ultralight;

        if (bd->_cursors.find(cursor) != bd->_cursors.end())
            return;

        auto cursorId = IDC_ARROW;

        switch (cursor) {
        case kCursor_Pointer: cursorId = IDC_ARROW; break;
        case kCursor_Cross: cursorId = IDC_CROSS; break;
        case kCursor_Hand: cursorId = IDC_HAND; break;
        case kCursor_IBeam: cursorId = IDC_IBEAM; break;
        case kCursor_Wait: cursorId = IDC_WAIT; break;
        case kCursor_Help: cursorId = IDC_HELP; break;
        case kCursor_EastResize: cursorId = IDC_SIZEWE; break;
        case kCursor_NorthResize: cursorId = IDC_SIZENS; break;
        case kCursor_NorthEastResize: cursorId = IDC_SIZENESW; break;
        case kCursor_NorthWestResize: cursorId = IDC_SIZENWSE; break;
        case kCursor_SouthResize: cursorId = IDC_SIZENS; break;
        case kCursor_SouthEastResize: cursorId = IDC_SIZENWSE; break;
        case kCursor_SouthWestResize: cursorId = IDC_SIZENESW; break;
        case kCursor_WestResize: cursorId = IDC_SIZEWE; break;
        case kCursor_NorthSouthResize: cursorId = IDC_SIZENS; break;
        case kCursor_EastWestResize: cursorId = IDC_SIZEWE; break;
        case kCursor_NorthEastSouthWestResize: cursorId = IDC_SIZENESW; break;
        case kCursor_NorthWestSouthEastResize: cursorId = IDC_SIZENWSE; break;
        // The following cursors might not have direct Win32 equivalents.
        case kCursor_ColumnResize:
        case kCursor_RowResize:
        case kCursor_MiddlePanning:
        case kCursor_EastPanning:
        case kCursor_NorthPanning:
        case kCursor_NorthEastPanning:
        case kCursor_NorthWestPanning:
        case kCursor_SouthPanning:
        case kCursor_SouthEastPanning:
        case kCursor_SouthWestPanning:
        case kCursor_WestPanning:
        case kCursor_Move:
        case kCursor_VerticalText:
        case kCursor_Cell:
        case kCursor_ContextMenu:
        case kCursor_Alias:
        case kCursor_Progress:
        case kCursor_NoDrop:
        case kCursor_Copy:
        case kCursor_None:
        case kCursor_NotAllowed:
        case kCursor_ZoomIn:
        case kCursor_ZoomOut:
        case kCursor_Grab:
        case kCursor_Grabbing:
        case kCursor_Custom:
            // Use a custom cursor or a default one like IDC_ARROW
            cursorId = IDC_ARROW;
            break;
        default: cursorId = IDC_ARROW; break;
        }

        // Load the system cursor
        HCURSOR hCursor = (HCURSOR)LoadCursor(0, cursorId);
        if (hCursor == NULL) {
            DWORD dwError = GetLastError();
            printf("LoadImage failed with error %lu\n", dwError);
            if (cursor != kCursor_Pointer) {
                // Load fallback arrow cursor if we can't map the one requested.
                bd->_cursors[cursor] = bd->_cursors[kCursor_Pointer];
            }
            return;
        }

        ICONINFO iconInfo = {0};
        GetIconInfo(hCursor, &iconInfo);

        HDC hdc = GetDC(nullptr);

        BITMAP bmpColor, bmpMask;
        if (iconInfo.hbmColor)
            GetObject(iconInfo.hbmColor, sizeof(BITMAP), &bmpColor);
        if (iconInfo.hbmMask) {
            GetObject(iconInfo.hbmMask, sizeof(BITMAP), &bmpMask);
            /*For monochrome icons, the hbmMask is twice the height of the icon (with the AND mask on top and the XOR mask on the bottom), and hbmColor is NULL.
            Also, in this case the height should be an even multiple of two.*/
            bmpMask.bmHeight /= 2;
        }

        BITMAP *bmp = iconInfo.hbmColor ? &bmpColor : &bmpMask;

        // Prepare a buffer to receive the bitmap data
        BITMAPINFO biColor              = {0};
        biColor.bmiHeader.biSize        = sizeof(BITMAPINFOHEADER);
        biColor.bmiHeader.biWidth       = bmp->bmWidth;
        biColor.bmiHeader.biHeight      = -bmp->bmHeight;
        biColor.bmiHeader.biPlanes      = 1;
        biColor.bmiHeader.biBitCount    = 32;
        biColor.bmiHeader.biCompression = BI_RGB;

        // Get the actual bitmap data
        BYTE *pPixels = new BYTE[bmp->bmWidth * bmp->bmHeight * 4];
        memset(pPixels, 0x00, bmp->bmWidth * bmp->bmHeight * 4);

        if (bmp == &bmpMask) {
            char *pPixelMask = new char[bmp->bmWidth * bmp->bmHeight * 8];
            GetDIBits(hdc, iconInfo.hbmMask, 0, bmp->bmHeight, &pPixelMask[0], &biColor, DIB_RGB_COLORS);

            for (int y = 0; y < bmp->bmHeight; y++) {
                for (int x = 0; x < bmp->bmWidth; x++) {
                    int i = (y * bmp->bmWidth + x) * 4;

                    bool andMask = pPixelMask[i] != 0;
                    bool xorMask = pPixelMask[i + (bmp->bmHeight * bmp->bmWidthBytes)] != 0;

                    if (!andMask) {
                        *(DWORD *)&pPixels[i] = !!xorMask ? 0xFFFFFFFF : 0xFF000000;
                    }
                }
            }

            delete[] pPixelMask;
        }
        else {
            GetDIBits(hdc, iconInfo.hbmColor, 0, bmp->bmHeight, &pPixels[0], &biColor, DIB_RGB_COLORS);
        }

        // Now we can populate CursorData
        CursorData cursorData;
        cursorData.pixels    = pPixels;
        cursorData.w         = bmp->bmWidth;
        cursorData.h         = bmp->bmHeight;
        bd->_cursors[cursor] = cursorData;

        // Perform cleanup
        ReleaseDC(NULL, hdc);
        DeleteObject(iconInfo.hbmColor);
        DeleteObject(iconInfo.hbmMask);
        DestroyIcon(hCursor);
    }

    void ViewD3D11::Update() {
        if (!_internalView || !_shouldDisplay) {
            return;
        }

        std::lock_guard lock(_renderMutex);

        // Update the view content
        View::Update();

        // Render the cursor
        RenderCursor();
    }

    void ViewD3D11::Render() {
        if (!_internalView || !_shouldDisplay) {
            return;
        }

        std::lock_guard lock(_renderMutex);

        // Make sure we have D3D setup
        InitD3D();

        // Make sure we have textures
        if (!_pixelData) {
            return;
        }

        // Grab the device & context
        const auto device    = _graphicsRenderer->GetD3D11Backend()->GetDevice();
        const auto renderCtx = _graphicsRenderer->GetD3D11Backend()->GetContext();

        // Reset the textures
        ResetTextures();

        if (!_texture || !_textureView) {
            return;
        }

        // Apply scissor/clipping rectangle
        const D3D11_RECT r = {0, 0, _width, _height};
        renderCtx->RSSetScissorRects(1, &r);

        // Setup shader and vertex buffers
        unsigned int stride = sizeof(VERTEX);
        unsigned int offset = 0;
        renderCtx->IASetInputLayout(bd->pInputLayout);
        renderCtx->IASetVertexBuffers(0, 1, &bd->pVB, &stride, &offset);
        renderCtx->IASetIndexBuffer(bd->pIB, DXGI_FORMAT_R16_UINT, 0);
        renderCtx->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
        renderCtx->VSSetShader(bd->pVertexShader, NULL, 0);
        renderCtx->VSSetConstantBuffers(0, 0, NULL);
        renderCtx->PSSetShader(bd->pPixelShader, NULL, 0);
        renderCtx->PSSetSamplers(0, 1, &bd->pSampler);
        renderCtx->GSSetShader(NULL, NULL, 0);
        renderCtx->HSSetShader(NULL, NULL, 0); // In theory we should backup and restore this as well.. very infrequently used..
        renderCtx->DSSetShader(NULL, NULL, 0); // In theory we should backup and restore this as well.. very infrequently used..
        renderCtx->CSSetShader(NULL, NULL, 0); // In theory we should backup and restore this as well.. very infrequently used..

        // Setup blend state
        const float blend_factor[4] = {0.f, 0.f, 0.f, 0.f};
        renderCtx->OMSetBlendState(bd->pBlendState, blend_factor, 0xffffffff);
        renderCtx->OMSetDepthStencilState(bd->pDepthStencilState, 0);
        renderCtx->RSSetState(bd->pRasterizerState);
        renderCtx->PSSetShaderResources(0, 1, &_textureView);
        renderCtx->DrawIndexed(6, 0, 0);
    }

    void ViewD3D11::RenderCursor() {
        if (bd->_cursors.find(_cursor) == bd->_cursors.end())
            return;

        const auto &cursorData = bd->_cursors[_cursor];
        for (int y = 0; y < cursorData.h; y++) {
            for (int x = 0; x < cursorData.w; x++) {
                int i  = (y * cursorData.w + x) * 4;
                int bx = _cursorPos.x + x;
                int by = _cursorPos.y + y;

                if (bx >= 0 && bx < _width && by >= 0 && by < _height) {
                    int bi             = (by * _width + bx) * 4;
                    char *cursorPixels = (char *)cursorData.pixels;
                    if (cursorPixels[i + 3]) {
                        _pixelData[bi]     = cursorPixels[i + 0]; // B
                        _pixelData[bi + 1] = cursorPixels[i + 1]; // G
                        _pixelData[bi + 2] = cursorPixels[i + 2]; // R
                    }
                }
            }
        }
    }

    void ViewD3D11::OnChangeCursor(ultralight::View *caller, ultralight::Cursor cursor) {
        _cursor = cursor;

        if (bd->_cursors.find(cursor) == bd->_cursors.end()) {
            LoadCursorData(cursor);
        }
    }
} // namespace Framework::GUI
