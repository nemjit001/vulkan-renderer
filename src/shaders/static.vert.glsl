#version 460
#pragma shader_stage(vertex)

layout(location = 0) in vec3 v_Position;
layout(location = 1) in vec3 v_Color;
layout(location = 2) in vec3 v_Normal;
layout(location = 3) in vec3 v_Tangent;
layout(location = 4) in vec2 v_TexCoord;

layout(location = 0) out FS_IN
{
	vec3 vertexPos;
	vec3 color;
	vec2 texCoord;
	mat3 TBN;
} out_Result;

layout(set = 0, binding = 0) uniform SCENE_DATA_UNIFORM
{
	vec3 sunDirection;
	vec3 sunColor;
	vec3 ambientLight;
	vec3 cameraPosition;
	mat4 viewproject;
};

layout(set = 1, binding = 0) uniform OBJECT_DATA_UNIFORM
{
	mat4 model;
	mat4 normal;
	float specularity;
};

void main()
{
	vec4 position = model * vec4(v_Position, 1);
	vec3 T = normalize((normal * vec4(v_Tangent, 0)).xyz);
	vec3 N = normalize((normal * vec4(v_Normal, 0)).xyz);

	T = normalize(T - dot(T, N) * N);
	vec3 B = cross(N, T);
	
	gl_Position = viewproject * position;
	out_Result.vertexPos = position.xyz / position.w;
	out_Result.color = v_Color;
	out_Result.texCoord = v_TexCoord;
	out_Result.TBN = mat3(T, B, N);
}
