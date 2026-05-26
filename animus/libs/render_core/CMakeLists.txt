find_package(OpenGL REQUIRED)
find_package(glfw3 REQUIRED)
find_package(GLEW REQUIRED)

add_library(animus_render_core
    src/gl_info.cpp
    src/mesh.cpp
    src/render_stats.cpp
    src/shader_program.cpp
    src/texture.cpp
    src/window.cpp
)

target_include_directories(animus_render_core
    PUBLIC
        include
)

target_link_libraries(animus_render_core
    PUBLIC
        OpenGL::GL
        glfw
        GLEW::GLEW
)

animus_enable_warnings(animus_render_core)
