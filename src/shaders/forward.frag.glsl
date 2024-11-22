#version 450
#pragma shader_stage(fragment)

#extension GL_EXT_nonuniform_qualifier : enable

#define GAMMA		2.2
#define INV_GAMMA	1.0 / GAMMA

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
	vec3 albedo = material.albedo;
	vec3 specular = material.specular;
	vec3 normal = vec3(0, 0, 1);

	if (material.albedoMapIndex != ~0) {
		albedo = pow(texture(textureMaps[material.albedoMapIndex], i_TexCoord).rgb, vec3(GAMMA));
	}

	if (material.specularMapIndex != ~0) {
		specular = pow(texture(textureMaps[material.specularMapIndex], i_TexCoord).rgb, vec3(GAMMA));
	}

	if (material.normalMapIndex != ~0) {
		normal = texture(textureMaps[material.normalMapIndex], i_TexCoord).rgb;
		normal = 2.0 * normal - 1.0;
	}

	vec3 N = normalize(i_TBN * normal);
	f_FragColor = vec4(0.5 + 0.5 * N, 1.0);
}
