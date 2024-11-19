
/// @brief The scene data for a shadow mapping pass contains a viewproject matrix for a light.
layout(set = 0, binding = 0) uniform SCENE_DATA_UNIFORM
{
	mat4 viewproject;
};

/// @brief Object uniform data is just the model transform.
layout(set = 1, binding = 0) uniform OBJECT_DATA_UNIFORM
{
	mat4 model;
};
