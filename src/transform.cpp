#include "transform.hpp"

glm::mat4 Transform::matrix() const
{
    return glm::translate(glm::identity<glm::mat4>(), position)
        * glm::mat4_cast(rotation)
        * glm::scale(glm::identity<glm::mat4>(), scale);
}

glm::vec3 Transform::forward() const
{
    return glm::normalize(glm::vec3(glm::inverse(matrix())[2]));
}

glm::vec3 Transform::right() const
{
    return glm::normalize(glm::vec3(glm::inverse(matrix())[0]));
}

glm::vec3 Transform::up() const
{
    return glm::normalize(glm::vec3(glm::inverse(matrix())[1]));
}
