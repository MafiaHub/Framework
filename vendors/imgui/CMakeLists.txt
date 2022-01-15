add_library(DearImGUI STATIC
    imgui.cpp
    imgui_draw.cpp
    imgui_tables.cpp
    imgui_widgets.cpp

    # backends
    backends/imgui_impl_dx9.cpp
    backends/imgui_impl_dx11.cpp
    backends/imgui_impl_dx12.cpp
    backends/imgui_impl_sdl.cpp
    backends/imgui_impl_win32.cpp
)

target_include_directories(DearImGUI PUBLIC ".")
