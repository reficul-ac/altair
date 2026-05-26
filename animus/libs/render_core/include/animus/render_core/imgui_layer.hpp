#pragma once

struct GLFWwindow;

namespace animus::render_core
{

class ImGuiLayer
{
  public:
    explicit ImGuiLayer(GLFWwindow *window);
    ~ImGuiLayer();

    ImGuiLayer(const ImGuiLayer &) = delete;
    ImGuiLayer &operator=(const ImGuiLayer &) = delete;
    ImGuiLayer(ImGuiLayer &&) = delete;
    ImGuiLayer &operator=(ImGuiLayer &&) = delete;

    void begin_frame();
    void end_frame();
    [[nodiscard]] bool wants_mouse_capture() const;
    [[nodiscard]] bool wants_keyboard_capture() const;
};

} // namespace animus::render_core
