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
#include "renderer.hpp"
#include "render_backend.hpp"
#include "scene.hpp"
#include "timer.hpp"

namespace Engine
{
    constexpr char const* pWindowTitle = "Vulkan Renderer";
    constexpr uint32_t DefaultWindowWidth = 1600;
    constexpr uint32_t DefaultWindowHeight = 900;

    bool isRunning = true;
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

        // Set up scene
        {
            Camera camera{};
            camera.type = CameraType::Perspective;
            camera.perspective.FOVy = 60.0F;
            camera.perspective.aspectRatio = static_cast<float>(framebufferWidth) / static_cast<float>(framebufferHeight);
            camera.perspective.zNear = 0.01F;
            camera.perspective.zFar = 100.0F;
            SceneRef cameraRef = scene.addCamera(camera);
            SceneRef cameraNode = scene.createNode("Camera", Transform{ { 0.0F, 0.0F, -5.0F } });
            scene.nodes.cameraRef[cameraNode] = cameraRef;
            scene.activeCamera = cameraNode;

            Mesh cubeMesh{};
            if (!loadOBJ(pDeviceContext, "data/assets/cube.obj", cubeMesh)) {
                return false;
            }

            Mesh suzanneMesh{};
            if (!loadOBJ(pDeviceContext, "data/assets/suzanne.obj", suzanneMesh)) {
                return false;
            }

            SceneRef cubeMeshRef = scene.addMesh(cubeMesh);
            SceneRef suzanneMeshRef = scene.addMesh(suzanneMesh);

            Texture brickAlbedoTexture{};
            if (!loadTexture(pDeviceContext, "data/assets/brickwall.jpg", brickAlbedoTexture)
                || !brickAlbedoTexture.initDefaultView(VK_IMAGE_VIEW_TYPE_2D, VK_IMAGE_ASPECT_COLOR_BIT)) {
                return false;
            }

            Texture brickNormalTexture{};
            if (!loadTexture(pDeviceContext, "data/assets/brickwall_normal.jpg", brickNormalTexture)
                || !brickNormalTexture.initDefaultView(VK_IMAGE_VIEW_TYPE_2D, VK_IMAGE_ASPECT_COLOR_BIT)) {
                return false;
            }

            SceneRef brickAlbedoRef = scene.addTexture(brickAlbedoTexture);
            SceneRef brickNormalRef = scene.addTexture(brickNormalTexture);

            Material defaultMaterial{};
            defaultMaterial.defaultAlbedo = glm::vec3(1.0F, 0.0F, 0.0F);
            defaultMaterial.defaultSpecular = glm::vec3(0.0F, 0.0F, 0.0F);
            defaultMaterial.albedoTexture = brickAlbedoRef;
            defaultMaterial.normalTexture = brickNormalRef;
            SceneRef brickMaterialRef = scene.addMaterial(defaultMaterial);

            SceneRef cubeObjectRef = scene.addObject(cubeMeshRef, brickMaterialRef);
            SceneRef suzanneObjectRef = scene.addObject(suzanneMeshRef, brickMaterialRef);
            SceneRef cubeNode = scene.createNode("Cube", Transform{ { -2.0F, 0.0F, 0.0F } });
            SceneRef suzanneNode = scene.createNode("Suzanne", Transform{ { 2.0F, 0.0F, 0.0F } });

            // FIXME(nemjit001): fix this with a "normal" scene API (this is kinda gross).
            scene.nodes.objectRef[cubeNode] = cubeObjectRef;
            scene.nodes.objectRef[suzanneNode] = suzanneObjectRef;
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

    void resize()
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

        if (!pDeviceContext->resizeSwapResources(framebufferWidth, framebufferHeight)) {
            isRunning = false;
            return;
        }

        if (!pRenderer->onResize(framebufferWidth, framebufferHeight)) {
            isRunning = false;
            return;
        }

        printf("Window resized (%d x %d)\n", framebufferWidth, framebufferHeight);
    }

    void render()
    {
        if (!pDeviceContext->newFrame())
        {
            resize();
            return;
        }

        cpuRenderTimer.reset();
        pRenderer->render(scene);
        cpuRenderTimer.tick();

        if (!pDeviceContext->present()) {
            resize();
        }
        return;
    }

    void update()
    {
        // Tick frame timer
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

        ImGui_ImplSDL2_NewFrame();
        ImGui_ImplVulkan_NewFrame();
        ImGui::NewFrame();

        if (ImGui::Begin("Vulkan Renderer Config"))
        {
            ImGui::SeparatorText("Statistics");
            ImGui::Text("Frame time:        %10.2f ms", avgFrameTime.getAverage());
            ImGui::Text("- CPU update time: %10.2f ms", avgCPUUpdateTime.getAverage());
            ImGui::Text("- CPU render time: %10.2f ms", avgCPURenderTime.getAverage());
        }
        ImGui::End();

        ImGui::Render();

        // Update active camera aspect ratio
        SceneRef const activeCamera = scene.nodes.cameraRef[scene.activeCamera];
        scene.cameras[activeCamera].perspective.aspectRatio = static_cast<float>(framebufferWidth) / static_cast<float>(framebufferHeight);
        pRenderer->update(scene);
        cpuUpdateTimer.tick();

        Engine::render();
    }
} // namespace Engine

int main()
{
    if (!Engine::init())
    {
        Engine::shutdown();
        return 1;
    }

    while (Engine::isRunning) {
        Engine::update();
    }

    Engine::shutdown();
    return 0;
}
