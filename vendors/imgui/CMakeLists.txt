add_library(DearImGUI STATIC
    imgui.cpp
    imgui_draw.cpp
    imgui_tables.cpp
    imgui_widgets.cpp

    # backends
    backends/imgui_impl_dx9.cpp
    backends/imgui_impl_dx11.cpp
    backends/imgui_impl_dx12.cpp
    backends/imgui_impl_win32.cpp
)

target_include_directories(DearImGUI PUBLIC ".")

# The bundled DX backends import from these system libs (e.g. the DX12 backend
# calls CreateDXGIFactory1/D3D12SerializeRootSignature since ImGui 1.92). Declaring
# them on this target makes consumers link them *after* DearImGUI.lib, which the
# MSVC linker requires to resolve those imports.
if (WIN32)
    target_link_libraries(DearImGUI PUBLIC d3d9 d3d11 d3d12 dxgi)
endif ()
