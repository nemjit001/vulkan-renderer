#include "gui.hpp"

#include <imgui.h>

namespace GUI
{
    void SceneTree(Scene& scene, SceneRef const& node)
    {
        assert(node < scene.nodes.count);
        std::string label = scene.nodes.name[node] + "##" + std::to_string(node);

        if (ImGui::TreeNode(label.c_str()))
        {
            Transform& transform = scene.nodes.transform[node];
            ImGui::SeparatorText("Transform");
            ImGui::DragFloat3("Position", &transform.position[0], 0.1F);
            ImGui::DragFloat4("Rotation", &transform.rotation[0], 0.1F); // FIXME(nemjit001): some good quat rotation pls
            ImGui::DragFloat3("Scale", &transform.scale[0], 0.1F);

            ImGui::SeparatorText("Scene Refs");
            ImGui::Text("Parent:   %d", scene.nodes.parentRef[node]);
            ImGui::Text("Camera:   %d", scene.nodes.cameraRef[node]);
            ImGui::Text("Mesh:     %d", scene.nodes.meshRef[node]);
            ImGui::Text("Light:    %d", scene.nodes.lightRef[node]);
            ImGui::Text("Material: %d", scene.nodes.materialRef[node]);

            ImGui::SeparatorText("Children");
            for (auto const& child : scene.nodes.children[node]) {
                SceneTree(scene, child);
            }

            ImGui::TreePop();
        }
    }
} // namespace GUI
