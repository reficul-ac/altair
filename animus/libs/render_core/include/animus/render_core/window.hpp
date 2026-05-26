#pragma once

#include <string_view>

struct GLFWwindow;

namespace animus::render_core {

struct WindowConfig {
    int width = 1280;
    int height = 720;
    std::string_view title = "terrain_lab";
    bool visible = true;
};

class GlfwWindow {
public:
    explicit GlfwWindow(const WindowConfig& config);
    ~GlfwWindow();

    GlfwWindow(const GlfwWindow&) = delete;
    GlfwWindow& operator=(const GlfwWindow&) = delete;
    GlfwWindow(GlfwWindow&& other) noexcept;
    GlfwWindow& operator=(GlfwWindow&& other) noexcept;

    void make_current() const;
    void swap_buffers() const;
    void poll_events() const;
    void request_close();

    [[nodiscard]] bool should_close() const;
    [[nodiscard]] bool escape_pressed() const;
    [[nodiscard]] int framebuffer_width() const;
    [[nodiscard]] int framebuffer_height() const;
    [[nodiscard]] GLFWwindow* native_handle() const;

private:
    GLFWwindow* window_ = nullptr;
    bool owns_glfw_ = false;
};

void initialize_glew();

} // namespace animus::render_core
