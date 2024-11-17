#pragma once

#include <vector>

#define VK_NO_PROTOTYPES
#include <SDL.h>
#include <vulkan/vulkan.h>

#define SIZEOF_ARRAY(val)   (sizeof((val)) / sizeof((val)[0]))
#define VK_FAILED(expr)     ((expr) != VK_SUCCESS)

/// @brief GPU buffer with associated data.
struct Buffer
{
	/// @brief Destroy this buffer.
	void destroy();

	/// @brief Map buffer memory.
	void map();

	/// @brief Unmap buffer memory.
	void unmap();

	VkDevice device;
	VkBuffer handle;
	VkDeviceMemory memory;
	size_t size;
	bool mapped;
	void* pData;
};

/// @brief GPU texture with associated data.
struct Texture
{
	/// @brief Destroy this texture.
	void destroy();

	VkDevice device;
	VkImage handle;
	VkDeviceMemory memory;
	VkFormat format;
	uint32_t width;
	uint32_t height;
	uint32_t depthOrLayers;
	uint32_t levels;
};

class RenderDeviceContext
{
public:
	RenderDeviceContext() = default;
	RenderDeviceContext(VkPhysicalDevice physicalDevice, VkSurfaceKHR surface, uint32_t windowWidth, uint32_t windowHeight);
	~RenderDeviceContext();

	RenderDeviceContext(RenderDeviceContext const&) = delete;
	RenderDeviceContext& operator=(RenderDeviceContext const&) = delete;

	bool newFrame();

	bool present();

	bool resizeSwapResources(uint32_t width, uint32_t height);

	bool createBuffer(
		Buffer& buffer,
		size_t size,
		VkBufferUsageFlags usage,
		VkMemoryPropertyFlags memoryProperties,
		bool createMapped = false
	);

	bool createTexture(
		Texture& texture,
		VkImageType imageType,
		VkFormat format,
		VkImageUsageFlags usage,
		VkMemoryPropertyFlags memoryProperties,
		uint32_t width,
		uint32_t height,
		uint32_t depth,
		uint32_t levels = 1,
		uint32_t layers = 1,
		VkSampleCountFlagBits samples = VK_SAMPLE_COUNT_1_BIT,
		VkImageTiling tiling = VK_IMAGE_TILING_OPTIMAL,
		VkImageLayout initialLayout = VK_IMAGE_LAYOUT_UNDEFINED
	);

	uint32_t getCurrentBackbufferIndex() const;

private:
    /// @brief Find a device queue family based on required and exclusion flags.
    /// @param physicalDevice 
    /// @param surface Optional surface parameter, if not VK_NULL_HANDLE the returned queue family supports presenting to this surface.
    /// @param flags Required queue flags.
    /// @param exclude Queue flags that must not be set.
    /// @return The found queue family or VK_QUEUE_FAMILY_IGNORED on failure.
	static uint32_t findQueueFamily(VkPhysicalDevice physicalDevice, VkSurfaceKHR surface, VkQueueFlags flags, VkQueueFlags exclude);

    /// @brief Get the memory type index based on memory requirements and property flags.
    /// @param requirements 
    /// @param propertyFlags 
    /// @return The memory type index or UINT32_MAX on failure.
    uint32_t getMemoryTypeIndex(VkMemoryRequirements const& requirements, VkMemoryPropertyFlags propertyFlags) const;

public:
	VkPhysicalDevice physicalDevice;
	VkSurfaceKHR surface;
	VkPhysicalDeviceMemoryProperties memoryProperties{};
	uint32_t directQueueFamily = VK_QUEUE_FAMILY_IGNORED;

	VkDevice device = VK_NULL_HANDLE;
	VkQueue directQueue = VK_NULL_HANDLE;

	VkSwapchainCreateInfoKHR swapchainCreateInfo{};
	VkSwapchainKHR swapchain = VK_NULL_HANDLE;
	std::vector<VkImage> swapImages{};
	std::vector<VkImageView> swapImageViews{};
	Texture depthStencilTexture{};
	VkImageView depthStencilView = VK_NULL_HANDLE;

	VkSemaphore swapAvailable = VK_NULL_HANDLE;
	VkSemaphore swapReleased = VK_NULL_HANDLE;
	VkFence directQueueIdle = VK_NULL_HANDLE;

	VkCommandPool commandPool = VK_NULL_HANDLE;
	VkCommandBuffer commandBuffer = VK_NULL_HANDLE;

	VkRenderPass renderPass = VK_NULL_HANDLE;

	std::vector<VkFramebuffer> swapFramebuffers{};

	uint32_t backbufferIndex = 0;
};

namespace Renderer
{
	bool init(SDL_Window* pWindow);

	void shutdown();

	VkInstance getInstance();

	RenderDeviceContext* pickRenderDevice();
}
