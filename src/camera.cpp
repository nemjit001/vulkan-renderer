#include "camera.hpp"

glm::mat4 PerspectiveCamera::matrix() const
{
    return glm::perspective(glm::radians(FOVy), aspectRatio, zNear, zFar);
}

glm::mat4 OrthographicCamera::matrix() const
{
    return glm::ortho(
        -0.5F * xMag, 0.5F * xMag,
        -0.5F * yMag, 0.5F * yMag,
        zNear, zFar
    );
}

glm::mat4 Camera::matrix() const
{
    switch (type)
    {
    case CameraType::Perspective:
        return perspective.matrix();
    case CameraType::Orthographic:
        return ortho.matrix();
    default:
        break;
    }

    return glm::identity<glm::mat4>();
}
