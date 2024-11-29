#pragma once

#include <cstdint>

#include "camera_controller.hpp"
#include "input.hpp"
#include "timer.hpp"

struct SDL_Window;

/// @brief the Engine class handles runtime state management.
class Engine
{
public:
	Engine();
	~Engine();

	Engine(Engine const&) = delete;
	Engine& operator=(Engine const&) = delete;

	Engine(Engine&&) = delete;
	Engine& operator=(Engine&&) = delete;

	/// @brief Handle a window resize event.
	void onResize();

	/// @brief Update the Engine state.
	void update();

	/// @brief Render the next engine frame.
	void render();

	/// @brief Check if the Engine is running.
	/// @return 
	bool isRunning();

public:
	static constexpr char const* pWindowTitle = "Vulkan Renderer";
	static constexpr uint32_t DefaultWindowWidth = 1600;
	static constexpr uint32_t DefaultWindowHeight = 900;

private:
	bool m_running = true;
	SDL_Window* m_pEngineWindow = nullptr;
	uint32_t m_framebufferWidth = 0;
	uint32_t m_framebufferHeight = 0;
	bool m_captureInput = false;

	Timer m_frameTimer{};
	Timer m_cpuUpdateTimer{};
	Timer m_cpuRenderTimer{};
	InputManager m_inputManager{};
	CameraController m_cameraController{ 0.25F, 30.0F };
};