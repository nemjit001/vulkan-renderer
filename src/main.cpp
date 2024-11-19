#include <cassert>
#include <iostream>
#include <vector>

#define SDL_MAIN_HANDLED
#define VK_NO_PROTOTYPES
#define VOLK_IMPLEMENTATION
#include <imgui.h>
#include <imgui_impl_sdl2.h>
#include <imgui_impl_vulkan.h>
#include <SDL.h>
#include <SDL_vulkan.h>
#include <volk.h>

#include "assets.hpp"
#include "math.hpp"
#include "scene.hpp"
#include "renderer.hpp"
#include "timer.hpp"

namespace Engine
{
    constexpr char const* pWindowTitle = "Vulkan Renderer";
    constexpr uint32_t DefaultWindowWidth = 1600;
    constexpr uint32_t DefaultWindowHeight = 900;

    bool isRunning = true;
    SDL_Window* pWindow = nullptr;
    Timer frameTimer{};
    Timer cpuUpdateTimer{};
    Timer cpuRenderTimer{};
    RenderDeviceContext* pDeviceContext = nullptr;
    uint32_t framebufferWidth = 0;
    uint32_t framebufferHeight = 0;

    // Frame command buffers w/ sync primitive
    VkFence commandsFinished = VK_NULL_HANDLE;
    VkCommandBuffer commandBuffer = VK_NULL_HANDLE;

    // Depth stencil target
    Texture depthStencilTexture{};
    VkImageView depthStencilView = VK_NULL_HANDLE;

    // Render pass & associated resources
    VkRenderPass renderPass = VK_NULL_HANDLE;
    std::vector<VkFramebuffer> framebuffers{};

    // GUI data
    VkDescriptorPool imguiDescriptorPool = VK_NULL_HANDLE; //< descriptor pool specifically for ImGui usage

    // Per pass data
    VkSampler textureSampler = VK_NULL_HANDLE;
    VkDescriptorSetLayout sceneDataSetLayout = VK_NULL_HANDLE;
    VkDescriptorSetLayout lightDataSetLayout = VK_NULL_HANDLE;
    VkDescriptorSetLayout objectDataSetLayout = VK_NULL_HANDLE;
    VkPipelineLayout pipelineLayout = VK_NULL_HANDLE;
    VkPipeline depthPrepassPipeline = VK_NULL_HANDLE;
    VkPipeline forwardPipeline = VK_NULL_HANDLE;
    VkViewport viewport{};
    VkRect2D scissor{};

    // Scene data
    VkDescriptorPool sceneDescriptorPool = VK_NULL_HANDLE;
    VkDescriptorPool lightDescriptorPool = VK_NULL_HANDLE;
    Scene scene{};

