#include "transform.hpp"

glm::mat4 Transform::matrix() const
{
    return glm::translate(glm::identity<glm::mat4>(), position)
        * glm::mat4_cast(rotation)
        * glm::scale(glm::identity<glm::mat4>(), scale);
}

glm::vec3 Transform::forward() const
{
    return glm::vec3(matrix() * glm::vec4(FORWARD, 0.0));
}

glm::vec3 Transform::right() const
{
    return glm::vec3(matrix() * glm::vec4(RIGHT, 0.0));
}

glm::vec3 Transform::up() const
{
    return glm::vec3(matrix() * glm::vec4(UP, 0.0));
}
