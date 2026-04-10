// src/Core/Window.h
#pragma once
#include <string>
#include <glm/glm.hpp>

struct GLFWwindow;

namespace Aero {

    class Window {
    public:
        struct Specs {
            uint32_t width{ 1280 };
            uint32_t height{ 720 };
            std::string title{ "AeroEngine" };
        };

        Window(const Specs& specs);
        ~Window();

        bool should_close() const;
        void poll_events();

        float get_delta_time() const { return _deltaTime; }
        float get_current_time() const;

        uint32_t width() const { return _width; }
        uint32_t height() const { return _height; }
        void get_framebuffer_size(int* w, int* h) const;

        bool is_key_down(int key) const;
        bool is_mouse_button_down(int button) const;
        glm::vec2 get_mouse_pos() const;
        glm::vec2 get_mouse_delta() const { return _mouseDelta; }
        void set_cursor_mode(bool locked);

        // 暴露句柄供 RHI 和 ImGui 使用
        GLFWwindow* handle() const { return _window; }

    private:
        static void framebuffer_size_callback(GLFWwindow* window, int width, int height);

        GLFWwindow* _window{ nullptr };
        uint32_t _width, _height;

        float _lastFrameTime{ 0.0f };
        float _deltaTime{ 0.0f };

        glm::vec2 _lastMousePos{ 0.0f };
        glm::vec2 _mouseDelta{ 0.0f };
        bool _firstMouse{ true };
    };

}