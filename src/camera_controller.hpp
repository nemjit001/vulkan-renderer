#pragma once

class Scene;
class InputManager;

class CameraController
{
public:
	CameraController(float moveSpeed);

	void getActiveCamera(Scene* scene);

	void update(InputManager const& inputManager, double deltaTimeMS);

private:
	float m_moveSpeed = 1.0F;
	Scene* m_pScene = nullptr;
};
