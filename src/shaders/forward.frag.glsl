#version 450
#pragma shader_stage(fragment)

#extension GL_EXT_nonuniform_qualifier : enable

#include "forward.uniforms.glsl.h"
#include "lighting.glsl.h"
#include "utils.glsl.h"

layout(location = 0) in FS_IN
{
	vec4 i_Position;
	vec3 i_Color;
	vec2 i_TexCoord;
	mat3 i_TBN;
};

layout(location = 0) out vec4 f_FragColor;

void main()
{
	// Gather material data
	vec4 albedo = vec4(material.albedo, 1.0);
	vec3 specular = material.specular;
	vec3 normal = vec3(0, 0, 1);

	if (material.albedoMapIndex != PARAMETER_UNUSED) {
		albedo = linear(texture(textureMaps[material.albedoMapIndex], i_TexCoord));
	}

	if (material.specularMapIndex != PARAMETER_UNUSED) {
		specular = texture(textureMaps[material.albedoMapIndex], i_TexCoord).rgb;
	}

	if (material.normalMapIndex != PARAMETER_UNUSED) {
		normal = texture(textureMaps[material.normalMapIndex], i_TexCoord).rgb;
		normal = 2.0 * normal - 1.0;
	}

	// Calculate fixed material params
	vec3 fragPosition = i_Position.xyz / i_Position.w;
	vec3 N = normalize(i_TBN * normal);
	vec3 V = normalize(camera.position - fragPosition);

	// Gather light influences
	vec3 outColor = vec3(0.0);
	for (uint lightIdx = 0; lightIdx < lights.length(); lightIdx++)
	{
		Light light = lights[lightIdx];
		vec3 lightColor = light.color;
		float shadowStrength = 0.0;
		vec3 L = vec3(0);

		// Calculate L vector
		if (light.type == LIGHT_TYPE_DIRECTIONAL) {
			L = normalize(-light.positionOrDirection);
		}
		else if (light.type == LIGHT_TYPE_POINT) {
			vec3 lightVec = light.positionOrDirection - fragPosition;
			lightColor /= dot(lightVec, lightVec); //< divide color strength by squared distance to light
			L = normalize(lightVec);
		}

		// Sample shadow maps if they are available
		if (light.type == LIGHT_TYPE_DIRECTIONAL && light.shadowMapIndex != PARAMETER_UNUSED) {
			vec4 shadowFragPos = light.lightSpaceTransform * i_Position;
			float shadowBias = max(0.05 * (1.0 - dot(N, L)), 0.005); //< Shadow bias in range [0.005, 005]
			shadowStrength = shadowStrengthDirectionalLight(shadowFragPos, shadowMaps[light.shadowMapIndex], shadowBias);
		}

		outColor += modelBlinnPhong(albedo.rgb, specular, lightColor, shadowStrength, N, L, V);
	}

	f_FragColor = vec4(outColor, albedo.a);
}
