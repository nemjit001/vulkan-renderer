#pragma once

#include "math.hpp"

/// @brief Simple TRS transform.
struct Transform
{
    /// @brief Calculate the transformation matrix for this transform.
    /// @return 
    glm::mat4 matrix() const;

    glm::vec3 forward() const;

    glm::vec3 right() const;

    glm::vec3 up() const;

    glm::vec3 position = glm::vec3(0.0F);
    glm::quat rotation = glm::quat(1.0F, 0.0F, 0.0F, 0.0F);
    glm::vec3 scale = glm::vec3(1.0F);
};
