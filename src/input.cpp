#include "input.hpp"

#include <cassert>

void InputManager::setKeyState(SDL_Scancode scancode, bool down)
{
	assert(scancode > 0 && scancode < SDL_NUM_SCANCODES);
	m_keystate[scancode] = down;
}

bool InputManager::isPressed(SDL_Scancode scancode) const
{
	assert(scancode > 0 && scancode < SDL_NUM_SCANCODES);
	return m_keystate[scancode];
}
