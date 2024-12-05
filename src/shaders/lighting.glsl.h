#ifndef LIGHTING_GLSL_H
#define LIGHTING_GLSL_H

/// @brief Blinn-Phong lighting model.
/// @param albedo Albedo color of material.
/// @param specular Specularity of material.
/// @param lightColor Light color.
/// @param shadowStength Shadow strength of light.
/// @param N Normal.
/// @param L Light vector, normalized.
/// @param V View vector, normalized.
/// @return Lighting model output color.
vec3 modelBlinnPhong(vec3 albedo, vec3 specular, vec3 lightColor, vec3 ambientLight, float shadowStength, vec3 N, vec3 L, vec3 V)
{
	vec3 H = normalize(V + L);
	float NoL = clamp(dot(N, L), 0.0, 1.0);
	float NoH = clamp(dot(N, H), 0.0, 1.0);

	vec3 diffuseComponent = NoL * lightColor;
	vec3 specularComponent = pow(NoH, 64.0) * lightColor;

	return (ambientLight + (1.0 - shadowStength) * (diffuseComponent + specularComponent)) * albedo;
}

#endif  // LIGHTING_GLSL_H
