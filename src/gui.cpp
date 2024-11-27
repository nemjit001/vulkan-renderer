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
            ImGui::DragFloat4("Rotation", &transform.rotation[0], 0.1F);
            ImGui::DragFloat3("Scale", &transform.scale[0], 0.1F);

            //ImGui::Text("Position: %8.2f %8.2f %8.2f", transform.position.x, transform.position.y, transform.position.z);
            //ImGui::Text("Rotation: %8.2f %8.2f %8.2f %8.2f", transform.rotation.x, transform.rotation.y, transform.rotation.z, transform.rotation.w);
            //ImGui::Text("Scale:    %8.2f %8.2f %8.2f", transform.scale.x, transform.scale.y, transform.scale.z);

            ImGui::SeparatorText("Scene Refs");
            ImGui::Text("Camera:   %d", scene.nodes.cameraRef[node]);
            ImGui::Text("Mesh:     %d", scene.nodes.meshRef[node]);
            ImGui::Text("Material: %d", scene.nodes.materialRef[node]);

            ImGui::SeparatorText("Children");
            for (auto const& child : scene.nodes.children[node]) {
                SceneTree(scene, child);
            }

            ImGui::TreePop();
        }
    }
} // namespace GUI
