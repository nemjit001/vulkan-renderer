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

Camera Camera::createPerspective(float FOVy, float aspect, float zNear, float zFar)
{
    Camera cam{};
    cam.type = CameraType::Perspective;
    cam.perspective.FOVy = FOVy;
    cam.perspective.aspectRatio = aspect;
    cam.perspective.zNear = zNear;
    cam.perspective.zFar = zFar;

    return cam;
}

Camera Camera::createOrtho(float xMag, float yMag, float zNear, float zFar)
{
    Camera cam{};
    cam.type = CameraType::Orthographic;
    cam.ortho.xMag = xMag;
    cam.ortho.yMag = yMag;
    cam.ortho.zNear = zNear;
    cam.ortho.zFar = zFar;

    return cam;
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
