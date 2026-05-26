#include "animus/render_core/gl_info.hpp"

#include <GL/glew.h>

#include <cstddef>
#include <iostream>
#include <sstream>
#include <string_view>

namespace animus::render_core {
namespace {

std::string gl_string(GLenum name)
{
    const auto* value = glGetString(name);
    if (value == nullptr) {
        return "unavailable";
    }
    return reinterpret_cast<const char*>(value);
}

void GLAPIENTRY debug_message_callback(
    GLenum source,
    GLenum type,
    GLuint id,
    GLenum severity,
    GLsizei length,
    const GLchar* message,
    const void* user_param)
{
    (void)source;
    (void)id;
    (void)user_param;

    std::string_view text{message, static_cast<std::size_t>(length)};
    std::cerr << "OpenGL debug";
    if (type == GL_DEBUG_TYPE_ERROR) {
        std::cerr << " error";
    }
    std::cerr << " severity=0x" << std::hex << severity << std::dec << ": " << text
              << '\n';
}

} // namespace

GlInfo query_gl_info()
{
    return GlInfo{
        gl_string(GL_VENDOR),
        gl_string(GL_RENDERER),
        gl_string(GL_VERSION),
        gl_string(GL_SHADING_LANGUAGE_VERSION),
    };
}

std::string format_gl_info(const GlInfo& info)
{
    std::ostringstream out;
    out << "OpenGL vendor: " << info.vendor << '\n'
        << "OpenGL renderer: " << info.renderer << '\n'
        << "OpenGL version: " << info.version << '\n'
        << "GLSL version: " << info.shading_language_version;
    return out.str();
}

void enable_debug_callback()
{
    if (GLEW_VERSION_4_3 || GLEW_KHR_debug) {
        glEnable(GL_DEBUG_OUTPUT);
        glEnable(GL_DEBUG_OUTPUT_SYNCHRONOUS);
        glDebugMessageCallback(debug_message_callback, nullptr);
        glDebugMessageControl(
            GL_DONT_CARE,
            GL_DONT_CARE,
            GL_DONT_CARE,
            0,
            nullptr,
            GL_TRUE);
    }
}

} // namespace animus::render_core