    bool init()
    {
        ImGui::CreateContext();
        ImGuiIO& io = ImGui::GetIO(); (void)(io);
        io.IniFilename = nullptr;

        ImGui::StyleColorsDark();

        if (SDL_Init(SDL_INIT_VIDEO) != 0)
        {
            printf("SDL init failed: %s\n", SDL_GetError());
            return false;
        }

        pWindow = SDL_CreateWindow(pWindowTitle, SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED, DefaultWindowWidth, DefaultWindowHeight, SDL_WINDOW_RESIZABLE | SDL_WINDOW_VULKAN);
        if (pWindow == nullptr)
        {
            printf("SDL window create failed: %s\n", SDL_GetError());
            return false;
        }

        if (!ImGui_ImplSDL2_InitForVulkan(pWindow))
        {
            printf("ImGui init for SDL2 failed\n");
            return false;
        }

        if (!Renderer::init(pWindow))
        {
            printf("VK Renderer renderer init failed\n");
            return false;
        }

        pDeviceContext = Renderer::pickRenderDevice();
        if (pDeviceContext == nullptr)
        {
            printf("VK Renderer no device available\n");
            return false;
        }

        framebufferWidth = DefaultWindowWidth;
        framebufferHeight = DefaultWindowHeight;

        // Create frame command buffers w/ sync
        {
            if (!pDeviceContext->createFence(&commandsFinished, true))
            {
                printf("VK Renderer command sync primitive create failed\n");
                return false;
            }

            if (!pDeviceContext->createCommandBuffer(CommandQueueType::Direct, &commandBuffer))
            {
                printf("VK Renderer command buffer create failed\n");
                return false;
            }
        }

        // Create depth stencil target
        {
            if (!pDeviceContext->createTexture(
                depthStencilTexture,
                VK_IMAGE_TYPE_2D,
                VK_FORMAT_D32_SFLOAT,
                VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT,
                VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
                framebufferWidth, framebufferHeight, 1
            ))
            {
                printf("Vulkan depth stencil texture create failed\n");
                return false;
            }

            VkImageViewCreateInfo depthStencilViewCreateInfo{ VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO };
            depthStencilViewCreateInfo.flags = 0;
            depthStencilViewCreateInfo.image = depthStencilTexture.handle;
            depthStencilViewCreateInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
            depthStencilViewCreateInfo.format = depthStencilTexture.format;
            depthStencilViewCreateInfo.components.r = VK_COMPONENT_SWIZZLE_IDENTITY;
            depthStencilViewCreateInfo.components.g = VK_COMPONENT_SWIZZLE_IDENTITY;
            depthStencilViewCreateInfo.components.b = VK_COMPONENT_SWIZZLE_IDENTITY;
            depthStencilViewCreateInfo.components.a = VK_COMPONENT_SWIZZLE_IDENTITY;
            depthStencilViewCreateInfo.subresourceRange.baseMipLevel = 0;
            depthStencilViewCreateInfo.subresourceRange.levelCount = depthStencilTexture.levels;
            depthStencilViewCreateInfo.subresourceRange.baseArrayLayer = 0;
            depthStencilViewCreateInfo.subresourceRange.layerCount = depthStencilTexture.depthOrLayers;
            depthStencilViewCreateInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT;

            if (VK_FAILED(vkCreateImageView(pDeviceContext->device, &depthStencilViewCreateInfo, nullptr, &depthStencilView)))
            {
                printf("Vulkan depth stencil view create failed\n");
                return false;
            }
        }

        // Create render pass
        {
            VkAttachmentDescription colorAttachment{};
            colorAttachment.flags = 0;
            colorAttachment.format = pDeviceContext->getSwapFormat();
            colorAttachment.samples = VK_SAMPLE_COUNT_1_BIT;
            colorAttachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
            colorAttachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
            colorAttachment.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
            colorAttachment.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
            colorAttachment.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
            colorAttachment.finalLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;

            VkAttachmentDescription depthStencilAttachment{};
            depthStencilAttachment.flags = 0;
            depthStencilAttachment.format = VK_FORMAT_D32_SFLOAT;
            depthStencilAttachment.samples = VK_SAMPLE_COUNT_1_BIT;
            depthStencilAttachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
            depthStencilAttachment.storeOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
            depthStencilAttachment.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
            depthStencilAttachment.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
            depthStencilAttachment.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
            depthStencilAttachment.finalLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;

            VkAttachmentReference depthPrepassDepthStencilAttachment{ 1, VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL };

            VkSubpassDescription depthPrepass{};
            depthPrepass.flags = 0;
            depthPrepass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
            depthPrepass.inputAttachmentCount = 0;
            depthPrepass.pInputAttachments = nullptr;
            depthPrepass.colorAttachmentCount = 0;
            depthPrepass.pColorAttachments = nullptr;
            depthPrepass.pResolveAttachments = nullptr;
            depthPrepass.pDepthStencilAttachment = &depthPrepassDepthStencilAttachment;
            depthPrepass.preserveAttachmentCount = 0;
            depthPrepass.pPreserveAttachments = nullptr;

            VkAttachmentReference forwardPassColorAttachments[] = {
                VkAttachmentReference{ 0, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL },
            };

            VkAttachmentReference forwardPassDepthStencilAttachment{ 1, VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL };

            VkSubpassDescription forwardPass{};
            forwardPass.flags = 0;
            forwardPass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
            forwardPass.inputAttachmentCount = 0;
            forwardPass.pInputAttachments = nullptr;
            forwardPass.colorAttachmentCount = SIZEOF_ARRAY(forwardPassColorAttachments);
            forwardPass.pColorAttachments = forwardPassColorAttachments;
            forwardPass.pResolveAttachments = nullptr;
            forwardPass.pDepthStencilAttachment = &forwardPassDepthStencilAttachment;
            forwardPass.preserveAttachmentCount = 0;
            forwardPass.pPreserveAttachments = nullptr;

            VkAttachmentReference GUIPassColorAttachments[] = {
                VkAttachmentReference{ 0, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL },
            };

            VkSubpassDescription GUIPass{};
            GUIPass.flags = 0;
            GUIPass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
            GUIPass.inputAttachmentCount = 0;
            GUIPass.pInputAttachments = nullptr;
            GUIPass.colorAttachmentCount = SIZEOF_ARRAY(GUIPassColorAttachments);
            GUIPass.pColorAttachments = GUIPassColorAttachments;
            GUIPass.pResolveAttachments = nullptr;
            GUIPass.pDepthStencilAttachment = nullptr;
            GUIPass.preserveAttachmentCount = 0;
            GUIPass.pPreserveAttachments = nullptr;

            VkSubpassDependency previousFrameDependency{};
            previousFrameDependency.srcSubpass = VK_SUBPASS_EXTERNAL;
            previousFrameDependency.dstSubpass = 0;
            previousFrameDependency.srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
            previousFrameDependency.dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
            previousFrameDependency.srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
            previousFrameDependency.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_READ_BIT;

            VkSubpassDependency depthPrepassDependency{};
            depthPrepassDependency.srcSubpass = 0;
            depthPrepassDependency.dstSubpass = 1;
            depthPrepassDependency.srcStageMask = VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT;
            depthPrepassDependency.dstStageMask = VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT;
            depthPrepassDependency.srcAccessMask = VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
            depthPrepassDependency.dstAccessMask = VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_READ_BIT;

            VkSubpassDependency GUIPassDependency{};
            GUIPassDependency.srcSubpass = 1;
            GUIPassDependency.dstSubpass = 2;
            GUIPassDependency.srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
            GUIPassDependency.dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
            GUIPassDependency.srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
            GUIPassDependency.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_READ_BIT;

            VkAttachmentDescription attachments[] = { colorAttachment, depthStencilAttachment, };
            VkSubpassDescription subpasses[] = { depthPrepass, forwardPass, GUIPass, };
            VkSubpassDependency dependencies[] = { previousFrameDependency, depthPrepassDependency, GUIPassDependency, };
            VkRenderPassCreateInfo renderPassCreateInfo{ VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO };
            renderPassCreateInfo.flags = 0;
            renderPassCreateInfo.attachmentCount = SIZEOF_ARRAY(attachments);
            renderPassCreateInfo.pAttachments = attachments;
            renderPassCreateInfo.subpassCount = SIZEOF_ARRAY(subpasses);
            renderPassCreateInfo.pSubpasses = subpasses;
            renderPassCreateInfo.dependencyCount = SIZEOF_ARRAY(dependencies);
            renderPassCreateInfo.pDependencies = dependencies;

            if (VK_FAILED(vkCreateRenderPass(pDeviceContext->device, &renderPassCreateInfo, nullptr, &renderPass)))
            {
                throw std::runtime_error("Vulkan render pass create failed\n");
            }
        }

        // Create framebuffers
        {
            auto const& backbuffers = pDeviceContext->getBackbuffers();
            framebuffers.reserve(backbuffers.size());
            for (auto& buffer : backbuffers)
            {
                VkImageView attachments[] = { buffer.view, depthStencilView, };

                VkFramebufferCreateInfo framebufferCreateInfo{ VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO };
                framebufferCreateInfo.flags = 0;
                framebufferCreateInfo.renderPass = renderPass;
                framebufferCreateInfo.attachmentCount = SIZEOF_ARRAY(attachments);
                framebufferCreateInfo.pAttachments = attachments;
                framebufferCreateInfo.width = framebufferWidth;
                framebufferCreateInfo.height = framebufferHeight;
                framebufferCreateInfo.layers = 1;

                VkFramebuffer framebuffer = VK_NULL_HANDLE;
                vkCreateFramebuffer(pDeviceContext->device, &framebufferCreateInfo, nullptr, &framebuffer);
                assert(framebuffer != VK_NULL_HANDLE);
                framebuffers.push_back(framebuffer);
            }
        }

        // Init ImGui for Vulkan
        {
            VkDescriptorPoolSize poolSizes[] = {
                VkDescriptorPoolSize{ VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1 },
            };

            VkDescriptorPoolCreateInfo imguiPoolCreateInfo{ VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO };
            imguiPoolCreateInfo.flags = VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT;
            imguiPoolCreateInfo.maxSets = 1;
            imguiPoolCreateInfo.poolSizeCount = SIZEOF_ARRAY(poolSizes);
            imguiPoolCreateInfo.pPoolSizes = poolSizes;

            if (VK_FAILED(vkCreateDescriptorPool(pDeviceContext->device, &imguiPoolCreateInfo, nullptr, &imguiDescriptorPool)))
            {
                printf("Vulkan imgui descriptor pool create failed\n");
                return false;
            }

            ImGui_ImplVulkan_InitInfo initInfo{};
            initInfo.Instance = Renderer::getInstance();
            initInfo.PhysicalDevice = pDeviceContext->physicalDevice;
            initInfo.Device = pDeviceContext->device;
            initInfo.QueueFamily = pDeviceContext->directQueueFamily;
            initInfo.Queue = pDeviceContext->directQueue;
            initInfo.DescriptorPool = imguiDescriptorPool;
            initInfo.RenderPass = renderPass;
            initInfo.MinImageCount = pDeviceContext->backbufferCount();
            initInfo.ImageCount = pDeviceContext->backbufferCount();
            initInfo.MSAASamples = VK_SAMPLE_COUNT_1_BIT;
            initInfo.PipelineCache = VK_NULL_HANDLE;
            initInfo.Subpass = 2;
            initInfo.UseDynamicRendering = false;
            initInfo.Allocator = nullptr;

            auto imguiLoadFunc = [](char const* pName, void* pUserData) { (void)(pUserData); return vkGetInstanceProcAddr(Renderer::getInstance(), pName); };
            if (!ImGui_ImplVulkan_LoadFunctions(imguiLoadFunc, nullptr)
                || !ImGui_ImplVulkan_Init(&initInfo))
            {
                printf("ImGui init for Vulkan failed\n");
                return false;
            }
        }

        // Create samplers and descriptor set layouts
        {
            VkSamplerCreateInfo samplerCreateInfo{ VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO };
            samplerCreateInfo.flags = 0;
            samplerCreateInfo.magFilter = VK_FILTER_LINEAR;
            samplerCreateInfo.minFilter = VK_FILTER_LINEAR;
            samplerCreateInfo.mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR;
            samplerCreateInfo.addressModeU = VK_SAMPLER_ADDRESS_MODE_REPEAT;
            samplerCreateInfo.addressModeV = VK_SAMPLER_ADDRESS_MODE_REPEAT;
            samplerCreateInfo.addressModeW = VK_SAMPLER_ADDRESS_MODE_REPEAT;
            samplerCreateInfo.mipLodBias = 0.0F;
            samplerCreateInfo.anisotropyEnable = VK_TRUE;
            samplerCreateInfo.maxAnisotropy = 16.0F; // TODO(nemjit001): query max from device.
            samplerCreateInfo.compareEnable = VK_FALSE;
            samplerCreateInfo.compareOp = VK_COMPARE_OP_ALWAYS;
            samplerCreateInfo.minLod = 0.0F;
            samplerCreateInfo.maxLod = VK_LOD_CLAMP_NONE;
            samplerCreateInfo.borderColor = VK_BORDER_COLOR_FLOAT_OPAQUE_BLACK;
            samplerCreateInfo.unnormalizedCoordinates = VK_FALSE;

            if (VK_FAILED(vkCreateSampler(pDeviceContext->device, &samplerCreateInfo, nullptr, &textureSampler)))
            {
                printf("Vulkan texture sampler create failed\n");
                return false;
            }

            VkDescriptorSetLayoutBinding sceneDataUniformBinding{};
            sceneDataUniformBinding.binding = 0;
            sceneDataUniformBinding.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
            sceneDataUniformBinding.descriptorCount = 1;
            sceneDataUniformBinding.stageFlags = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT;
            sceneDataUniformBinding.pImmutableSamplers = nullptr;

            VkDescriptorSetLayoutBinding sceneDataSetLayoutBindings[] = { sceneDataUniformBinding };
            VkDescriptorSetLayoutCreateInfo sceneDataSetLayoutCreateInfo{ VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO };
            sceneDataSetLayoutCreateInfo.flags = 0;
            sceneDataSetLayoutCreateInfo.bindingCount = SIZEOF_ARRAY(sceneDataSetLayoutBindings);
            sceneDataSetLayoutCreateInfo.pBindings = sceneDataSetLayoutBindings;

            if (VK_FAILED(vkCreateDescriptorSetLayout(pDeviceContext->device, &sceneDataSetLayoutCreateInfo, nullptr, &sceneDataSetLayout)))
            {
                printf("Vulkan scene descriptor set layout create failed\n");
                return false;
            }

            VkDescriptorSetLayoutBinding lightDataUniformBinding{};
            lightDataUniformBinding.binding = 0;
            lightDataUniformBinding.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
            lightDataUniformBinding.descriptorCount = 1;
            lightDataUniformBinding.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
            lightDataUniformBinding.pImmutableSamplers = nullptr;

            VkDescriptorSetLayoutBinding lightDataSetLayoutBindings[] = { lightDataUniformBinding };
            VkDescriptorSetLayoutCreateInfo lightDataSetLayoutCreateInfo{ VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO };
            lightDataSetLayoutCreateInfo.flags = 0;
            lightDataSetLayoutCreateInfo.bindingCount = SIZEOF_ARRAY(lightDataSetLayoutBindings);
            lightDataSetLayoutCreateInfo.pBindings = lightDataSetLayoutBindings;

            if (VK_FAILED(vkCreateDescriptorSetLayout(pDeviceContext->device, &lightDataSetLayoutCreateInfo, nullptr, &lightDataSetLayout)))
            {
                printf("Vulkan light descriptor set layout create failed\n");
                return false;
            }

            VkDescriptorSetLayoutBinding objectDataUniformBinding{};
            objectDataUniformBinding.binding = 0;
            objectDataUniformBinding.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
            objectDataUniformBinding.descriptorCount = 1;
            objectDataUniformBinding.stageFlags = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT;
            objectDataUniformBinding.pImmutableSamplers = nullptr;

            VkDescriptorSetLayoutBinding colorTextureBinding{};
            colorTextureBinding.binding = 1;
            colorTextureBinding.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
            colorTextureBinding.descriptorCount = 1;
            colorTextureBinding.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
            colorTextureBinding.pImmutableSamplers = &textureSampler;

            VkDescriptorSetLayoutBinding normalTextureBinding{};
            normalTextureBinding.binding = 2;
            normalTextureBinding.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
            normalTextureBinding.descriptorCount = 1;
            normalTextureBinding.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
            normalTextureBinding.pImmutableSamplers = &textureSampler;

            VkDescriptorSetLayoutBinding objectDataSetLayoutBindings[] = { objectDataUniformBinding, colorTextureBinding, normalTextureBinding };
            VkDescriptorSetLayoutCreateInfo objectDataSetLayoutCreateInfo{ VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO };
            objectDataSetLayoutCreateInfo.flags = 0;
            objectDataSetLayoutCreateInfo.bindingCount = SIZEOF_ARRAY(objectDataSetLayoutBindings);
            objectDataSetLayoutCreateInfo.pBindings = objectDataSetLayoutBindings;

            if (VK_FAILED(vkCreateDescriptorSetLayout(pDeviceContext->device, &objectDataSetLayoutCreateInfo, nullptr, &objectDataSetLayout)))
            {
                printf("Vulkan scene descriptor set layout create failed\n");
                return false;
            }
        }

        // Create pipeline layout
        {
            VkDescriptorSetLayout descriptorSetLayouts[] = { sceneDataSetLayout, lightDataSetLayout, objectDataSetLayout, };
            VkPipelineLayoutCreateInfo pipelineLayoutCreateInfo{ VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO };
            pipelineLayoutCreateInfo.flags = 0;
            pipelineLayoutCreateInfo.setLayoutCount = SIZEOF_ARRAY(descriptorSetLayouts);
            pipelineLayoutCreateInfo.pSetLayouts = descriptorSetLayouts;
            pipelineLayoutCreateInfo.pushConstantRangeCount = 0;
            pipelineLayoutCreateInfo.pPushConstantRanges = nullptr;

            if (VK_FAILED(vkCreatePipelineLayout(pDeviceContext->device, &pipelineLayoutCreateInfo, nullptr, &pipelineLayout)))
            {
                printf("Vulkan pipeline layout create failed\n");
                return false;
            }
        }

        // Create graphics pipelines
        {
            std::vector<uint32_t> vertexShaderCode;
            if (!readShaderFile("static.vert.spv", vertexShaderCode))
            {
                printf("VK Renderer vertex shader read failed\n");
                return false;
            }

            VkShaderModuleCreateInfo vertexShaderCreateInfo{ VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO };
            vertexShaderCreateInfo.flags = 0;
            vertexShaderCreateInfo.codeSize = static_cast<uint32_t>(vertexShaderCode.size() * sizeof(uint32_t));
            vertexShaderCreateInfo.pCode = vertexShaderCode.data();

            VkShaderModule vertexShader = VK_NULL_HANDLE;
            if (VK_FAILED(vkCreateShaderModule(pDeviceContext->device, &vertexShaderCreateInfo, nullptr, &vertexShader)))
            {
                printf("Vulkan vertex shader create failed\n");
                return false;
            }

            std::vector<uint32_t> forwardFragmentShaderCode;
            if (!readShaderFile("forward.frag.spv", forwardFragmentShaderCode))
            {
                printf("VK Renderer forward fragment shader read failed\n");
                return false;
            }

            VkShaderModuleCreateInfo forwardFragmentShaderCreateInfo{ VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO };
            forwardFragmentShaderCreateInfo.flags = 0;
            forwardFragmentShaderCreateInfo.codeSize = static_cast<uint32_t>(forwardFragmentShaderCode.size() * sizeof(uint32_t));
            forwardFragmentShaderCreateInfo.pCode = forwardFragmentShaderCode.data();

            VkShaderModule forwardFragmentShader = VK_NULL_HANDLE;
            if (VK_FAILED(vkCreateShaderModule(pDeviceContext->device, &forwardFragmentShaderCreateInfo, nullptr, &forwardFragmentShader)))
            {
                printf("Vulkan forward fragment shader create failed\n");
                return false;
            }

            std::vector<uint32_t> depthOnlyFragmentShaderCode;
            if (!readShaderFile("depth_only.frag.spv", depthOnlyFragmentShaderCode))
            {
                printf("VK Renderer depth only fragment shader read failed\n");
                return false;
            }

            VkShaderModuleCreateInfo depthOnlyFragmentShaderCreateInfo{ VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO };
            depthOnlyFragmentShaderCreateInfo.flags = 0;
            depthOnlyFragmentShaderCreateInfo.codeSize = static_cast<uint32_t>(depthOnlyFragmentShaderCode.size() * sizeof(uint32_t));
            depthOnlyFragmentShaderCreateInfo.pCode = depthOnlyFragmentShaderCode.data();

            VkShaderModule depthOnlyFragmentShader = VK_NULL_HANDLE;
            if (VK_FAILED(vkCreateShaderModule(pDeviceContext->device, &depthOnlyFragmentShaderCreateInfo, nullptr, &depthOnlyFragmentShader)))
            {
                printf("Vulkan depth only fragment shader create failed\n");
                return false;
            }

            VkPipelineShaderStageCreateInfo vertexStageInfo{ VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO };
            vertexStageInfo.flags = 0;
            vertexStageInfo.stage = VK_SHADER_STAGE_VERTEX_BIT;
            vertexStageInfo.module = vertexShader;
            vertexStageInfo.pName = "main";
            vertexStageInfo.pSpecializationInfo = nullptr;

            VkPipelineShaderStageCreateInfo forwardFragmentStageInfo{ VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO };
            forwardFragmentStageInfo.flags = 0;
            forwardFragmentStageInfo.stage = VK_SHADER_STAGE_FRAGMENT_BIT;
            forwardFragmentStageInfo.module = forwardFragmentShader;
            forwardFragmentStageInfo.pName = "main";
            forwardFragmentStageInfo.pSpecializationInfo = nullptr;

            VkPipelineShaderStageCreateInfo depthOnlyFragmentStageInfo{ VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO };
            depthOnlyFragmentStageInfo.flags = 0;
            depthOnlyFragmentStageInfo.stage = VK_SHADER_STAGE_FRAGMENT_BIT;
            depthOnlyFragmentStageInfo.module = depthOnlyFragmentShader;
            depthOnlyFragmentStageInfo.pName = "main";
            depthOnlyFragmentStageInfo.pSpecializationInfo = nullptr;

            VkVertexInputBindingDescription vertexBindings[] = {
                VkVertexInputBindingDescription{ 0, sizeof(Vertex), VK_VERTEX_INPUT_RATE_VERTEX }
            };

            VkVertexInputAttributeDescription vertexAttributes[] = {
                VkVertexInputAttributeDescription{ 0, 0, VK_FORMAT_R32G32B32_SFLOAT, offsetof(Vertex, position) },
                VkVertexInputAttributeDescription{ 1, 0, VK_FORMAT_R32G32B32_SFLOAT, offsetof(Vertex, color) },
                VkVertexInputAttributeDescription{ 2, 0, VK_FORMAT_R32G32B32_SFLOAT, offsetof(Vertex, normal) },
                VkVertexInputAttributeDescription{ 3, 0, VK_FORMAT_R32G32B32_SFLOAT, offsetof(Vertex, tangent) },
                VkVertexInputAttributeDescription{ 4, 0, VK_FORMAT_R32G32_SFLOAT, offsetof(Vertex, texCoord) },
            };

            VkPipelineVertexInputStateCreateInfo vertexInputState{ VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO };
            vertexInputState.flags = 0;
            vertexInputState.vertexBindingDescriptionCount = SIZEOF_ARRAY(vertexBindings);
            vertexInputState.pVertexBindingDescriptions = vertexBindings;
            vertexInputState.vertexAttributeDescriptionCount = SIZEOF_ARRAY(vertexAttributes);
            vertexInputState.pVertexAttributeDescriptions = vertexAttributes;

            VkPipelineInputAssemblyStateCreateInfo inputAssemblyState{ VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO };
            inputAssemblyState.flags = 0;
            inputAssemblyState.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
            inputAssemblyState.primitiveRestartEnable = VK_FALSE;

            float viewportWidth = static_cast<float>(framebufferWidth);
            float viewportHeight = static_cast<float>(framebufferHeight);
            viewport = VkViewport{ 0.0F, viewportHeight, viewportWidth, -1.0F * viewportHeight, 0.0F, 1.0F }; // viewport height hack is needed because of OpenGL / Vulkan viewport differences
            scissor = VkRect2D{ VkOffset2D{ 0, 0 }, VkExtent2D{ framebufferWidth, framebufferHeight } };

            VkPipelineViewportStateCreateInfo viewportState{ VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO };
            viewportState.flags = 0;
            viewportState.viewportCount = 1;
            viewportState.pViewports = &viewport;
            viewportState.scissorCount = 1;
            viewportState.pScissors = &scissor;

            VkPipelineRasterizationStateCreateInfo rasterizationState{ VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO };
            rasterizationState.flags = 0;
            rasterizationState.depthClampEnable = VK_FALSE;
            rasterizationState.rasterizerDiscardEnable = VK_FALSE;
            rasterizationState.polygonMode = VK_POLYGON_MODE_FILL;
            rasterizationState.cullMode = VK_CULL_MODE_BACK_BIT;
            rasterizationState.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;
            rasterizationState.depthBiasEnable = VK_FALSE;
            rasterizationState.depthBiasConstantFactor = 0.0F;
            rasterizationState.depthBiasClamp = 0.0F;
            rasterizationState.depthBiasSlopeFactor = 0.0F;
            rasterizationState.lineWidth = 1.0F;

            VkPipelineMultisampleStateCreateInfo multisampleState{ VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO };
            multisampleState.flags = 0;
            multisampleState.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;
            multisampleState.sampleShadingEnable = VK_FALSE;
            multisampleState.minSampleShading = 0.0F;
            multisampleState.pSampleMask = nullptr;
            multisampleState.alphaToCoverageEnable = VK_FALSE;
            multisampleState.alphaToOneEnable = VK_FALSE;

            VkPipelineDepthStencilStateCreateInfo depthStencilStateWrite{ VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO };
            depthStencilStateWrite.flags = 0;
            depthStencilStateWrite.depthTestEnable = VK_TRUE;
            depthStencilStateWrite.depthWriteEnable = VK_TRUE;
            depthStencilStateWrite.depthCompareOp = VK_COMPARE_OP_LESS;
            depthStencilStateWrite.depthBoundsTestEnable = VK_TRUE;
            depthStencilStateWrite.stencilTestEnable = VK_FALSE;
            depthStencilStateWrite.front =
                VkStencilOpState{ VK_STENCIL_OP_KEEP, VK_STENCIL_OP_KEEP, VK_STENCIL_OP_KEEP, VK_COMPARE_OP_ALWAYS, UINT32_MAX, UINT32_MAX, UINT32_MAX };
            depthStencilStateWrite.back =
                VkStencilOpState{ VK_STENCIL_OP_KEEP, VK_STENCIL_OP_KEEP, VK_STENCIL_OP_KEEP, VK_COMPARE_OP_ALWAYS, UINT32_MAX, UINT32_MAX, UINT32_MAX };
            depthStencilStateWrite.minDepthBounds = 0.0F;
            depthStencilStateWrite.maxDepthBounds = 1.0F;

            VkPipelineDepthStencilStateCreateInfo depthStencilStateCompare{ VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO };
            depthStencilStateCompare.flags = 0;
            depthStencilStateCompare.depthTestEnable = VK_TRUE;
            depthStencilStateCompare.depthWriteEnable = VK_FALSE;
            depthStencilStateCompare.depthCompareOp = VK_COMPARE_OP_EQUAL;
            depthStencilStateCompare.depthBoundsTestEnable = VK_FALSE;
            depthStencilStateCompare.stencilTestEnable = VK_FALSE;
            depthStencilStateCompare.front =
                VkStencilOpState{ VK_STENCIL_OP_KEEP, VK_STENCIL_OP_KEEP, VK_STENCIL_OP_KEEP, VK_COMPARE_OP_ALWAYS, UINT32_MAX, UINT32_MAX, UINT32_MAX };
            depthStencilStateCompare.back =
                VkStencilOpState{ VK_STENCIL_OP_KEEP, VK_STENCIL_OP_KEEP, VK_STENCIL_OP_KEEP, VK_COMPARE_OP_ALWAYS, UINT32_MAX, UINT32_MAX, UINT32_MAX };
            depthStencilStateCompare.minDepthBounds = 0.0F;
            depthStencilStateCompare.maxDepthBounds = 0.0F;

            VkPipelineColorBlendAttachmentState colorBlendAttachments[] = {
                VkPipelineColorBlendAttachmentState{
                    VK_FALSE,
                    VK_BLEND_FACTOR_ZERO,
                    VK_BLEND_FACTOR_ONE,
                    VK_BLEND_OP_ADD,
                    VK_BLEND_FACTOR_ZERO,
                    VK_BLEND_FACTOR_ONE,
                    VK_BLEND_OP_ADD,
                    VK_COLOR_COMPONENT_R_BIT
                    | VK_COLOR_COMPONENT_G_BIT
                    | VK_COLOR_COMPONENT_B_BIT
                    | VK_COLOR_COMPONENT_A_BIT
                },
            };

            VkPipelineColorBlendStateCreateInfo colorBlendState{ VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO };
            colorBlendState.flags = 0;
            colorBlendState.logicOpEnable = VK_FALSE;
            colorBlendState.logicOp = VK_LOGIC_OP_CLEAR;
            colorBlendState.attachmentCount = SIZEOF_ARRAY(colorBlendAttachments);
            colorBlendState.pAttachments = colorBlendAttachments;
            colorBlendState.blendConstants[0] = 0.0F;
            colorBlendState.blendConstants[1] = 0.0F;
            colorBlendState.blendConstants[2] = 0.0F;
            colorBlendState.blendConstants[3] = 0.0F;

            VkDynamicState dynamicStates[] = {
                VK_DYNAMIC_STATE_VIEWPORT,
                VK_DYNAMIC_STATE_SCISSOR,
            };

            VkPipelineDynamicStateCreateInfo dynamicState{ VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO };
            dynamicState.flags = 0;
            dynamicState.dynamicStateCount = SIZEOF_ARRAY(dynamicStates);
            dynamicState.pDynamicStates = dynamicStates;

            VkPipelineShaderStageCreateInfo depthPrepassShaderStages[] = { vertexStageInfo, depthOnlyFragmentStageInfo, };
            VkGraphicsPipelineCreateInfo depthPrepassPipelineCreateInfo{ VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO };
            depthPrepassPipelineCreateInfo.flags = 0;
            depthPrepassPipelineCreateInfo.stageCount = SIZEOF_ARRAY(depthPrepassShaderStages);
            depthPrepassPipelineCreateInfo.pStages = depthPrepassShaderStages;
            depthPrepassPipelineCreateInfo.pVertexInputState = &vertexInputState;
            depthPrepassPipelineCreateInfo.pInputAssemblyState = &inputAssemblyState;
            depthPrepassPipelineCreateInfo.pTessellationState = nullptr;
            depthPrepassPipelineCreateInfo.pViewportState = &viewportState;
            depthPrepassPipelineCreateInfo.pRasterizationState = &rasterizationState;
            depthPrepassPipelineCreateInfo.pMultisampleState = &multisampleState;
            depthPrepassPipelineCreateInfo.pDepthStencilState = &depthStencilStateWrite;
            depthPrepassPipelineCreateInfo.pColorBlendState = nullptr;
            depthPrepassPipelineCreateInfo.pDynamicState = &dynamicState;
            depthPrepassPipelineCreateInfo.layout = pipelineLayout;
            depthPrepassPipelineCreateInfo.renderPass = renderPass;
            depthPrepassPipelineCreateInfo.subpass = 0;
            depthPrepassPipelineCreateInfo.basePipelineHandle = VK_NULL_HANDLE;
            depthPrepassPipelineCreateInfo.basePipelineIndex = 0;

            if (VK_FAILED(vkCreateGraphicsPipelines(pDeviceContext->device, VK_NULL_HANDLE, 1, &depthPrepassPipelineCreateInfo, nullptr, &depthPrepassPipeline)))
            {
                printf("Vulkan depth prepass pipeline create failed\n");
                return false;
            }

            VkPipelineShaderStageCreateInfo shaderStages[] = { vertexStageInfo, forwardFragmentStageInfo, };
            VkGraphicsPipelineCreateInfo forwardPipelineCreateInfo{ VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO };
            forwardPipelineCreateInfo.flags = 0;
            forwardPipelineCreateInfo.stageCount = SIZEOF_ARRAY(shaderStages);
            forwardPipelineCreateInfo.pStages = shaderStages;
            forwardPipelineCreateInfo.pVertexInputState = &vertexInputState;
            forwardPipelineCreateInfo.pInputAssemblyState = &inputAssemblyState;
            forwardPipelineCreateInfo.pTessellationState = nullptr;
            forwardPipelineCreateInfo.pViewportState = &viewportState;
            forwardPipelineCreateInfo.pRasterizationState = &rasterizationState;
            forwardPipelineCreateInfo.pMultisampleState = &multisampleState;
            forwardPipelineCreateInfo.pDepthStencilState = &depthStencilStateCompare;
            forwardPipelineCreateInfo.pColorBlendState = &colorBlendState;
            forwardPipelineCreateInfo.pDynamicState = &dynamicState;
            forwardPipelineCreateInfo.layout = pipelineLayout;
            forwardPipelineCreateInfo.renderPass = renderPass;
            forwardPipelineCreateInfo.subpass = 1;
            forwardPipelineCreateInfo.basePipelineHandle = VK_NULL_HANDLE;
            forwardPipelineCreateInfo.basePipelineIndex = 0;

            if (VK_FAILED(vkCreateGraphicsPipelines(pDeviceContext->device, VK_NULL_HANDLE, 1, &forwardPipelineCreateInfo, nullptr, &forwardPipeline)))
            {
                printf("Vulkan forward pipeline create failed\n");
                return false;
            }

            vkDestroyShaderModule(pDeviceContext->device, depthOnlyFragmentShader, nullptr);
            vkDestroyShaderModule(pDeviceContext->device, forwardFragmentShader, nullptr);
            vkDestroyShaderModule(pDeviceContext->device, vertexShader, nullptr);
        }

        // Create scene data
        {
            uint32_t const maxSceneCount = 1;
            uint32_t const maxLightCount = 1;

            VkDescriptorPoolSize scenePoolSizes[] = {
                VkDescriptorPoolSize{ VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 1 * maxSceneCount },
            };

            VkDescriptorPoolCreateInfo sceneDescriptorPoolCreateInfo{ VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO };
            sceneDescriptorPoolCreateInfo.flags = 0;
            sceneDescriptorPoolCreateInfo.maxSets = maxSceneCount;
            sceneDescriptorPoolCreateInfo.poolSizeCount = SIZEOF_ARRAY(scenePoolSizes);
            sceneDescriptorPoolCreateInfo.pPoolSizes = scenePoolSizes;

            if (VK_FAILED(vkCreateDescriptorPool(pDeviceContext->device, &sceneDescriptorPoolCreateInfo, nullptr, &sceneDescriptorPool)))
            {
                printf("Vulkan scene descriptor pool create failed\n");
                return false;
            }

            VkDescriptorPoolSize lightPoolSizes[] = {
                VkDescriptorPoolSize{ VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 1 * maxLightCount },
            };

            VkDescriptorPoolCreateInfo lightDescriptorPoolCreateInfo{ VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO };
            lightDescriptorPoolCreateInfo.flags = 0;
            lightDescriptorPoolCreateInfo.maxSets = maxLightCount;
            lightDescriptorPoolCreateInfo.poolSizeCount = SIZEOF_ARRAY(lightPoolSizes);
            lightDescriptorPoolCreateInfo.pPoolSizes = lightPoolSizes;

            if (VK_FAILED(vkCreateDescriptorPool(pDeviceContext->device, &lightDescriptorPoolCreateInfo, nullptr, &lightDescriptorPool)))
            {
                printf("Vulkan light descriptor pool create failed\n");
                return false;
            }

            VkDescriptorSetAllocateInfo sceneDataSetAllocInfo{ VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO };
            sceneDataSetAllocInfo.descriptorPool = sceneDescriptorPool;
            sceneDataSetAllocInfo.descriptorSetCount = 1;
            sceneDataSetAllocInfo.pSetLayouts = &sceneDataSetLayout;

            VkDescriptorSet sceneDataSet = VK_NULL_HANDLE;
            if (VK_FAILED(vkAllocateDescriptorSets(pDeviceContext->device, &sceneDataSetAllocInfo, &sceneDataSet)))
            {
                printf("Vulkan descriptor set allocation failed\n");
                return false;
            }

            VkDescriptorSetAllocateInfo lightDataSetAllocInfo{ VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO };
            lightDataSetAllocInfo.descriptorPool = lightDescriptorPool;
            lightDataSetAllocInfo.descriptorSetCount = 1;
            lightDataSetAllocInfo.pSetLayouts = &lightDataSetLayout;

            VkDescriptorSet lightDataSet = VK_NULL_HANDLE;
            if (VK_FAILED(vkAllocateDescriptorSets(pDeviceContext->device, &lightDataSetAllocInfo, &lightDataSet)))
            {
                printf("Vulkan descriptor set allocation failed\n");
                return false;
            }

            scene = Scene(pDeviceContext, sceneDataSet, lightDataSet);
            scene.cameraTransform.position = glm::vec3(0.0F, 0.0F, -5.0F);
            scene.camera.type = CameraType::Perspective;
            scene.camera.perspective.aspectRatio = static_cast<float>(DefaultWindowWidth) / static_cast<float>(DefaultWindowHeight);
        }

        // Set up scene object data
        {
            Vertex vertices[] = {
                Vertex{ { -1.0F, 0.0F, -1.0F }, { 1.0F, 1.0F, 1.0F }, { 0.0F, 1.0F, 0.0F }, { 1.0F, 0.0F, 0.0F }, { 0.0F, 0.0F } },
                Vertex{ { -1.0F, 0.0F,  1.0F }, { 1.0F, 1.0F, 1.0F }, { 0.0F, 1.0F, 0.0F }, { 1.0F, 0.0F, 0.0F }, { 1.0F, 0.0F } },
                Vertex{ {  1.0F, 0.0F,  1.0F }, { 1.0F, 1.0F, 1.0F }, { 0.0F, 1.0F, 0.0F }, { 1.0F, 0.0F, 0.0F }, { 1.0F, 1.0F } },
                Vertex{ {  1.0F, 0.0F, -1.0F }, { 1.0F, 1.0F, 1.0F }, { 0.0F, 1.0F, 0.0F }, { 1.0F, 0.0F, 0.0F }, { 0.0F, 1.0F } },
            };

            uint32_t indices[] = {
                0, 1, 2,
                2, 3, 0
            };

            Mesh planeMesh{};
            if (!createMesh(pDeviceContext, planeMesh, vertices, SIZEOF_ARRAY(vertices), indices, SIZEOF_ARRAY(indices)))
            {
                printf("VK Renderer mesh create failed\n");
                return false;
            }

            Mesh suzanneMesh{};
            if (!loadOBJ(pDeviceContext, "data/assets/suzanne.obj", suzanneMesh))
            {
                printf("VK Renderer mesh load failed\n");
                return false;
            }

            Mesh cubeMesh{};
            if (!loadOBJ(pDeviceContext, "data/assets/cube.obj", cubeMesh))
            {
                printf("VK Renderer mesh load failed\n");
                return false;
            }

            Texture colorTexture{};
            Texture normalTexture{};
            if (!loadTexture(pDeviceContext, "data/assets/brickwall.jpg", colorTexture)
                || !loadTexture(pDeviceContext, "data/assets/brickwall_normal.jpg", normalTexture))
            {
                printf("VK Renderer texture loading failed\n");
                return false;
            }

            VkImageViewCreateInfo colorTextureViewCreateInfo{ VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO };
            colorTextureViewCreateInfo.flags = 0;
            colorTextureViewCreateInfo.image = colorTexture.handle;
            colorTextureViewCreateInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
            colorTextureViewCreateInfo.format = colorTexture.format;
            colorTextureViewCreateInfo.components.r = VK_COMPONENT_SWIZZLE_IDENTITY;
            colorTextureViewCreateInfo.components.g = VK_COMPONENT_SWIZZLE_IDENTITY;
            colorTextureViewCreateInfo.components.b = VK_COMPONENT_SWIZZLE_IDENTITY;
            colorTextureViewCreateInfo.components.a = VK_COMPONENT_SWIZZLE_IDENTITY;
            colorTextureViewCreateInfo.subresourceRange.baseMipLevel = 0;
            colorTextureViewCreateInfo.subresourceRange.levelCount = colorTexture.levels;
            colorTextureViewCreateInfo.subresourceRange.baseArrayLayer = 0;
            colorTextureViewCreateInfo.subresourceRange.layerCount = colorTexture.depthOrLayers;
            colorTextureViewCreateInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;

            VkImageViewCreateInfo normalTextureViewCreateInfo{ VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO };
            normalTextureViewCreateInfo.flags = 0;
            normalTextureViewCreateInfo.image = normalTexture.handle;
            normalTextureViewCreateInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
            normalTextureViewCreateInfo.format = normalTexture.format;
            normalTextureViewCreateInfo.components.r = VK_COMPONENT_SWIZZLE_IDENTITY;
            normalTextureViewCreateInfo.components.g = VK_COMPONENT_SWIZZLE_IDENTITY;
            normalTextureViewCreateInfo.components.b = VK_COMPONENT_SWIZZLE_IDENTITY;
            normalTextureViewCreateInfo.components.a = VK_COMPONENT_SWIZZLE_IDENTITY;
            normalTextureViewCreateInfo.subresourceRange.baseMipLevel = 0;
            normalTextureViewCreateInfo.subresourceRange.levelCount = normalTexture.levels;
            normalTextureViewCreateInfo.subresourceRange.baseArrayLayer = 0;
            normalTextureViewCreateInfo.subresourceRange.layerCount = normalTexture.depthOrLayers;
            normalTextureViewCreateInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;

            VkImageView colorTextureView = VK_NULL_HANDLE;
            VkImageView normalTextureView = VK_NULL_HANDLE;
            if (VK_FAILED(vkCreateImageView(pDeviceContext->device, &colorTextureViewCreateInfo, nullptr, &colorTextureView))
                || VK_FAILED(vkCreateImageView(pDeviceContext->device, &normalTextureViewCreateInfo, nullptr, &normalTextureView)))
            {
                throw std::runtime_error("VK Renderer texture view create failed");
            }

            VkDescriptorSetAllocateInfo objectDataSetAllocInfo{ VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO };
            objectDataSetAllocInfo.descriptorPool = scene.objectDescriptorPool;
            objectDataSetAllocInfo.descriptorSetCount = 1;
            objectDataSetAllocInfo.pSetLayouts = &objectDataSetLayout;

            VkDescriptorSet suzanneDataSet = VK_NULL_HANDLE;
            if (VK_FAILED(vkAllocateDescriptorSets(pDeviceContext->device, &objectDataSetAllocInfo, &suzanneDataSet)))
            {
                printf("Vulkan descriptor set allocation failed\n");
                return false;
            }

            Object suzanne = Object(pDeviceContext, suzanneDataSet, suzanneMesh, colorTextureView, normalTextureView);
            suzanne.transform.position = glm::vec3(-2.0F, 0.0F, 0.0F);

            VkDescriptorSet cubeDataSet = VK_NULL_HANDLE;
            if (VK_FAILED(vkAllocateDescriptorSets(pDeviceContext->device, &objectDataSetAllocInfo, &cubeDataSet)))
            {
                printf("Vulkan descriptor set allocation failed\n");
                return false;
            }

            Object cube = Object(pDeviceContext, cubeDataSet, cubeMesh, colorTextureView, normalTextureView);
            cube.transform.position = glm::vec3(2.0F, 0.0F, 0.0F);

            VkDescriptorSet planeDataSet = VK_NULL_HANDLE;
            if (VK_FAILED(vkAllocateDescriptorSets(pDeviceContext->device, &objectDataSetAllocInfo, &planeDataSet)))
            {
                printf("Vulkan descriptor set allocation failed\n");
                return false;
            }

            Object plane = Object(pDeviceContext, planeDataSet, planeMesh, colorTextureView, normalTextureView);
            plane.transform.position = glm::vec3(0.0F, -1.0F, 0.0F);
            plane.transform.scale = glm::vec3(5.0F, 5.0F, 5.0F);

            scene.meshes.push_back(suzanneMesh);
            scene.meshes.push_back(cubeMesh);
            scene.meshes.push_back(planeMesh);
            scene.textures.push_back(colorTexture);
            scene.textures.push_back(normalTexture);
            scene.textureViews.push_back(colorTextureView);
            scene.textureViews.push_back(normalTextureView);
            scene.objects.push_back(cube);
            scene.objects.push_back(suzanne);
            scene.objects.push_back(plane);
        }

        printf("Initialized Vulkan Renderer\n");
        return true;
    }

