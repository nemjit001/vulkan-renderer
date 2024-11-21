#include "scene.hpp"

SceneRef Scene::addCamera(Camera const& camera)
{
    SceneRef ref = static_cast<SceneRef>(cameras.size());
    cameras.push_back(camera);
    return ref;
}

SceneRef Scene::addMesh(Mesh const& mesh)
{
    SceneRef ref = static_cast<SceneRef>(meshes.size());
    meshes.push_back(mesh);
    return ref;
}

SceneRef Scene::addTexture(Texture const& texture)
{
    SceneRef ref = static_cast<SceneRef>(textures.size());
    textures.push_back(texture);
    return ref;
}

SceneRef Scene::addMaterial(Material const& material)
{
    SceneRef ref = static_cast<SceneRef>(materials.size());
    materials.push_back(material);
    return ref;
}

SceneRef Scene::addObject(SceneRef mesh, SceneRef material)
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

SceneRef Scene::createNode(std::string const& name, Transform const& transform)
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

void Scene::clear()
{
    nodes.count = 0;
    nodes.name.clear();
    nodes.transform.clear();
    nodes.cameraRef.clear();
    nodes.objectRef.clear();

    objects.count = 0;
    objects.meshRef.clear();
    objects.materialRef.clear();

    materials.clear();

    for (auto& texture : textures) {
        texture.destroy();
    }
    textures.clear();

    for (auto& mesh : meshes) {
        mesh.destroy();
    }
    meshes.clear();

    cameras.clear();
}
