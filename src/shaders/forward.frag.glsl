#version 450
#pragma shader_stage(fragment)

#extension GL_EXT_nonuniform_qualifier : enable

#include "utils.glsl.h"

layout(location = 0) in FS_IN
{
	vec4 i_Position;
	vec3 i_Color;
	vec2 i_TexCoord;
	mat3 i_TBN;
};

layout(location = 0) out vec4 f_FragColor;

//-- uniform data --//

layout(set = 0, binding = 0) uniform CAMERA_DATA
{
	vec3 position;
	mat4 matrix;
} camera;
layout(set = 0, binding = 1) uniform sampler2D textureMaps[];

layout(set = 1, binding = 0) uniform MATERIAL_DATA
{
	vec3 albedo;
	vec3 specular;
	uint albedoMapIndex;
	uint specularMapIndex;
	uint normalMapIndex;
} material;

layout(set = 2, binding = 0) uniform OBJECT_DATA
{
	mat4 model;
	mat4 normal;
} object;

void main()
{
	vec4 albedo = vec4(material.albedo, 1.0);
	vec3 specular = material.specular;
	vec3 normal = vec3(0, 0, 1);

	if (material.albedoMapIndex != ~0) {
		albedo = linear(texture(textureMaps[material.albedoMapIndex], i_TexCoord));
	}

	if (material.specularMapIndex != ~0) {
		specular = texture(textureMaps[material.albedoMapIndex], i_TexCoord).rgb;
	}

	if (material.normalMapIndex != ~0) {
		normal = texture(textureMaps[material.normalMapIndex], i_TexCoord).rgb;
		normal = 2.0 * normal - 1.0;
	}

	// TODO(nemjit001): Implment shading model
	vec3 N = normalize(i_TBN * normal);
	f_FragColor = albedo;
}
