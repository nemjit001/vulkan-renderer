#pragma once

#include <SDL.h>

class InputManager
{
public:
	void setKeyState(SDL_Scancode scancode, bool down);

	bool isPressed(SDL_Scancode scancode) const;

private:
	bool m_keystate[SDL_NUM_SCANCODES]{};
};
