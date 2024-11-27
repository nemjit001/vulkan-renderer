#pragma once

/// @brief Engine namespace with state management functions/
namespace Engine
{
	/// @brief Initialize the Engine.
	/// @return 
	bool init();

	/// @brief Shut down the Engine.
	void shutdown();

	/// @brief Handle a window resize event.
	void onResize();

	/// @brief Update the Engine state.
	void update();

	/// @brief Render the next engine frame.
	void render();

	/// @brief Check if the Engine is running.
	/// @return 
	bool isRunning();
} // namespace Engine
