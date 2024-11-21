#include "renderer.hpp"

#include <cassert>

#include "render_backend.hpp"

IRenderer::IRenderer(RenderDeviceContext* pDeviceContext)
	:
	m_pDeviceContext(pDeviceContext)
{
	assert(m_pDeviceContext != nullptr);
}

ForwardRenderer::ForwardRenderer(RenderDeviceContext* pDeviceContext)
	:
	IRenderer(pDeviceContext)
{
	//
}

ForwardRenderer::~ForwardRenderer()
{
	//
}

bool ForwardRenderer::onResize(uint32_t width, uint32_t height)
{
	return m_pDeviceContext->resizeSwapResources(width, height);
}

void ForwardRenderer::update(Scene const& scene)
{
	(void)(scene);
}

void ForwardRenderer::render(Scene const& scene)
{
	(void)(scene);
}
