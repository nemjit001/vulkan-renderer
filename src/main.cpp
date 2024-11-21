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
    uint32_t framebufferWidth = 0;
    uint32_t framebufferHeight = 0;

    Timer frameTimer{};
    Timer cpuUpdateTimer{};
    Timer cpuRenderTimer{};
    RenderDeviceContext* pDeviceContext = nullptr;

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

        // Set up scene
        {
            Camera camera{};
            camera.type = CameraType::Perspective;
            camera.perspective.FOVy = 60.0F;
            camera.perspective.aspectRatio = static_cast<float>(framebufferWidth) / static_cast<float>(framebufferHeight);
            camera.perspective.zNear = 0.01F;
            camera.perspective.zFar = 100.0F;
            SceneRef cameraRef = scene.addCamera(camera);

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
            scene.createNode("Cube", Transform{});
            scene.createNode("Suzanne", Transform{});
        }

        printf("Initialized Vulkan Renderer\n");
        return true;
    }

    void shutdown()
    {
        printf("Shutting down Vulkan Renderer\n");

        Renderer::destroyRenderDevice(pDeviceContext);
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

        // TODO(nemjit001): Handle swap resize in renderer
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

        // TODO(nemjit001): Update renderer scene state here (transforms, anims, etc.)

        cpuUpdateTimer.tick();
    }

    void render()
    {
        // Await & start new frame
        if (!pDeviceContext->newFrame()) {
            resize();
            return;
        }

        uint32_t backbufferIndex = pDeviceContext->getCurrentBackbufferIndex();

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
