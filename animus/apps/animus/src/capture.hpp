#pragma once

#include <filesystem>

namespace animus::app
{

void write_ppm_capture(const std::filesystem::path &path, int width, int height);
void write_png_capture(const std::filesystem::path &path, int width, int height);
void encode_mp4_from_png_sequence(const std::filesystem::path &sequence_dir,
                                  int fps,
                                  const std::filesystem::path &output_path);

} // namespace animus::app
