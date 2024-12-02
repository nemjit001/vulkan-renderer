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
	vec3 uvw = 0.5 + 0.5 * (fragPos.xyz / fragPos.w); //< projected frag position in range [0,1]
	float shadowDepth = texture(shadowMap, uvw.xy).r;
	float adjustedDepth = uvw.z - shadowBias;
	return adjustedDepth > shadowDepth ? 1.0 : 0.0;
}

#endif  // UTILS_GLSL_H