    void shutdown()
    {
        printf("Shutting down Vulkan Renderer\n");

        // Wait for the last frame.
        vkWaitForFences(pDeviceContext->device, 1, &commandsFinished, VK_TRUE, UINT64_MAX);

        // Destroy scene data
        scene.destroy();
        vkDestroyDescriptorPool(pDeviceContext->device, lightDescriptorPool, nullptr);
        vkDestroyDescriptorPool(pDeviceContext->device, sceneDescriptorPool, nullptr);

        // Destroy graphics pipeline & associated data
        vkDestroyPipeline(pDeviceContext->device, forwardPipeline, nullptr);
        vkDestroyPipeline(pDeviceContext->device, depthPrepassPipeline, nullptr);
        vkDestroyPipelineLayout(pDeviceContext->device, pipelineLayout, nullptr);

        vkDestroyDescriptorSetLayout(pDeviceContext->device, objectDataSetLayout, nullptr);
        vkDestroyDescriptorSetLayout(pDeviceContext->device, lightDataSetLayout, nullptr);
        vkDestroyDescriptorSetLayout(pDeviceContext->device, sceneDataSetLayout, nullptr);
        vkDestroySampler(pDeviceContext->device, textureSampler, nullptr);

        // Destroy GUI data
        ImGui_ImplVulkan_Shutdown();
        vkDestroyDescriptorPool(pDeviceContext->device, imguiDescriptorPool, nullptr);

        // Destroy framebuffers & render pass
        for (auto& framebuffer : framebuffers) {
            vkDestroyFramebuffer(pDeviceContext->device, framebuffer, nullptr);
        }
        vkDestroyRenderPass(pDeviceContext->device, renderPass, nullptr);

        // Destroy render targets
        vkDestroyImageView(pDeviceContext->device, depthStencilView, nullptr);
        depthStencilTexture.destroy();

        // Destroy command buffer & sync primitive
        pDeviceContext->destroyCommandBuffer(CommandQueueType::Direct, commandBuffer);
        pDeviceContext->destroyFence(commandsFinished);

        Renderer::destroyRenderDeviceContext(pDeviceContext);
        Renderer::shutdown();

        ImGui_ImplSDL2_Shutdown();
        SDL_DestroyWindow(pWindow);
        SDL_Quit();

        ImGui::DestroyContext();
    }

