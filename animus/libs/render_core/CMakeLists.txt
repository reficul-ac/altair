find_package(OpenGL REQUIRED)
find_package(glfw3 REQUIRED)
find_package(GLEW REQUIRED)

add_library(
    animus_imgui
    ../../third_party/imgui/imgui.cpp
    ../../third_party/imgui/imgui_draw.cpp
    ../../third_party/imgui/imgui_tables.cpp
    ../../third_party/imgui/imgui_widgets.cpp
    ../../third_party/imgui/backends/imgui_impl_glfw.cpp
    ../../third_party/imgui/backends/imgui_impl_opengl3.cpp
)

target_include_directories(
    animus_imgui SYSTEM PUBLIC ../../third_party/imgui ../../third_party/imgui/backends
)

target_link_libraries(animus_imgui PUBLIC OpenGL::GL glfw)

add_library(
    animus_render_core
    src/gl_info.cpp
    src/imgui_layer.cpp
    src/mesh.cpp
    src/render_stats.cpp
    src/shader_program.cpp
    src/texture.cpp
    src/window.cpp
)

target_include_directories(animus_render_core PUBLIC include)

target_link_libraries(animus_render_core PUBLIC OpenGL::GL glfw GLEW::GLEW animus_imgui)

animus_enable_warnings(animus_render_core)
