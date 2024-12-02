#pragma once

#include "math.hpp"

/// @brief Simple TRS transform.
struct Transform
{
    /// @brief Calculate the transformation matrix for this transform.
    /// @return 
    glm::mat4 matrix() const;

    /// @brief Local forward vector.
    /// @return 
    glm::vec3 forward() const;

    /// @brief Local right vector.
    /// @return 
    glm::vec3 right() const;

    /// @brief Local up vector.
    /// @return 
    glm::vec3 up() const;

    static glm::vec3 forward(glm::mat4 const& transform);

    static glm::vec3 right(glm::mat4 const& transform);

    static glm::vec3 up(glm::mat4 const& transform);

    glm::vec3 position = glm::vec3(0.0F);
    glm::quat rotation = glm::quat(1.0F, 0.0F, 0.0F, 0.0F);
    glm::vec3 scale = glm::vec3(1.0F);
};
