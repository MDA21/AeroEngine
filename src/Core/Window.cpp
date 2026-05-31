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

		glfwSetWindowUserPointer(_window, this);
		glfwSetFramebufferSizeCallback(_window, framebuffer_size_callback);

		// ��ʼ��ʱ���׼
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

		// 1. �Զ����� DeltaTime
		float currentFrameTime = static_cast<float>(glfwGetTime());
		_deltaTime = currentFrameTime - _lastFrameTime;
		_lastFrameTime = currentFrameTime;

		// 2. �Զ��������λ�� (Mouse Delta)
		double xpos, ypos;
		glfwGetCursorPos(_window, &xpos, &ypos);
		glm::vec2 currentMousePos = {static_cast<float>(xpos), static_cast<float>(ypos)};

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

		if (!locked) {
			_firstMouse = true;
		}
	}

	void Window::framebuffer_size_callback(GLFWwindow* window, int width, int height) {
		auto* self = static_cast<Window*>(glfwGetWindowUserPointer(window));
		if (!self) {
			return;
		}

		self->_width = static_cast<uint32_t>(width);
		self->_height = static_cast<uint32_t>(height);
		self->_framebufferResized = true;
	}

} // namespace Aero
