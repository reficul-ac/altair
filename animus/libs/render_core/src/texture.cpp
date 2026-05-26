#include "animus/render_core/texture.hpp"

#include <GL/glew.h>

#include <stdexcept>
#include <utility>

namespace animus::render_core {
namespace {

void configure_tile_texture()
{
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
}

} // namespace

Texture2D::Texture2D()
{
    glGenTextures(1, &texture_);
}

Texture2D::~Texture2D()
{
    if (texture_ != 0) {
        glDeleteTextures(1, &texture_);
    }
}

Texture2D::Texture2D(Texture2D&& other) noexcept
    : texture_(std::exchange(other.texture_, 0)),
      width_(std::exchange(other.width_, 0)),
      height_(std::exchange(other.height_, 0))
{
}

Texture2D& Texture2D::operator=(Texture2D&& other) noexcept
{
    if (this != &other) {
        if (texture_ != 0) {
            glDeleteTextures(1, &texture_);
        }
        texture_ = std::exchange(other.texture_, 0);
        width_ = std::exchange(other.width_, 0);
        height_ = std::exchange(other.height_, 0);
    }
    return *this;
}

void Texture2D::upload_rgba8(int width, int height, std::span<const std::uint8_t> pixels)
{
    if (width <= 0 || height <= 0 ||
        pixels.size() != static_cast<std::size_t>(width * height * 4)) {
        throw std::invalid_argument("RGBA8 texture upload size does not match dimensions");
    }

    glBindTexture(GL_TEXTURE_2D, texture_);
    configure_tile_texture();
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    glTexImage2D(
        GL_TEXTURE_2D,
        0,
        GL_RGBA8,
        width,
        height,
        0,
        GL_RGBA,
        GL_UNSIGNED_BYTE,
        pixels.data());
    width_ = width;
    height_ = height;
}

void Texture2D::upload_r32f(int width, int height, std::span<const float> values)
{
    if (width <= 0 || height <= 0 ||
        values.size() != static_cast<std::size_t>(width * height)) {
        throw std::invalid_argument("R32F texture upload size does not match dimensions");
    }

    glBindTexture(GL_TEXTURE_2D, texture_);
    configure_tile_texture();
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    glTexImage2D(
        GL_TEXTURE_2D,
        0,
        GL_R32F,
        width,
        height,
        0,
        GL_RED,
        GL_FLOAT,
        values.data());
    width_ = width;
    height_ = height;
}

void Texture2D::bind_to_unit(unsigned int unit) const
{
    glActiveTexture(GL_TEXTURE0 + unit);
    glBindTexture(GL_TEXTURE_2D, texture_);
}

unsigned int Texture2D::id() const
{
    return texture_;
}

int Texture2D::width() const
{
    return width_;
}

int Texture2D::height() const
{
    return height_;
}

} // namespace animus::render_core
