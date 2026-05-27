#include "capture.hpp"

#include <GL/glew.h>
#include <png.h>

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <stdexcept>
#include <string>
#include <vector>

namespace animus::app
{
namespace
{

std::vector<std::uint8_t> read_framebuffer_rgb(int width, int height)
{
    std::vector<std::uint8_t> pixels(static_cast<std::size_t>(width * height * 3));
    glPixelStorei(GL_PACK_ALIGNMENT, 1);
    glReadPixels(0, 0, width, height, GL_RGB, GL_UNSIGNED_BYTE, pixels.data());
    return pixels;
}

void ensure_parent_directory(const std::filesystem::path &path)
{
    if (path.has_parent_path())
    {
        std::filesystem::create_directories(path.parent_path());
    }
}

std::string shell_quote(const std::filesystem::path &path)
{
    std::string quoted = "'";
    for (const char value : path.string())
    {
        if (value == '\'')
        {
            quoted += "'\\''";
        }
        else
        {
            quoted += value;
        }
    }
    quoted += "'";
    return quoted;
}

} // namespace

void write_ppm_capture(const std::filesystem::path &path, int width, int height)
{
    const std::vector<std::uint8_t> pixels = read_framebuffer_rgb(width, height);

    ensure_parent_directory(path);
    std::ofstream out(path, std::ios::binary);
    if (!out)
    {
        throw std::runtime_error("Failed to open capture path: " + path.string());
    }
    out << "P6\n" << width << ' ' << height << "\n255\n";
    for (int row = height - 1; row >= 0; --row)
    {
        const auto offset = static_cast<std::size_t>(row * width * 3);
        out.write(reinterpret_cast<const char *>(pixels.data() + offset), width * 3);
    }
}

void write_png_capture(const std::filesystem::path &path, int width, int height)
{
    const std::vector<std::uint8_t> pixels = read_framebuffer_rgb(width, height);

    ensure_parent_directory(path);
    FILE *file = std::fopen(path.string().c_str(), "wb");
    if (file == nullptr)
    {
        throw std::runtime_error("Failed to open PNG capture path: " + path.string());
    }

    png_structp png = png_create_write_struct(PNG_LIBPNG_VER_STRING, nullptr, nullptr, nullptr);
    if (png == nullptr)
    {
        std::fclose(file);
        throw std::runtime_error("png_create_write_struct failed");
    }
    png_infop info = png_create_info_struct(png);
    if (info == nullptr)
    {
        png_destroy_write_struct(&png, nullptr);
        std::fclose(file);
        throw std::runtime_error("png_create_info_struct failed");
    }
    if (setjmp(png_jmpbuf(png)) != 0)
    {
        png_destroy_write_struct(&png, &info);
        std::fclose(file);
        throw std::runtime_error("Failed to encode PNG capture: " + path.string());
    }

    png_init_io(png, file);
    png_set_IHDR(png,
                 info,
                 static_cast<png_uint_32>(width),
                 static_cast<png_uint_32>(height),
                 8,
                 PNG_COLOR_TYPE_RGB,
                 PNG_INTERLACE_NONE,
                 PNG_COMPRESSION_TYPE_DEFAULT,
                 PNG_FILTER_TYPE_DEFAULT);
    png_write_info(png, info);

    std::vector<png_bytep> rows(static_cast<std::size_t>(height));
    for (int row = 0; row < height; ++row)
    {
        const int source_row = height - 1 - row;
        rows[static_cast<std::size_t>(row)] =
            const_cast<png_bytep>(pixels.data() + static_cast<std::size_t>(source_row * width * 3));
    }
    png_write_image(png, rows.data());
    png_write_end(png, nullptr);
    png_destroy_write_struct(&png, &info);
    std::fclose(file);
}

void encode_mp4_from_png_sequence(const std::filesystem::path &sequence_dir,
                                  int fps,
                                  const std::filesystem::path &output_path)
{
    if (fps <= 0)
    {
        throw std::runtime_error("MP4 export FPS must be positive");
    }
    if (!std::filesystem::exists(sequence_dir / "frame_000000.png"))
    {
        throw std::runtime_error("MP4 export has no recorded PNG frames: " + sequence_dir.string());
    }

    ensure_parent_directory(output_path);
    const std::filesystem::path input_pattern = sequence_dir / "frame_%06d.png";
    const std::string command = "ffmpeg -y -hide_banner -loglevel error -framerate " +
                                std::to_string(fps) + " -i " + shell_quote(input_pattern) +
                                " -c:v libx264 -pix_fmt yuv420p -crf 18 " +
                                shell_quote(output_path);
    const int result = std::system(command.c_str());
    if (result != 0)
    {
        throw std::runtime_error("ffmpeg MP4 export failed");
    }
    if (!std::filesystem::exists(output_path) || std::filesystem::file_size(output_path) == 0U)
    {
        throw std::runtime_error("ffmpeg MP4 export wrote an empty file: " + output_path.string());
    }
}

} // namespace animus::app
