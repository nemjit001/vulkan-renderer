#pragma once

#include <memory>
#include <string>
#include <vector>

#include "math.hpp"
#include "transform.hpp"

struct Camera;
class Mesh;
class Texture;

using SceneRef = uint32_t;
constexpr SceneRef RefUnused = ~0U;

/// @brief Material data, contains defaults and references to scene textures.
struct Material
{
    glm::vec3 defaultAlbedo     = glm::vec3(0.5F);
    glm::vec3 defaultSpecular   = glm::vec3(0.5F);
    SceneRef albedoTexture      = RefUnused;
    SceneRef normalTexture      = RefUnused;
    SceneRef specularTexture    = RefUnused;
};

/// @brief Optimized renderer scene structure, contains GPU friendly scene data stored in flat arrays.
class Scene
{
public:
    SceneRef addCamera(Camera const& camera);

    SceneRef addMesh(std::shared_ptr<Mesh> mesh);

    SceneRef addTexture(std::shared_ptr<Texture> texture);

    SceneRef addMaterial(Material const& material);

    SceneRef createRootNode(std::string const& name = "Node", Transform const& transform = Transform{});

    SceneRef createChildNode(SceneRef const& parent, std::string const& name = "Node", Transform const& transform = Transform{});

    void clear();

    bool empty() const;

private:
    SceneRef createNode(std::string const& name, Transform const& transform);

public:
    static constexpr uint32_t MaxTextures = 1024; //< Required for descriptor indexing in renderer

    SceneRef activeCamera = RefUnused; //< Reference to a scene node containing a camera reference
    std::vector<SceneRef> rootNodes{};

    std::vector<Camera> cameras{};
    std::vector<std::shared_ptr<Mesh>> meshes{};
    std::vector<std::shared_ptr<Texture>> textures{};
    std::vector<Material> materials{};

    struct
    {
        uint32_t count = 0;
        std::vector<std::string> name{};
        std::vector<Transform> transform{};
        std::vector<SceneRef> cameraRef{};
        std::vector<SceneRef> meshRef{};
        std::vector<SceneRef> materialRef{};
        std::vector<std::vector<SceneRef>> children{};
    } nodes;
};
