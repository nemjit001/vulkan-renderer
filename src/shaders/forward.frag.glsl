#version 460
#pragma shader_stage(fragment)

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

layout(set = 0, binding = 0) uniform SCENE_DATA_UNIFORM
{
	vec3 sunDirection;
	vec3 sunColor;
	vec3 ambientLight;
	vec3 cameraPosition;
	mat4 viewproject;
	mat4 model;
	mat4 normal;
	float specularity;
};

layout(set = 0, binding = 1) uniform sampler2D colorTexture;
layout(set = 0, binding = 2) uniform sampler2D normalTexture;

void main()
{
	vec3 color = pow(texture(colorTexture, in_Input.texCoord).rgb, vec3(INV_GAMMA)); // Convert from SRGB to linear colors    
    vec3 normal = texture(normalTexture, in_Input.texCoord).rgb; // Assume normals stored in linear format
    normal = (2.0 * normal) - 1.0;

	vec3 L = normalize(sunDirection);
    vec3 V = normalize(cameraPosition - in_Input.vertexPos);
    vec3 H = normalize(L + V);
    vec3 N = normalize(in_Input.TBN * normal);

	float NoL = clamp(dot(N, L), 0.0, 1.0);
    float NoH = clamp(dot(N, H), 0.0, 1.0);

	vec3 ambient = ambientLight * color;
    vec3 diffuse = NoL * color * sunColor;
    vec3 specular = pow(NoH, 64.0) * sunColor;
    vec3 outColor = ambient + diffuse + specularity * specular;

	out_FragColor = vec4(outColor, 1);
}
