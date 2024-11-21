#pragma once

#include <string>
#include <vector>

#include "assets.hpp"
#include "camera.hpp"
#include "mesh.hpp"
#include "renderer.hpp"
#include "transform.hpp"

constexpr glm::vec3 FORWARD = glm::vec3(0.0F, 0.0F, 1.0F);
constexpr glm::vec3 UP      = glm::vec3(0.0F, 1.0F, 0.0F);
constexpr glm::vec3 RIGHT   = glm::vec3(1.0F, 0.0F, 0.0F);

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
    SceneRef addCamera(Camera const& camera)
    {
        SceneRef ref = static_cast<SceneRef>(cameras.size());
        cameras.push_back(camera);
        return ref;
    }

    SceneRef addMesh(Mesh const& mesh)
    {
        SceneRef ref = static_cast<SceneRef>(meshes.size());
        meshes.push_back(mesh);
        return ref;
    }

    SceneRef addTexture(Texture const& texture)
    {
        SceneRef ref = static_cast<SceneRef>(textures.size());
        textures.push_back(texture);
        return ref;
    }

    SceneRef addMaterial(Material const& material)
    {
        SceneRef ref = static_cast<SceneRef>(materials.size());
        materials.push_back(material);
        return ref;
    }

    SceneRef addObject(SceneRef mesh, SceneRef material)
    {
        SceneRef ref = static_cast<SceneRef>(objects.count);
        objects.meshRef.push_back(mesh);
        objects.materialRef.push_back(material);
        objects.count++;

        assert(
            objects.count == objects.meshRef.size()
            && objects.count == objects.materialRef.size()
        );

        return ref;
    }

    SceneRef createNode(std::string const& name = "Node", Transform const& transform = Transform{})
    {
        SceneRef ref = static_cast<SceneRef>(nodes.count);
        nodes.name.push_back(name);
        nodes.transform.push_back(transform);
        nodes.cameraRef.push_back(RefUnused);
        nodes.objectRef.push_back(RefUnused);
        nodes.count++;

        assert(
            nodes.count == nodes.name.size()
            && nodes.count == nodes.transform.size()
            && nodes.count == nodes.cameraRef.size()
            && nodes.count == nodes.objectRef.size()
        );

        return ref;
    }

public:
    std::vector<Camera> cameras{};
    std::vector<Mesh> meshes{};
    std::vector<Texture> textures{};
    std::vector<Material> materials{};

    struct
    {
        uint32_t count = 0;
        std::vector<SceneRef> meshRef{};
        std::vector<SceneRef> materialRef{};
    } objects;

    struct
    {
        uint32_t count = 0;
        std::vector<std::string> name{};
        std::vector<Transform> transform{};
        std::vector<SceneRef> cameraRef{};
        std::vector<SceneRef> objectRef{};
    } nodes;
};
