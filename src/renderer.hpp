#pragma once

#include <cstdint>

class RenderDeviceContext;
class Scene;

/// @brief Renderer interface, manages render passes, pipelines, render command recording, etc. internally.
class IRenderer
{
public:
	IRenderer(RenderDeviceContext* pDeviceContext);
	virtual ~IRenderer() = default;

	virtual bool onResize(uint32_t width, uint32_t height) = 0;

	virtual void update(Scene const& scene) = 0;

	virtual void render(Scene const& scene) = 0;

protected:
	RenderDeviceContext* m_pDeviceContext = nullptr;
};

/// @brief Forward renderer. Implements forward shading pipeline for opaque and transparent objects.
class ForwardRenderer
	:
	public IRenderer
{
public:
	ForwardRenderer(RenderDeviceContext* pDeviceContext);
	virtual ~ForwardRenderer();

	ForwardRenderer(ForwardRenderer const&) = delete;
	ForwardRenderer& operator=(ForwardRenderer const&) = delete;

	virtual bool onResize(uint32_t width, uint32_t height) override;

	virtual void update(Scene const& scene) override;

	virtual void render(Scene const& scene) override;
};
