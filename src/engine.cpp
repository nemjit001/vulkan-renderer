#include "engine.hpp"

#include <cassert>
#include <iostream>
#include <vector>

#define VK_NO_PROTOTYPES
#include <imgui.h>
#include <imgui_impl_sdl2.h>
#include <imgui_impl_vulkan.h>
#include <SDL.h>

#include "assets.hpp"
#include "camera.hpp"
#include "gui.hpp"
#include "light.hpp"
#include "math.hpp"
#include "mesh.hpp"
#include "renderer.hpp"
#include "render_backend.hpp"
#include "render_backend/buffer.hpp"
#include "render_backend/texture.hpp"
#include "scene.hpp"
#include "transform.hpp"

Engine::Engine()
{
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO(); (void)(io);
    io.IniFilename = nullptr;

    ImGui::StyleColorsDark();

    if (SDL_Init(SDL_INIT_VIDEO) != 0)
    {
        printf("SDL init failed: %s\n", SDL_GetError());
        m_running = false;
        return;
    }

    m_pEngineWindow = SDL_CreateWindow(pWindowTitle, SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED, DefaultWindowWidth, DefaultWindowHeight, SDL_WINDOW_RESIZABLE | SDL_WINDOW_VULKAN);
    if (m_pEngineWindow == nullptr)
    {
        printf("SDL window create failed: %s\n", SDL_GetError());
        m_running = false;
        return;
    }
    m_framebufferWidth = DefaultWindowWidth;
    m_framebufferHeight = DefaultWindowHeight;

    if (!ImGui_ImplSDL2_InitForVulkan(m_pEngineWindow))
    {
        printf("ImGui init for SDL2 failed\n");
        m_running = false;
        return;
    }

    if (!RenderBackend::init(m_pEngineWindow))
    {
        printf("VK Renderer render backend init failed\n");
        m_running = false;
        return;
    }

    m_pDeviceContext = RenderBackend::pickRenderDevice();
    if (m_pDeviceContext == nullptr)
    {
        printf("VK Renderer no render device available\n");
        m_running = false;
        return;
    }

    m_pRenderer = std::make_unique<ForwardRenderer>(m_pDeviceContext.get(), m_framebufferWidth, m_framebufferHeight);
    if (m_pRenderer == nullptr)
    {
        printf("VK Renderer renderer init failed\n");
        m_running = false;
        return;
    }

    // Set up default scene sun, skybox & camera
    m_scene.sun.zenith = 45.0F;
    m_scene.skybox = loadCubeMap(m_pDeviceContext.get(), {
        "data/assets/skybox/right.jpg",
        "data/assets/skybox/left.jpg",
        "data/assets/skybox/top.jpg",
        "data/assets/skybox/bottom.jpg",
        "data/assets/skybox/front.jpg",
        "data/assets/skybox/back.jpg",
    });

    if (m_scene.skybox == nullptr || !m_scene.skybox->initDefaultView(VK_IMAGE_VIEW_TYPE_CUBE, VK_IMAGE_ASPECT_COLOR_BIT))
    {
        printf("VK Renderer skybox load failed\n");
        m_running = false;
        return;
    }

    Camera camera = Camera::createPerspective(60.0F, static_cast<float>(m_framebufferWidth) / static_cast<float>(m_framebufferHeight), 1.0F, 5'000.0F);
    SceneRef cameraRef = m_scene.addCamera(camera);
    SceneRef cameraNode = m_scene.createRootNode("Camera", Transform{{ 0.0F, 50.0F, -5.0F }});
    m_scene.nodes.cameraRef[cameraNode] = cameraRef;
    m_scene.activeCamera = cameraNode;

    // Load scene files from disk
    if (!loadScene(m_pDeviceContext.get(), "data/assets/sponza/sponza.obj", m_scene))
    {
        printf("VK Renderer scene load failed\n");
        m_running = false;
        return;
    }

    m_cameraController.getActiveCamera(&m_scene);
    printf("Initialized Vulkan Renderer\n");
}

Engine::~Engine()
{
    printf("Shutting down Vulkan Renderer\n");
    m_pRenderer->awaitFrame();

    m_scene.clear();
    m_pRenderer.reset();
    m_pDeviceContext.reset();
    RenderBackend::shutdown();

    ImGui_ImplSDL2_Shutdown();
    SDL_DestroyWindow(m_pEngineWindow);
    SDL_Quit();

    ImGui::DestroyContext();
}

void Engine::onResize()
{
    int width = 0;
    int height = 0;
    SDL_GetWindowSize(m_pEngineWindow, &width, &height);
    uint32_t windowFlags = SDL_GetWindowFlags(m_pEngineWindow);
    if (width == 0 || height == 0 || (windowFlags & SDL_WINDOW_MINIMIZED) != 0) {
        return;
    }

    m_framebufferWidth = static_cast<uint32_t>(std::max(width, 1));
    m_framebufferHeight = static_cast<uint32_t>(std::max(height, 1));
    m_pRenderer->awaitFrame();

    if (!m_pDeviceContext->resizeSwapResources(m_framebufferWidth, m_framebufferHeight)
        || !m_pRenderer->onResize(m_framebufferWidth, m_framebufferHeight)) {
        m_running = false;
        return;
    }

    printf("Window resized (%d x %d)\n", m_framebufferWidth, m_framebufferHeight);
}

void Engine::update()
{
    // Await frame start
    m_pRenderer->awaitFrame();
    m_frameTimer.tick();

    // Get last update times
    static RunningAverage avgFrameTime(25);
    static RunningAverage avgCPUUpdateTime(25);
    static RunningAverage avgCPURenderTime(25);
    avgFrameTime.update(m_frameTimer.deltaTimeMS());
    avgCPUUpdateTime.update(m_cpuUpdateTimer.deltaTimeMS());
    avgCPURenderTime.update(m_cpuRenderTimer.deltaTimeMS());

    m_cpuUpdateTimer.reset();

    // Update window state & handle input
    SDL_Event event{};
    while (SDL_PollEvent(&event))
    {
        ImGui_ImplSDL2_ProcessEvent(&event);

        switch (event.type)
        {
        case SDL_QUIT:
            m_running = false;
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
            }
            break;
        case SDL_MOUSEMOTION:
            m_inputManager.setMouseDelta({ (float)event.motion.xrel / (0.5F * (float)m_framebufferWidth), (float)event.motion.yrel / (0.5F * (float)m_framebufferHeight) });
            break;
        case SDL_KEYUP:
        case SDL_KEYDOWN:
            m_inputManager.setKeyState(event.key.keysym.scancode, event.type == SDL_KEYDOWN);
            break;
        default:
            break;
        }
    }

    // Get active camera state
    SceneRef const& activeCameraRef = m_scene.nodes.cameraRef[m_scene.activeCamera];
    Transform const& activeCameraTransform = m_scene.nodes.transform[m_scene.activeCamera];
    glm::vec3 const camPosition = activeCameraTransform.position;
    glm::vec3 const camForward = activeCameraTransform.forward();
    glm::vec3 const camRight = activeCameraTransform.right();
    glm::vec3 const camUp = activeCameraTransform.up();
    Camera& activeCamera = m_scene.cameras[activeCameraRef];
    activeCamera.perspective.aspectRatio = static_cast<float>(m_framebufferWidth) / static_cast<float>(m_framebufferHeight);

    // Draw GUI
    ImGui_ImplSDL2_NewFrame();
    ImGui_ImplVulkan_NewFrame();
    ImGui::NewFrame();

    if (ImGui::Begin("Vulkan Renderer Config"))
    {
        ImGui::SeparatorText("Controls");
        ImGui::Text("Exit renderer            [Escape]");
        ImGui::Text("Enable Camera Controller [Space]");
        ImGui::Text(" - Camera movement       [WASD]");
        ImGui::Text(" - Camera look           [Mouse]");

        ImGui::SeparatorText("Status");
        ImGui::Text("Framebuffer resolution:    %u x %u", m_framebufferWidth, m_framebufferHeight);
        ImGui::Text("Camera Controller Enabled: %s", m_captureInput ? "yes" : "no");

        ImGui::SeparatorText("Statistics");
        ImGui::Text("Frame time:        %10.2f ms", avgFrameTime.getAverage());
        ImGui::Text("- CPU update time: %10.2f ms", avgCPUUpdateTime.getAverage());
        ImGui::Text("- CPU render time: %10.2f ms", avgCPURenderTime.getAverage());

        ImGui::SeparatorText("Sun");
        ImGui::DragFloat("Azimuth", &m_scene.sun.azimuth, 1.0F, 0.0F, 360.0F);
        ImGui::DragFloat("Zenith", &m_scene.sun.zenith, 1.0F, 0.01F, 89.9F);
        if (ImGui::TreeNode("Color settings"))
        {
            ImGui::ColorPicker3("Color", &m_scene.sun.color[0], ImGuiColorEditFlags_DisplayHex | ImGuiColorEditFlags_DisplayRGB);
            ImGui::ColorPicker3("Ambient", &m_scene.sun.ambient[0], ImGuiColorEditFlags_DisplayHex | ImGuiColorEditFlags_DisplayRGB);
            ImGui::TreePop();
        }

        ImGui::SeparatorText("Camera");
        ImGui::Text("Position: %8.2f %8.2f %8.2f", camPosition.x, camPosition.y, camPosition.z);
        ImGui::Text("Forward:  %8.2f %8.2f %8.2f", camForward.x, camForward.y, camForward.z);
        ImGui::Text("Right:    %8.2f %8.2f %8.2f", camRight.x, camRight.y, camRight.z);
        ImGui::Text("Up:       %8.2f %8.2f %8.2f", camUp.x, camUp.y, camUp.z);
        ImGui::DragFloat("FOV Y", &activeCamera.perspective.FOVy, 1.0F, 20.0F, 120.0F);
        ImGui::DragFloat("Z Near", &activeCamera.perspective.zNear, 1.0F, 0.0F, 1000.0F);
        ImGui::DragFloat("Z Far", &activeCamera.perspective.zFar, 1.0F, 0.0F, 10000.0F);

        ImGui::SeparatorText("Scene data");
        ImGui::Text("Meshes:    %d", m_scene.meshes.size());
        ImGui::Text("Textures:  %d", m_scene.textures.size());
        ImGui::Text("Materials: %d", m_scene.materials.size());
        ImGui::Text("Nodes:     %d", m_scene.nodes.count);

        ImGui::SeparatorText("Scene tree");
        for (auto const& root : m_scene.rootNodes) {
            GUI::SceneTree(m_scene, root);
        }
    }
    ImGui::End();

    ImGui::Render();

    // Handle inputs
    if (m_inputManager.isFirstPressed(SDL_SCANCODE_ESCAPE)) {
        m_running = false;
    }

    if (m_inputManager.isFirstPressed(SDL_SCANCODE_SPACE)) {
        m_captureInput = !m_captureInput;
        SDL_SetRelativeMouseMode(m_captureInput ? SDL_TRUE : SDL_FALSE);
    }

    if (m_captureInput) {
        m_cameraController.update(m_inputManager, m_frameTimer.deltaTimeMS());
    }

    // Update subsystems
    m_pRenderer->update(m_scene);
    m_inputManager.update();
    m_cpuUpdateTimer.tick();

    Engine::render();
}

void Engine::render()
{
    if (!m_pDeviceContext->newFrame())
    {
        Engine::onResize();
        return;
    }

    m_cpuRenderTimer.reset();
    m_pRenderer->render(m_scene);
    m_cpuRenderTimer.tick();

    if (!m_pDeviceContext->present()) {
        Engine::onResize();
    }
    return;
}

bool Engine::isRunning()
{
    return m_running;
}
