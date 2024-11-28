#include "camera_controller.hpp"

#include <cassert>

#include "math.hpp"
#include "input.hpp"
#include "scene.hpp"

CameraController::CameraController(float moveSpeed, float lookSpeed)
	:
	m_moveSpeed(moveSpeed),
	m_lookSpeed(lookSpeed)
{
	//
}

void CameraController::getActiveCamera(Scene* pScene)
{
	m_pScene = pScene;
}

void CameraController::update(InputManager const& inputManager, double deltaTimeMS)
{
	if (m_pScene == nullptr || m_pScene->activeCamera == RefUnused) {
		return;
	}

	Transform& transform = m_pScene->nodes.transform[m_pScene->activeCamera];
	glm::vec3 const forward = transform.forward();
	glm::vec3 const right = transform.right();

	glm::vec3 deltaPosition(0.0F);
	if (inputManager.isPressed(SDL_SCANCODE_W)) {
		deltaPosition -= forward;
	}
	if (inputManager.isPressed(SDL_SCANCODE_S)) {
		deltaPosition += forward;
	}
	if (inputManager.isPressed(SDL_SCANCODE_A)) {
		deltaPosition -= right;
	}
	if (inputManager.isPressed(SDL_SCANCODE_D)) {
		deltaPosition += right;
	}
	if (inputManager.isPressed(SDL_SCANCODE_E)) {
		deltaPosition += UP;
	}
	if (inputManager.isPressed(SDL_SCANCODE_Q)) {
		deltaPosition -= UP;
	}

	glm::vec2 rotationDelta = inputManager.mouseDelta() * m_lookSpeed * static_cast<float>(deltaTimeMS);
	transform.position += deltaPosition * m_moveSpeed * static_cast<float>(deltaTimeMS);
	transform.rotation = glm::rotate(transform.rotation, glm::radians(rotationDelta.x), UP);
	transform.rotation = glm::rotate(transform.rotation, glm::radians(rotationDelta.y), transform.right());
}
