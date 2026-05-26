#pragma once

#include <string>

namespace animus::render_core
{

struct GlInfo
{
    std::string vendor;
    std::string renderer;
    std::string version;
    std::string shading_language_version;
};

GlInfo query_gl_info();
std::string format_gl_info(const GlInfo &info);
void enable_debug_callback();

} // namespace animus::render_core
