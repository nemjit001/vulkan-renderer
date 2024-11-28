#pragma once

#define GLM_FORCE_DEPTH_ZERO_TO_ONE
#define GLM_FORCE_RIGHT_HANDED
#define GLM_FORCE_RADIANS

#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>
#include <glm/gtc/matrix_transform.hpp>

constexpr glm::vec3 FORWARD = glm::vec3(0.0F, 0.0F, 1.0F);
constexpr glm::vec3 UP		= glm::vec3(0.0F, 1.0F, 0.0F);
constexpr glm::vec3 RIGHT	= glm::vec3(1.0F, 0.0F, 0.0F);
