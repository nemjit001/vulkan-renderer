#include "camera_controller.hpp"

#include <cassert>

#include "math.hpp"
#include "input.hpp"
#include "scene.hpp"

CameraController::CameraController(float moveSpeed)
	:
	m_moveSpeed(moveSpeed)
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

	SceneRef const& cameraNode = m_pScene->nodes.cameraRef[m_pScene->activeCamera];
	Transform& cameraTransform = m_pScene->nodes.transform[m_pScene->activeCamera];
	assert(cameraNode != RefUnused);

	glm::vec3 const camForward = cameraTransform.forward();
	glm::vec3 const camRight = cameraTransform.right();
	glm::vec3 const camUp = cameraTransform.up();

	glm::vec3 deltaPosition(0.0F);
	if (inputManager.isPressed(SDL_SCANCODE_W)) {
		deltaPosition += camForward * m_moveSpeed * static_cast<float>(deltaTimeMS);
	}
	if (inputManager.isPressed(SDL_SCANCODE_S)) {
		deltaPosition -= camForward * m_moveSpeed * static_cast<float>(deltaTimeMS);
	}
	if (inputManager.isPressed(SDL_SCANCODE_A)) {
		deltaPosition += camRight * m_moveSpeed * static_cast<float>(deltaTimeMS);
	}
	if (inputManager.isPressed(SDL_SCANCODE_D)) {
		deltaPosition -= camRight * m_moveSpeed * static_cast<float>(deltaTimeMS);
	}
	if (inputManager.isPressed(SDL_SCANCODE_E)) {
		deltaPosition += camUp * m_moveSpeed * static_cast<float>(deltaTimeMS);
	}
	if (inputManager.isPressed(SDL_SCANCODE_Q)) {
		deltaPosition -= camUp * m_moveSpeed * static_cast<float>(deltaTimeMS);
	}

	cameraTransform.position += deltaPosition;
}
