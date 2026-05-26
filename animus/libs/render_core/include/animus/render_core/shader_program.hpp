#pragma once

#include <string_view>

namespace animus::render_core
{

class ShaderProgram
{
  public:
    ShaderProgram(std::string_view vertex_source, std::string_view fragment_source);
    ~ShaderProgram();

    ShaderProgram(const ShaderProgram &) = delete;
    ShaderProgram &operator=(const ShaderProgram &) = delete;
    ShaderProgram(ShaderProgram &&other) noexcept;
    ShaderProgram &operator=(ShaderProgram &&other) noexcept;

    void use() const;
    [[nodiscard]] unsigned int id() const;

  private:
    unsigned int program_ = 0;
};

} // namespace animus::render_core
