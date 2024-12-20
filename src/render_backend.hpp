#pragma once

#include <memory>
#include <vector>

#define VK_NO_PROTOTYPES
#include <SDL.h>
#include <vulkan/vulkan.h>

class Buffer;
class Texture;

/// @brief Command queue types available in a render device context.
enum class CommandQueueType : uint8_t
{
	Undefined = 0x00,
	Direct = 0x01,
	Copy = 0x02,
};

/// @brief Swap chain backbuffer.
struct Backbuffer
{
	VkFormat format;
	VkImage image;
	VkImageView view;
};

/// @brief Command context with associated command queue.
struct CommandContext
{
	CommandQueueType queue;
	VkCommandBuffer handle;
};

class RenderDeviceContext
{
public:
	RenderDeviceContext(VkPhysicalDevice physicalDevice, VkSurfaceKHR surface, uint32_t windowWidth, uint32_t windowHeight);
	~RenderDeviceContext();

	RenderDeviceContext(RenderDeviceContext const&) = delete;
	RenderDeviceContext& operator=(RenderDeviceContext const&) = delete;

	RenderDeviceContext(RenderDeviceContext&&) = delete;
	RenderDeviceContext& operator=(RenderDeviceContext&&) = delete;

	/// @brief Start a new frame, acquiring the next available backbuffer.
	/// @return 
	bool newFrame();

	/// @brief present the currently acquired frame.
	/// @return 
	bool present();

	/// @brief Resize swap dependent resources (swap framebuffers, color targets, depth target, etc.).
	/// @param width 
	/// @param height 
	/// @return 
	bool resizeSwapResources(uint32_t width, uint32_t height);

	/// @brief Create a GPU buffer.
	/// @param size 
	/// @param usage 
	/// @param memoryProperties 
	/// @param createMapped 
	/// @return A GPU buffer, NULL on error.
	std::shared_ptr<Buffer> createBuffer(
		size_t size,
		VkBufferUsageFlags usage,
		VkMemoryPropertyFlags memoryProperties,
		bool createMapped = false
	);

	/// @brief Create a GPU texture.
	/// @param imageType 
	/// @param format 
	/// @param usage 
	/// @param memoryProperties 
	/// @param width 
	/// @param height 
	/// @param depth 
	/// @param levels 
	/// @param layers 
	/// @param samples 
	/// @param tiling 
	/// @param initialLayout 
	/// @return 
	std::shared_ptr<Texture> createTexture(
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

	/// @brief Create a command context for use on a specific queue.
	/// @param queue Target command queue for the command buffer.
	/// @param commandContext 
	/// @return A boolean indicating successful creation.
	bool createCommandContext(CommandQueueType queue, CommandContext& commandContext);

	/// @brief Destroy a command context.
	/// @param commandContext Context to desctroy.
	void destroyCommandContext(CommandContext& commandContext);

	/// @brief Create a synchronization fence.
	/// @param pFence 
	/// @param signaled Create fence in the signaled state.
	/// @return A boolean indicating successful creation.
	bool createFence(VkFence* pFence, bool signaled);

	/// @brief Destroy a fence.
	/// @param fence 
	void destroyFence(VkFence fence);

	/// @brief Get the swap chain image format.
	/// @return 
	VkFormat getSwapFormat() const;

	/// @brief Get the current backbuffer index.
	/// @return 
	uint32_t getCurrentBackbufferIndex() const;

	/// @brief Get the number of backbuffers.
	/// @return 
	uint32_t backbufferCount() const;

	/// @brief Get the swap chain backbuffers
	/// @return 
	std::vector<Backbuffer> getBackbuffers() const;

	/// @brief Retrieve the physical device (adapter) associated with this device context.
	/// @return 
	VkPhysicalDevice getAdapter();

	/// @brief Retrieve the queue family index for a command queue type.
	/// @param queue 
	/// @return 
	uint32_t getQueueFamily(CommandQueueType queue);

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
	VkDevice device = VK_NULL_HANDLE;
	VkQueue directQueue = VK_NULL_HANDLE;

private:
	VkPhysicalDevice m_physicalDevice;
	VkSurfaceKHR m_surface;
	VkPhysicalDeviceMemoryProperties m_memoryProperties{};
	uint32_t m_directQueueFamily = VK_QUEUE_FAMILY_IGNORED;

	bool m_presentModeImmediateSupported = false;
	bool m_presentModeMailboxSupported = false;
	VkSwapchainCreateInfoKHR m_swapchainCreateInfo{};
	VkSwapchainKHR m_swapchain = VK_NULL_HANDLE;
	std::vector<VkImage> m_swapImages{};
	std::vector<VkImageView> m_swapImageViews{};
	std::vector<Backbuffer> m_backbuffers;
	VkFence m_swapAvailable = VK_NULL_HANDLE;

	VkCommandPool m_directCommandPool = VK_NULL_HANDLE;
	VkCommandPool m_transferCommandPool = VK_NULL_HANDLE;

	uint32_t m_backbufferIndex = 0;
};

namespace RenderBackend
{
	/// @brief Initialize the render backend.
	/// @param pWindow Associated window pointer.
	/// @return 
	bool init(SDL_Window* pWindow);

	/// @brief Shut down the render backend.
	void shutdown();

	/// @brief Get the Vulkan instance handle.
	/// @return 
	VkInstance getInstance();

	/// @brief Automatically pick a render device.
	/// @return A pointer to a new render device, or NULL on failure.
	std::unique_ptr<RenderDeviceContext> pickRenderDevice();
} // namespace RenderBackend
