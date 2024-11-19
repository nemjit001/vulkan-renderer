
/// @brief Scene data for a forward pass requires a camera matrix + position (for lighting calc in fragment shader).
layout(set = 0, binding = 0) uniform SCENE_DATA_UNIFORM
{
	vec3 cameraPosition;
	mat4 viewproject;
};

/// @brief This is per-light data that is bound CPU-side.
/// The lightSpaceTransform is used for shadow calculation.
layout(set = 1, binding = 0) uniform LIGHT_DATA_UNIFORM
{
	vec3 lightDirection;
	vec3 lightColor;
	vec3 ambient;
	mat4 lightSpaceTransform;
};

/// @brief This is per-object data. Currently material data & maps are linked one-to-one to objects.
layout(set = 2, binding = 0) uniform OBJECT_DATA_UNIFORM
{
	mat4 model;
	mat4 normal;
	float specularity;
};

layout(set = 2, binding = 1) uniform sampler2D colorTexture;
layout(set = 2, binding = 2) uniform sampler2D normalTexture;
