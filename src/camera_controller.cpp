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

	SceneRef const& cameraNode = m_pScene->nodes.cameraRef[m_pScene->activeCamera];
	Transform& cameraTransform = m_pScene->nodes.transform[m_pScene->activeCamera];
	assert(cameraNode != RefUnused);

	glm::vec3 const camForward = cameraTransform.forward();
	glm::vec3 const camRight = cameraTransform.right();
	glm::vec3 const camUp = cameraTransform.up();

	glm::vec3 deltaPosition(0.0F);
	if (inputManager.isPressed(SDL_SCANCODE_W)) {
		deltaPosition += camForward;
	}
	if (inputManager.isPressed(SDL_SCANCODE_S)) {
		deltaPosition -= camForward;
	}
	if (inputManager.isPressed(SDL_SCANCODE_A)) {
		deltaPosition -= camRight;
	}
	if (inputManager.isPressed(SDL_SCANCODE_D)) {
		deltaPosition += camRight;
	}
	if (inputManager.isPressed(SDL_SCANCODE_E)) {
		deltaPosition += UP;
	}
	if (inputManager.isPressed(SDL_SCANCODE_Q)) {
		deltaPosition -= UP;
	}

	glm::vec2 delta = inputManager.mouseDelta() * m_lookSpeed * static_cast<float>(deltaTimeMS);
	glm::quat rotation = glm::rotate(cameraTransform.rotation, glm::radians(-delta.x), UP);
	

	// TODO(nemjit001): Handle rotation based on mouse movement

	cameraTransform.position += deltaPosition * m_moveSpeed * static_cast<float>(deltaTimeMS);
	cameraTransform.rotation = rotation;
}
