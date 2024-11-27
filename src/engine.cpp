#include "engine.hpp"

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
#include "camera.hpp"
#include "gui.hpp"
#include "math.hpp"
#include "mesh.hpp"
#include "renderer.hpp"
#include "render_backend.hpp"
#include "scene.hpp"
#include "timer.hpp"
#include "transform.hpp"

namespace Engine
{
    constexpr char const* pWindowTitle = "Vulkan Renderer";
    constexpr uint32_t DefaultWindowWidth = 1600;
    constexpr uint32_t DefaultWindowHeight = 900;

    bool running = true;
    SDL_Window* pWindow = nullptr;
    uint32_t framebufferWidth = 0;
    uint32_t framebufferHeight = 0;

    Timer frameTimer{};
    Timer cpuUpdateTimer{};
    Timer cpuRenderTimer{};
    RenderDeviceContext* pDeviceContext = nullptr;
    IRenderer* pRenderer = nullptr;
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
        framebufferWidth = DefaultWindowWidth;
        framebufferHeight = DefaultWindowHeight;

        if (!ImGui_ImplSDL2_InitForVulkan(pWindow))
        {
            printf("ImGui init for SDL2 failed\n");
            return false;
        }

        if (!RenderBackend::init(pWindow))
        {
            printf("VK Renderer render backend init failed\n");
            return false;
        }

        pDeviceContext = RenderBackend::pickRenderDevice();
        if (pDeviceContext == nullptr)
        {
            printf("VK Renderer no render device available\n");
            return false;
        }

        pRenderer = new ForwardRenderer(pDeviceContext, framebufferWidth, framebufferHeight);
        if (pRenderer == nullptr)
        {
            printf("VK Renderer renderer init failed\n");
            return false;
        }

        // Set up default scene camera
        Camera camera{};
        camera.type = CameraType::Perspective;
        camera.perspective.FOVy = 60.0F;
        camera.perspective.aspectRatio = static_cast<float>(framebufferWidth) / static_cast<float>(framebufferHeight);
        camera.perspective.zNear = 0.01F;
        camera.perspective.zFar = 1000.0F;
        SceneRef cameraRef = scene.addCamera(camera);
        SceneRef cameraNode = scene.createRootNode("Camera", Transform{ { 0.0F, 50.0F, -5.0F } });
        scene.nodes.cameraRef[cameraNode] = cameraRef;
        scene.activeCamera = cameraNode;

        // Load scene files from disk
        if (!loadScene(pDeviceContext, "data/assets/Sponza/sponza.obj", scene))
        {
            printf("VK Renderer scene load failed\n");
            return false;
        }

        printf("Initialized Vulkan Renderer\n");
        return true;
    }

    void shutdown()
    {
        printf("Shutting down Vulkan Renderer\n");
        pRenderer->awaitFrame();

        scene.clear();
        delete pRenderer;

        RenderBackend::destroyRenderDevice(pDeviceContext);
        RenderBackend::shutdown();

        ImGui_ImplSDL2_Shutdown();
        SDL_DestroyWindow(pWindow);
        SDL_Quit();

        ImGui::DestroyContext();
    }

    void onResize()
    {
        int width = 0;
        int height = 0;
        SDL_GetWindowSize(pWindow, &width, &height);
        uint32_t windowFlags = SDL_GetWindowFlags(pWindow);
        if (width == 0 || height == 0 || (windowFlags & SDL_WINDOW_MINIMIZED) != 0) {
            return;
        }

        framebufferWidth = static_cast<uint32_t>(std::max(width, 1));
        framebufferHeight = static_cast<uint32_t>(std::max(height, 1));
        pRenderer->awaitFrame();

        if (!pDeviceContext->resizeSwapResources(framebufferWidth, framebufferHeight)
            || !pRenderer->onResize(framebufferWidth, framebufferHeight)) {
            running = false;
            return;
        }

        printf("Window resized (%d x %d)\n", framebufferWidth, framebufferHeight);
    }

    void update()
    {
        pRenderer->awaitFrame();
        frameTimer.tick();

        // Get last update times
        static RunningAverage avgFrameTime(25);
        static RunningAverage avgCPUUpdateTime(25);
        static RunningAverage avgCPURenderTime(25);
        avgFrameTime.update(frameTimer.deltaTimeMS());
        avgCPUUpdateTime.update(cpuUpdateTimer.deltaTimeMS());
        avgCPURenderTime.update(cpuRenderTimer.deltaTimeMS());

        cpuUpdateTimer.reset();

        // Update window state
        SDL_Event event{};
        while (SDL_PollEvent(&event))
        {
            ImGui_ImplSDL2_ProcessEvent(&event);

            switch (event.type)
            {
            case SDL_QUIT:
                running = false;
                break;
            case SDL_WINDOWEVENT: {
                switch (event.window.event)
                {
                case SDL_WINDOWEVENT_RESIZED:
                    Engine::onResize();
                    break;
                default:
                    break;
                }
            } break;
            default:
                break;
            }
        }

        ImGui_ImplSDL2_NewFrame();
        ImGui_ImplVulkan_NewFrame();
        ImGui::NewFrame();

        if (ImGui::Begin("Vulkan Renderer Config"))
        {
            ImGui::SeparatorText("Status");
            ImGui::Text("Framebuffer resolution: %u x %u", framebufferWidth, framebufferHeight);

            ImGui::SeparatorText("Statistics");
            ImGui::Text("Frame time:        %10.2f ms", avgFrameTime.getAverage());
            ImGui::Text("- CPU update time: %10.2f ms", avgCPUUpdateTime.getAverage());
            ImGui::Text("- CPU render time: %10.2f ms", avgCPURenderTime.getAverage());

            ImGui::SeparatorText("Scene data");
            ImGui::Text("Meshes:    %d", scene.meshes.size());
            ImGui::Text("Textures:  %d", scene.textures.size());
            ImGui::Text("Materials: %d", scene.materials.size());
            ImGui::Text("Nodes:     %d", scene.nodes.count);

            ImGui::SeparatorText("Scene tree");
            for (auto const& root : scene.rootNodes) {
                GUI::SceneTree(scene, root);
            }
        }
        ImGui::End();

        ImGui::Render();

        // Update active camera aspect ratio
        SceneRef const activeCamera = scene.nodes.cameraRef[scene.activeCamera];
        scene.cameras[activeCamera].perspective.aspectRatio = static_cast<float>(framebufferWidth) / static_cast<float>(framebufferHeight);

        // Update scene & renderer
        pRenderer->update(scene);
        cpuUpdateTimer.tick();

        Engine::render();
    }

    void render()
    {
        if (!pDeviceContext->newFrame())
        {
            Engine::onResize();
            return;
        }

        cpuRenderTimer.reset();
        pRenderer->render(scene);
        cpuRenderTimer.tick();

        if (!pDeviceContext->present()) {
            Engine::onResize();
        }
        return;
    }

    bool isRunning()
    {
        return running;
    }
} // namespace Engine
