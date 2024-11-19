
layout(set = 0, binding = 0) uniform SCENE_DATA_UNIFORM
{
	vec3 ambientLight;
	vec3 cameraPosition;
	mat4 viewproject;
};

layout(set = 1, binding = 0) uniform LIGHT_DATA_UNIFORM
{
	vec3 lightDirection;
	vec3 lightColor;
};

layout(set = 2, binding = 0) uniform OBJECT_DATA_UNIFORM
{
	mat4 model;
	mat4 normal;
	float specularity;
};

layout(set = 2, binding = 1) uniform sampler2D colorTexture;
layout(set = 2, binding = 2) uniform sampler2D normalTexture;
