#pragma once

#include "scene.hpp"

namespace GUI
{
    /// @brief Draw the scene node tree starting from a node ref.
    /// @param scene 
    /// @param node Start node.
    void SceneTree(Scene& scene, SceneRef const& node);
} // namespace GUI
