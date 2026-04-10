#include "Window.h"
#include <GLFW/glfw3.h>
#include <stdexcept>

namespace Aero {

    Window::Window(const Specs& specs) : _width(specs.width), _height(specs.height) {
        if (!glfwInit()) {
            throw std::runtime_error("Failed to init GLFW");
        }

        glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
        glfwWindowHint(GLFW_RESIZABLE, GLFW_TRUE);

        _window = glfwCreateWindow(_width, _height, specs.title.c_str(), nullptr, nullptr);
        if (!_window) {
            throw std::runtime_error("Failed to create GLFW window");
        }

        // 初始化时间基准
        _lastFrameTime = static_cast<float>(glfwGetTime());
    }

    Window::~Window() {
        glfwDestroyWindow(_window);
        glfwTerminate();
    }

    bool Window::should_close() const {
        return glfwWindowShouldClose(_window);
    }

    void Window::poll_events() {
        glfwPollEvents();

        // 1. 自动计算 DeltaTime
        float currentFrameTime = static_cast<float>(glfwGetTime());
        _deltaTime = currentFrameTime - _lastFrameTime;
        _lastFrameTime = currentFrameTime;

        // 2. 自动计算鼠标位移 (Mouse Delta)
        double xpos, ypos;
        glfwGetCursorPos(_window, &xpos, &ypos);
        glm::vec2 currentMousePos = { static_cast<float>(xpos), static_cast<float>(ypos) };

        if (_firstMouse) {
            _lastMousePos = currentMousePos;
            _firstMouse = false;
        }

        _mouseDelta = currentMousePos - _lastMousePos;
        _lastMousePos = currentMousePos;
    }

    float Window::get_current_time() const {
        return static_cast<float>(glfwGetTime());
    }

    void Window::get_framebuffer_size(int* w, int* h) const {
        glfwGetFramebufferSize(_window, w, h);
    }

    bool Window::is_key_down(int key) const {
        return glfwGetKey(_window, key) == GLFW_PRESS;
    }

    bool Window::is_mouse_button_down(int button) const {
        return glfwGetMouseButton(_window, button) == GLFW_PRESS;
    }

    glm::vec2 Window::get_mouse_pos() const {
        return _lastMousePos;
    }

    void Window::set_cursor_mode(bool locked) {
        glfwSetInputMode(_window, GLFW_CURSOR, locked ? GLFW_CURSOR_DISABLED : GLFW_CURSOR_NORMAL);

        // 释放鼠标时，重置 _firstMouse 标志，防止下次右键点击时视角瞬移
        if (!locked) {
            _firstMouse = true;
        }
    }

} // namespace Aero