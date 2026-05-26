#pragma once

#include <cstddef>
#include <cstdint>
#include <span>

namespace animus::render_core
{

class Texture2D
{
  public:
    Texture2D();
    ~Texture2D();

    Texture2D(const Texture2D &) = delete;
    Texture2D &operator=(const Texture2D &) = delete;
    Texture2D(Texture2D &&other) noexcept;
    Texture2D &operator=(Texture2D &&other) noexcept;

    void upload_rgba8(int width, int height, std::span<const std::uint8_t> pixels);
    void upload_r32f(int width, int height, std::span<const float> values);
    void bind_to_unit(unsigned int unit) const;

    [[nodiscard]] unsigned int id() const;
    [[nodiscard]] int width() const;
    [[nodiscard]] int height() const;

  private:
    unsigned int texture_ = 0;
    int width_ = 0;
    int height_ = 0;
};

} // namespace animus::render_core
