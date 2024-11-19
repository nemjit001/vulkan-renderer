#version 460
#pragma shader_stage(fragment)

#include "uniforms.glsl.h"

#define INV_GAMMA   2.2
#define GAMMA       1.0 / INV_GAMMA

layout(location = 0) in FS_IN
{
	vec3 vertexPos;
	vec3 color;
	vec2 texCoord;
	mat3 TBN;
} in_Input;

layout(location = 0) out vec4 out_FragColor;

void main()
{
	vec3 color = pow(texture(colorTexture, in_Input.texCoord).rgb, vec3(INV_GAMMA)); // Convert from SRGB to linear colors    
    vec3 normal = texture(normalTexture, in_Input.texCoord).rgb; // Assume normals stored in linear format
    normal = (2.0 * normal) - 1.0;

	vec3 L = normalize(lightDirection);
    vec3 V = normalize(cameraPosition - in_Input.vertexPos);
    vec3 H = normalize(L + V);
    vec3 N = normalize(in_Input.TBN * normal);

	float NoL = clamp(dot(N, L), 0.0, 1.0);
    float NoH = clamp(dot(N, H), 0.0, 1.0);

	vec3 ambientComponent = ambient * color;
    vec3 diffuseComponent = NoL * color * lightColor;
    vec3 specularComponent = specularity * pow(NoH, 64.0) * lightColor;
    vec3 outColor = ambientComponent + diffuseComponent + specularComponent;

	out_FragColor = vec4(outColor, 1);
}
