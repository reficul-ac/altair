#include "animus/render_core/shader_program.hpp"

#include <GL/glew.h>

#include <stdexcept>
#include <string>
#include <utility>

namespace animus::render_core
{
namespace
{

std::string shader_log(GLuint shader)
{
    GLint length = 0;
    glGetShaderiv(shader, GL_INFO_LOG_LENGTH, &length);
    if (length <= 1)
    {
        return {};
    }

    std::string log(static_cast<std::size_t>(length), '\0');
    GLsizei written = 0;
    glGetShaderInfoLog(shader, length, &written, log.data());
    log.resize(static_cast<std::size_t>(written));
    return log;
}

std::string program_log(GLuint program)
{
    GLint length = 0;
    glGetProgramiv(program, GL_INFO_LOG_LENGTH, &length);
    if (length <= 1)
    {
        return {};
    }

    std::string log(static_cast<std::size_t>(length), '\0');
    GLsizei written = 0;
    glGetProgramInfoLog(program, length, &written, log.data());
    log.resize(static_cast<std::size_t>(written));
    return log;
}

GLuint compile_shader(GLenum type, std::string_view source)
{
    const GLuint shader = glCreateShader(type);
    const char *text = source.data();
    const auto length = static_cast<GLint>(source.size());
    glShaderSource(shader, 1, &text, &length);
    glCompileShader(shader);

    GLint ok = GL_FALSE;
    glGetShaderiv(shader, GL_COMPILE_STATUS, &ok);
    if (ok != GL_TRUE)
    {
        const std::string log = shader_log(shader);
        glDeleteShader(shader);
        const char *stage = type == GL_VERTEX_SHADER ? "vertex" : "fragment";
        throw std::runtime_error(std::string("OpenGL ") + stage + " shader compile failed:\n" +
                                 log);
    }

    return shader;
}

} // namespace

ShaderProgram::ShaderProgram(std::string_view vertex_source, std::string_view fragment_source)
{
    const GLuint vertex = compile_shader(GL_VERTEX_SHADER, vertex_source);
    const GLuint fragment = compile_shader(GL_FRAGMENT_SHADER, fragment_source);

    program_ = glCreateProgram();
    glAttachShader(program_, vertex);
    glAttachShader(program_, fragment);
    glLinkProgram(program_);

    glDetachShader(program_, vertex);
    glDetachShader(program_, fragment);
    glDeleteShader(vertex);
    glDeleteShader(fragment);

    GLint ok = GL_FALSE;
    glGetProgramiv(program_, GL_LINK_STATUS, &ok);
    if (ok != GL_TRUE)
    {
        const std::string log = program_log(program_);
        glDeleteProgram(program_);
        program_ = 0;
        throw std::runtime_error("OpenGL shader program link failed:\n" + log);
    }
}

ShaderProgram::~ShaderProgram()
{
    if (program_ != 0)
    {
        glDeleteProgram(program_);
    }
}

ShaderProgram::ShaderProgram(ShaderProgram &&other) noexcept
    : program_(std::exchange(other.program_, 0))
{
}

ShaderProgram &ShaderProgram::operator=(ShaderProgram &&other) noexcept
{
    if (this != &other)
    {
        if (program_ != 0)
        {
            glDeleteProgram(program_);
        }
        program_ = std::exchange(other.program_, 0);
    }
    return *this;
}

void ShaderProgram::use() const
{
    glUseProgram(program_);
}

unsigned int ShaderProgram::id() const
{
    return program_;
}

} // namespace animus::render_core
