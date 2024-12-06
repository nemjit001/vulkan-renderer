#include "scene.hpp"

#include "camera.hpp"
#include "light.hpp"
#include "mesh.hpp"
#include "render_backend/texture.hpp"

glm::vec3 Sun::direction() const
{
    float const azimuthRad = glm::radians(azimuth);
    float const elevationRad = glm::radians(90.0F + zenith);

    return glm::normalize(glm::vec3(
        glm::cos(azimuthRad) * glm::sin(elevationRad),
        glm::cos(elevationRad),
        glm::sin(azimuthRad) * glm::sin(elevationRad)
    ));
}

SceneRef Scene::addCamera(Camera const& camera)
{
    SceneRef ref = static_cast<SceneRef>(cameras.size());
    cameras.push_back(camera);
    return ref;
}

SceneRef Scene::addMesh(std::shared_ptr<Mesh> mesh)
{
    SceneRef ref = static_cast<SceneRef>(meshes.size());
    meshes.push_back(mesh);
    return ref;
}

SceneRef Scene::addTexture(std::shared_ptr<Texture> texture)
{
    if (textures.size() + 1 > MaxTextures) {
        printf("Scene max textures reached\n");
        return RefUnused;
    }

    SceneRef ref = static_cast<SceneRef>(textures.size());
    textures.push_back(texture);
    return ref;
}

SceneRef Scene::addLight(Light const& light)
{
    SceneRef ref = static_cast<SceneRef>(lights.size());
    lights.push_back(light);
    return ref;
}

SceneRef Scene::addMaterial(Material const& material)
{
    SceneRef ref = static_cast<SceneRef>(materials.size());
    materials.push_back(material);
    return ref;
}

SceneRef Scene::createRootNode(std::string const& name, Transform const& transform)
{
    SceneRef ref = createNode(name, transform);
    rootNodes.emplace_back(ref);
    return ref;
}

SceneRef Scene::createChildNode(SceneRef const& parent, std::string const& name, Transform const& transform)
{
    assert(parent < nodes.count);
    assert(parent != RefUnused);

    SceneRef ref = createNode(name, transform);
    nodes.parentRef[ref] = parent;
    nodes.children[parent].emplace_back(ref);
    return ref;
}

void Scene::clear()
{
    nodes.count = 0;
    nodes.name.clear();
    nodes.transform.clear();
    nodes.cameraRef.clear();

    materials.clear();
    lights.clear();
    textures.clear();
    meshes.clear();

    cameras.clear();
    activeCamera = RefUnused;

    skybox.reset();
    sun = Sun{};
}

SceneRef Scene::createNode(std::string const& name, Transform const& transform)
{
    SceneRef ref = static_cast<SceneRef>(nodes.count);
    nodes.name.push_back(name);
    nodes.transform.push_back(transform);
    nodes.parentRef.emplace_back(RefUnused);
    nodes.cameraRef.push_back(RefUnused);
    nodes.meshRef.push_back(RefUnused);
    nodes.lightRef.push_back(RefUnused);
    nodes.materialRef.push_back(RefUnused);
    nodes.children.emplace_back();
    nodes.count++;

    assert(
        nodes.count == nodes.name.size()
        && nodes.count == nodes.transform.size()
        && nodes.count == nodes.cameraRef.size()
        && nodes.count == nodes.meshRef.size()
        && nodes.count == nodes.lightRef.size()
        && nodes.count == nodes.materialRef.size()
        && nodes.count == nodes.parentRef.size()
        && nodes.count == nodes.children.size()
    );

    return ref;
}
