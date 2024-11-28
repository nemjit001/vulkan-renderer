#include "input.hpp"

#include <cassert>

void InputManager::update()
{
	memcpy(m_lastKeystate, m_keystate, sizeof(m_keystate));

	if (!m_mouseUpdate) {
		m_mouseDelta = glm::vec3(0.0F);
	}
	m_mouseUpdate = false;
}

void InputManager::setKeyState(SDL_Scancode scancode, bool down)
{
	assert(scancode >= 0 && scancode < SDL_NUM_SCANCODES);
	m_keystate[scancode] = down;
}

void InputManager::setMouseDelta(glm::vec2 const& delta)
{
	m_mouseUpdate = true;
	m_mouseDelta = delta;
}

bool InputManager::isPressed(SDL_Scancode scancode) const
{
	assert(scancode >= 0 && scancode < SDL_NUM_SCANCODES);
	return m_keystate[scancode];
}

bool InputManager::isFirstPressed(SDL_Scancode scancode) const
{
	assert(scancode >= 0 && scancode < SDL_NUM_SCANCODES);
	return m_keystate[scancode] && m_keystate[scancode] != m_lastKeystate[scancode];
}

glm::vec2 InputManager::mouseDelta() const
{
	return m_mouseDelta;
}
