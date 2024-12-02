#pragma once

#include <cstdint>

#include "math.hpp"

/// @brief Virtual perspective camera.
struct PerspectiveCamera
{
    /// @brief Calculate the projection matrix for this camera.
    /// @return 
    glm::mat4 matrix() const;

    // Perspective camera data
    float FOVy = 60.0F;
    float aspectRatio = 1.0F;
    float zNear = 0.1F;
    float zFar = 100.0F;
};

/// @brief Virtual orthographic canera.
struct OrthographicCamera
{
    /// @brief Calculate the projection matrix for this camera.
    /// @return 
    glm::mat4 matrix() const;

    float xMag = 1.0F;
    float yMag = 1.0F;
    float zNear = 0.1F;
    float zFar = 100.0F;
};

/// @brief Camera type.
enum class CameraType : uint8_t
{
    Perspective = 0,
    Orthographic = 1,
};

/// @brief Camera tagged union, provides Virtual Camera interface.
struct Camera
{
    static Camera createPerspective(float FOVy, float aspect, float zNear, float zFar);

    static Camera createOrtho(float xMag, float yMag, float zNear, float zFar);

    /// @brief Calculate the projection matrix for this camera.
    /// @return 
    glm::mat4 matrix() const;

    CameraType type = CameraType::Perspective;
    union
    {
        PerspectiveCamera perspective;
        OrthographicCamera ortho;
    };
};
