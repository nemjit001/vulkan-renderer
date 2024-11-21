#pragma once

#include <cstdint>

class Scene;

class IRenderer
{
public:
	virtual ~IRenderer() = default;

	virtual void onResize(uint32_t width, uint32_t height) = 0;

	virtual void update(Scene const& scene) = 0;

	virtual void render(Scene const& scene) = 0;
};
