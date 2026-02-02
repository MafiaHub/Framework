add_library(ImGuizmo STATIC
    ImGuizmo.cpp
)

target_include_directories(ImGuizmo PUBLIC ".")
target_link_libraries(ImGuizmo DearImGUI)