    void resize()
    {
        int width = 0;
        int height = 0;
        SDL_GetWindowSize(pWindow, &width, &height);
        uint32_t windowFlags = SDL_GetWindowFlags(pWindow);
        if (width == 0 || height == 0 || (windowFlags & SDL_WINDOW_MINIMIZED) != 0) {
            return;
        }

        printf("Window resized (%d x %d)\n", width, height);
        framebufferWidth = static_cast<uint32_t>(width);
        framebufferHeight = static_cast<uint32_t>(height);

        vkWaitForFences(pDeviceContext->device, 1, &commandsFinished, VK_TRUE, UINT64_MAX);

        // Destroy swap dependent resources
        {
            for (auto& framebuffer : framebuffers) {
                vkDestroyFramebuffer(pDeviceContext->device, framebuffer, nullptr);
            }
            framebuffers.clear();

            vkDestroyImageView(pDeviceContext->device, depthStencilView, nullptr);
            depthStencilTexture.destroy();
        }

        if (!pDeviceContext->resizeSwapResources(framebufferWidth, framebufferHeight))
        {
            printf("VK Renderer swap resource resize failed\n");
            isRunning = false;
        }

        // Recreate swap dependent resources
        {
            if (!pDeviceContext->createTexture(
                depthStencilTexture,
                VK_IMAGE_TYPE_2D,
                VK_FORMAT_D32_SFLOAT,
                VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT,
                VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
                framebufferWidth, framebufferHeight, 1
            ))
            {
                printf("Vulkan depth stencil texture create failed\n");
                isRunning = false;
            }

            VkImageViewCreateInfo depthStencilViewCreateInfo{ VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO };
            depthStencilViewCreateInfo.flags = 0;
            depthStencilViewCreateInfo.image = depthStencilTexture.handle;
            depthStencilViewCreateInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
            depthStencilViewCreateInfo.format = depthStencilTexture.format;
            depthStencilViewCreateInfo.components.r = VK_COMPONENT_SWIZZLE_IDENTITY;
            depthStencilViewCreateInfo.components.g = VK_COMPONENT_SWIZZLE_IDENTITY;
            depthStencilViewCreateInfo.components.b = VK_COMPONENT_SWIZZLE_IDENTITY;
            depthStencilViewCreateInfo.components.a = VK_COMPONENT_SWIZZLE_IDENTITY;
            depthStencilViewCreateInfo.subresourceRange.baseMipLevel = 0;
            depthStencilViewCreateInfo.subresourceRange.levelCount = depthStencilTexture.levels;
            depthStencilViewCreateInfo.subresourceRange.baseArrayLayer = 0;
            depthStencilViewCreateInfo.subresourceRange.layerCount = depthStencilTexture.depthOrLayers;
            depthStencilViewCreateInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT;

            if (VK_FAILED(vkCreateImageView(pDeviceContext->device, &depthStencilViewCreateInfo, nullptr, &depthStencilView)))
            {
                printf("Vulkan depth stencil view create failed\n");
                isRunning = false;
            }

            auto const& backbuffers = pDeviceContext->getBackbuffers();
            framebuffers.reserve(backbuffers.size());
            for (auto& buffer : backbuffers)
            {
                VkImageView attachments[] = { buffer.view, depthStencilView, };

                VkFramebufferCreateInfo framebufferCreateInfo{ VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO };
                framebufferCreateInfo.flags = 0;
                framebufferCreateInfo.renderPass = renderPass;
                framebufferCreateInfo.attachmentCount = SIZEOF_ARRAY(attachments);
                framebufferCreateInfo.pAttachments = attachments;
                framebufferCreateInfo.width = framebufferWidth;
                framebufferCreateInfo.height = framebufferHeight;
                framebufferCreateInfo.layers = 1;

                VkFramebuffer framebuffer = VK_NULL_HANDLE;
                vkCreateFramebuffer(pDeviceContext->device, &framebufferCreateInfo, nullptr, &framebuffer);
                assert(framebuffer != VK_NULL_HANDLE);
                framebuffers.push_back(framebuffer);
            }
        }

        // Set viewport & scissor
        float viewportWidth = static_cast<float>(framebufferWidth);
        float viewportHeight = static_cast<float>(framebufferHeight);
        viewport = VkViewport{ 0.0F, viewportHeight, viewportWidth, -1.0F * viewportHeight, 0.0F, 1.0F }; // viewport height hack is needed because of OpenGL / Vulkan viewport differences
        scissor = VkRect2D{ VkOffset2D{ 0, 0 }, VkExtent2D{ framebufferWidth, framebufferHeight, }};

        // Update camera aspect ratio
        scene.camera.perspective.aspectRatio = viewportWidth / viewportHeight;
    }

