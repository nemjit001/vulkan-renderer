#ifndef UTILS_GLSL_H
#define UTILS_GLSL_H

#define GAMMA		2.2
#define INV_GAMMA	1.0 / GAMMA

/// @brief Convert linear color to gamma-space, maintaining alpha.
/// @param color 
/// @return 
vec4 gamma(vec4 color)
{
	return vec4(pow(color.rgb, vec3(INV_GAMMA)), color.a);
}

/// @brief Convert gamma-space color to linear color, maintainging alpha.
/// @param color 
/// @return 
vec4 linear(vec4 color)
{
	return vec4(pow(color.rgb, vec3(GAMMA)), color.a);
}

/// @brief Read a shadow map using PCF, comparing against a fragment depth value.
/// @param depth Fragment depth to compare against.
/// @param shadowMap 
/// @param uv 
/// @return A shadow percentage in range [0, 1]
float readShadowMapPCF(float depth, sampler2D shadowMap, vec2 uv)
{
	vec2 dUV = 1.0 / textureSize(shadowMap, 0);
	float shadow = 0.0;
	float weight = 0.0;

	const int kernelRadius = 4; //< 16x filtering box blur
	for (int y = -kernelRadius; y <= kernelRadius; y++)
	{
		for (int x = -kernelRadius; x <= kernelRadius; x++)
		{
			shadow += depth > texture(shadowMap, uv + dUV * vec2(x, y)).r ? 1.0 : 0.0;
			weight += 1.0;
		}
	}

	shadow /= weight;
	return shadow;
}

/// @brief Calculate shadow bias.
/// @param N normal vector.
/// @param L light vector.
/// @return 
float shadowBias(vec3 N, vec3 L)
{
	return max(0.05 * (1.0 - dot(N, L)), 0.005); //< Shadow bias in range [0.005, 005]
}

/// @brief Simple shadow mapping implementation.
/// @param fragPos Fragment position in light space.
/// @param shadowMap Shadow map associated with the light.
/// @param shadowBias Shadow bias to apply.
/// @return A float representing shadow strength.
float shadowStrength(vec4 fragPos, sampler2D shadowMap, float shadowBias)
{
	vec3 projected = fragPos.xyz / fragPos.w;
	vec3 uvw = 0.5 + 0.5 * projected;
	float adjustedDepth = projected.z - shadowBias;
	return readShadowMapPCF(adjustedDepth, shadowMap, uvw.xy);
}

#endif  // UTILS_GLSL_H
