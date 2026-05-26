#include "animus/render_core/window.hpp"

#include <GL/glew.h>
#include <GLFW/glfw3.h>

#include <iostream>
#include <stdexcept>
#include <string>
#include <utility>

namespace animus::render_core {
namespace {

void glfw_error_callback(int code, const char* description)
{
    const std::string message = description == nullptr ? "unknown GLFW error" : description;
    std::cerr << "GLFW error " << code << ": " << message << '\n';
}

} // namespace

GlfwWindow::GlfwWindow(const WindowConfig& config)
{
    if (config.width <= 0 || config.height <= 0) {
        throw std::invalid_argument("Window dimensions must be positive");
    }

    glfwSetErrorCallback(glfw_error_callback);
    if (glfwInit() != GLFW_TRUE) {
        throw std::runtime_error("glfwInit failed");
    }
    owns_glfw_ = true;

    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
    glfwWindowHint(GLFW_OPENGL_FORWARD_COMPAT, GLFW_TRUE);
    glfwWindowHint(GLFW_VISIBLE, config.visible ? GLFW_TRUE : GLFW_FALSE);
#ifndef NDEBUG
    glfwWindowHint(GLFW_OPENGL_DEBUG_CONTEXT, GLFW_TRUE);
#endif

    window_ = glfwCreateWindow(
        config.width,
        config.height,
        std::string(config.title).c_str(),
        nullptr,
        nullptr);
    if (window_ == nullptr) {
        glfwTerminate();
        owns_glfw_ = false;
        throw std::runtime_error("Failed to create GLFW OpenGL window");
    }
}

GlfwWindow::~GlfwWindow()
{
    if (window_ != nullptr) {
        glfwDestroyWindow(window_);
    }
    if (owns_glfw_) {
        glfwTerminate();
    }
}

GlfwWindow::GlfwWindow(GlfwWindow&& other) noexcept
    : window_(std::exchange(other.window_, nullptr)),
      owns_glfw_(std::exchange(other.owns_glfw_, false))
{
}

GlfwWindow& GlfwWindow::operator=(GlfwWindow&& other) noexcept
{
    if (this != &other) {
        if (window_ != nullptr) {
            glfwDestroyWindow(window_);
        }
        if (owns_glfw_) {
            glfwTerminate();
        }
        window_ = std::exchange(other.window_, nullptr);
        owns_glfw_ = std::exchange(other.owns_glfw_, false);
    }
    return *this;
}

void GlfwWindow::make_current() const
{
    glfwMakeContextCurrent(window_);
}

void GlfwWindow::swap_buffers() const
{
    glfwSwapBuffers(window_);
}

void GlfwWindow::poll_events() const
{
    glfwPollEvents();
}

void GlfwWindow::request_close()
{
    glfwSetWindowShouldClose(window_, GLFW_TRUE);
}

bool GlfwWindow::should_close() const
{
    return glfwWindowShouldClose(window_) == GLFW_TRUE;
}

bool GlfwWindow::escape_pressed() const
{
    return glfwGetKey(window_, GLFW_KEY_ESCAPE) == GLFW_PRESS;
}

int GlfwWindow::framebuffer_width() const
{
    int width = 0;
    int height = 0;
    glfwGetFramebufferSize(window_, &width, &height);
    return width;
}

int GlfwWindow::framebuffer_height() const
{
    int width = 0;
    int height = 0;
    glfwGetFramebufferSize(window_, &width, &height);
    return height;
}

GLFWwindow* GlfwWindow::native_handle() const
{
    return window_;
}

void initialize_glew()
{
    glewExperimental = GL_TRUE;
    const GLenum status = glewInit();
    glGetError();
    if (status != GLEW_OK) {
        throw std::runtime_error(
            std::string("glewInit failed: ") +
            reinterpret_cast<const char*>(glewGetErrorString(status)));
    }
}

} // namespace animus::render_core