    void update()
    {
        // Tick frame timer
        frameTimer.tick();
        cpuUpdateTimer.reset();

        // Update window state
        SDL_Event event{};
        while (SDL_PollEvent(&event))
        {
            ImGui_ImplSDL2_ProcessEvent(&event);

            switch (event.type)
            {
            case SDL_QUIT:
                isRunning = false;
                break;
            case SDL_WINDOWEVENT: {
                switch (event.window.event)
                {
                case SDL_WINDOWEVENT_RESIZED:
                    resize();
                    break;
                default:
                    break;
                }
            } break;
            default:
                break;
            }
        }

        // Update scene data
        scene.update();

        cpuUpdateTimer.tick();

        // Record GUI state (not part of CPU update)
        static RunningAverage avgFrameTime(25);
        static RunningAverage avgCpuUpdate(25);
        static RunningAverage avgCpuRender(25);
        avgFrameTime.update(frameTimer.deltaTimeMS());
        avgCpuUpdate.update(cpuUpdateTimer.deltaTimeMS());
        avgCpuRender.update(cpuRenderTimer.deltaTimeMS());

        ImGui_ImplSDL2_NewFrame();
        ImGui_ImplVulkan_NewFrame();
        ImGui::NewFrame();

        if (ImGui::Begin("Vulkan Renderer Config"))
        {
            ImGui::SeparatorText("Statistics");
            ImGui::Text("Framebuffer size:    (%u, %u)", framebufferWidth, framebufferHeight);
            ImGui::Text("Frame time:          %10.2f ms", avgFrameTime.getAverage());
            ImGui::Text("- CPU update:        %10.2f ms", avgCpuUpdate.getAverage());
            ImGui::Text("- Command recording: %10.2f ms", avgCpuRender.getAverage());
            ImGui::Text("FPS:                 %10.2f fps", 1'000.0 / avgFrameTime.getAverage());

            // TODO(nemjit001): implement this
            ImGui::SeparatorText("Settings");
            ImGui::RadioButton("VSync Enabled", true);
            ImGui::RadioButton("VSync Disabled", false);
            ImGui::RadioButton("VSync Disabled with tearing", false);

            ImGui::SeparatorText("Scene");
            ImGui::DragFloat("Sun Azimuth", &scene.sunAzimuth, 1.0F, 0.0F, 360.0F);
            ImGui::DragFloat("Sun Zenith", &scene.sunZenith, 1.0F, -90.0F, 90.0F);
            ImGui::ColorEdit3("Sun Color", &scene.sunColor[0], ImGuiColorEditFlags_DisplayHex | ImGuiColorEditFlags_InputRGB);
            ImGui::ColorEdit3("Ambient Light", &scene.ambientLight[0], ImGuiColorEditFlags_DisplayHex | ImGuiColorEditFlags_InputRGB);

            glm::vec3 rotation = glm::degrees(glm::eulerAngles(scene.cameraTransform.rotation));
            ImGui::SeparatorText("Camera");
            ImGui::Text("FOV y:  %10.2f", scene.camera.perspective.FOVy);
            ImGui::Text("Z Near: %10.2f", scene.camera.perspective.zNear);
            ImGui::Text("Z Far:  %10.2f", scene.camera.perspective.zFar);
            ImGui::DragFloat3("Position", &scene.cameraTransform.position[0], 0.1F);
            ImGui::DragFloat3("Rotation", &rotation[0], 0.1F);
            scene.cameraTransform.rotation = glm::quat(glm::radians(rotation));

            // TODO(nemjit001): Implement per-object data editing
            // ImGui::SeparatorText("Object data");
            // ImGui::DragFloat("Specularity", &pObject->specularity, 0.01F, 0.0F, 1.0F);
        }
        ImGui::End();

        ImGui::Render();
    }

    void render()
    {
        // Await & start new frame
        vkWaitForFences(pDeviceContext->device, 1, &commandsFinished, VK_TRUE, UINT64_MAX);
        if (!pDeviceContext->newFrame()) {
            resize();
            return;
        }

        uint32_t backbufferIndex = pDeviceContext->getCurrentBackbufferIndex();
        vkResetFences(pDeviceContext->device, 1, &commandsFinished);
        vkResetCommandBuffer(commandBuffer, 0 /* no flags */);

        // Record frame commands
        {
            // Reset render timer
            cpuRenderTimer.reset();

            VkCommandBufferBeginInfo commandBeginInfo{ VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO };
            commandBeginInfo.flags = 0;
            commandBeginInfo.pInheritanceInfo = nullptr;

            if (VK_FAILED(vkBeginCommandBuffer(commandBuffer, &commandBeginInfo)))
            {
                printf("Vulkan command buffer begin failed\n");
                isRunning = false;
                return;
            }

            // Begin render pass
            VkClearValue clearValues[] = {
                VkClearValue{{ 0.1F, 0.1F, 0.1F, 1.0F }},
                VkClearValue{{ 1.0F, 0x00 }},
            };

            VkRenderPassBeginInfo renderPassBeginInfo{ VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO };
            renderPassBeginInfo.renderPass = renderPass;
            renderPassBeginInfo.framebuffer = framebuffers[backbufferIndex];
            renderPassBeginInfo.renderArea = VkRect2D{ VkOffset2D{ 0, 0 }, VkExtent2D{ framebufferWidth, framebufferHeight } };
            renderPassBeginInfo.clearValueCount = SIZEOF_ARRAY(clearValues);
            renderPassBeginInfo.pClearValues = clearValues;

            vkCmdBeginRenderPass(commandBuffer, &renderPassBeginInfo, VK_SUBPASS_CONTENTS_INLINE);

            vkCmdSetViewport(commandBuffer, 0, 1, &viewport);
            vkCmdSetScissor(commandBuffer, 0, 1, &scissor);
                
                // Bind depth prepass pipeline
                vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, depthPrepassPipeline);

                // Bind scene descriptor set
                vkCmdBindDescriptorSets(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, pipelineLayout, 0, 1, &scene.sceneDataSet, 0, nullptr);
                vkCmdBindDescriptorSets(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, pipelineLayout, 1, 1, &scene.lightDataSet, 0, nullptr);

                // Render objects in scene
                for (auto& object : scene.objects)
                {
                    // Bind object descriptor set
                    vkCmdBindDescriptorSets(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, pipelineLayout, 2, 1, &object.objectDataSet, 0, nullptr);

                    // Draw object mesh
                    VkBuffer vertexBuffers[] = { object.mesh.vertexBuffer.handle, };
                    VkDeviceSize offsets[] = { 0, };
                    vkCmdBindVertexBuffers(commandBuffer, 0, 1, vertexBuffers, offsets);
                    vkCmdBindIndexBuffer(commandBuffer, object.mesh.indexBuffer.handle, 0, VK_INDEX_TYPE_UINT32);
                    vkCmdDrawIndexed(commandBuffer, object.mesh.indexCount, 1, 0, 0, 0);
                }

            vkCmdNextSubpass(commandBuffer, VK_SUBPASS_CONTENTS_INLINE);

                // Bind forward pipeline
                vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, forwardPipeline);

                // Bind scene descriptor set
                vkCmdBindDescriptorSets(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, pipelineLayout, 0, 1, &scene.sceneDataSet, 0, nullptr);
                vkCmdBindDescriptorSets(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, pipelineLayout, 1, 1, &scene.lightDataSet, 0, nullptr);

                // Render objects in scene
                for (auto& object : scene.objects)
                {
                    // Bind object descriptor set
                    vkCmdBindDescriptorSets(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, pipelineLayout, 2, 1, &object.objectDataSet, 0, nullptr);

                    // Draw object mesh
                    VkBuffer vertexBuffers[] = { object.mesh.vertexBuffer.handle, };
                    VkDeviceSize offsets[] = { 0, };
                    vkCmdBindVertexBuffers(commandBuffer, 0, 1, vertexBuffers, offsets);
                    vkCmdBindIndexBuffer(commandBuffer, object.mesh.indexBuffer.handle, 0, VK_INDEX_TYPE_UINT32);
                    vkCmdDrawIndexed(commandBuffer, object.mesh.indexCount, 1, 0, 0, 0);
                }


            vkCmdNextSubpass(commandBuffer, VK_SUBPASS_CONTENTS_INLINE);

                // Draw GUI
                ImGui_ImplVulkan_RenderDrawData(ImGui::GetDrawData(), commandBuffer);

            vkCmdEndRenderPass(commandBuffer);

            if (VK_FAILED(vkEndCommandBuffer(commandBuffer)))
            {
                printf("Vulkan command buffer end failed\n");
                isRunning = false;
                return;
            }

            cpuRenderTimer.tick();
        }

        // Submit recorded commands
        VkPipelineStageFlags waitStages[] = {
            VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
        };

        VkSubmitInfo submitInfo{ VK_STRUCTURE_TYPE_SUBMIT_INFO };
        submitInfo.waitSemaphoreCount = 1;
        submitInfo.pWaitSemaphores = &pDeviceContext->swapAvailable;
        submitInfo.pWaitDstStageMask = waitStages;
        submitInfo.commandBufferCount = 1;
        submitInfo.pCommandBuffers = &commandBuffer;
        submitInfo.signalSemaphoreCount = 1;
        submitInfo.pSignalSemaphores = &pDeviceContext->swapReleased;

        if (VK_FAILED(vkQueueSubmit(pDeviceContext->directQueue, 1, &submitInfo, commandsFinished)))
        {
            printf("Vulkan queue submit failed\n");
            isRunning = false;
            return;
        }

        if (!pDeviceContext->present()) {
            resize();
        }
    }
} // namespace Engine

int main()
{
    if (!Engine::init())
    {
        Engine::shutdown();
        return 1;
    }

    while (Engine::isRunning)
    {
        Engine::update();
        Engine::render();
    }

    Engine::shutdown();
    return 0;
}
