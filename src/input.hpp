#pragma once

#include <SDL_scancode.h>

#include "math.hpp"

class InputManager
{
public:
	void update();

	void setKeyState(SDL_Scancode scancode, bool down);

	void setMouseDelta(glm::vec2 const& delta);

	bool isPressed(SDL_Scancode scancode) const;

	bool isFirstPressed(SDL_Scancode scancode) const;

	glm::vec2 mouseDelta() const;

private:
	bool m_mouseUpdate = false;
	bool m_lastKeystate[SDL_NUM_SCANCODES]{};
	bool m_keystate[SDL_NUM_SCANCODES]{};
	glm::vec2 m_mouseDelta{};
};
