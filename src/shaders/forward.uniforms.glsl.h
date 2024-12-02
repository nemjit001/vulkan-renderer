#ifndef FORWARD_UNIFORMS_GLSL_H
#define FORWARD_UNIFORMS_GLSL_H

//-- forward pipeline uniform data --//

#define PARAMETER_UNUSED		(~0)

#define LIGHT_TYPE_UNDEFINED	(0)
#define LIGHT_TYPE_DIRECTIONAL	(1)
#define LIGHT_TYPE_POINT		(2)

struct Camera
{
	vec3 position;
	mat4 matrix;
};

struct SunLight
{
	vec3 direction;
	vec3 color;
	mat4 lightSpaceTransform;
};

struct Light
{
	uint type;
	vec3 color;
	vec3 positionOrDirection;
};

struct Material
{
	vec3 albedo;
	vec3 specular;
	uint albedoMapIndex;
	uint specularMapIndex;
	uint normalMapIndex;
};

struct Object
{
	mat4 model;
	mat4 normal;
};

//-- Scene uniforms --//
layout(set = 0, binding = 0) uniform CAMERA_DATA { Camera camera; };
layout(set = 0, binding = 1) uniform SUN_LIGHT_DATA { SunLight sun; };
layout(set = 0, binding = 2) buffer LIGHT_BUFFER { Light lights[]; };
layout(set = 0, binding = 3) uniform sampler2D textureMaps[];

//-- Material uniforms --//
layout(set = 1, binding = 0) uniform MATERIAL_DATA { Material material; };

//-- Object uniforms --//
layout(set = 2, binding = 0) uniform OBJECT_DATA { Object object; };

#endif  // FORWARD_UNIFORMS_GLSL_H
