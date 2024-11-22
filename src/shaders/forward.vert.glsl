#version 450
#pragma shader_stage(vertex)

layout(location = 0) in vec3 v_Position;
layout(location = 1) in vec3 v_Color;
layout(location = 2) in vec3 v_Normal;
layout(location = 3) in vec3 v_Tangent;
layout(location = 4) in vec2 v_TexCoord;

layout(location = 0) out FS_IN
{
	vec4 o_Position;
	vec3 o_Color;
	vec2 o_TexCoord;
	mat3 o_TBN;
};

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
	vec4 position = object.model * vec4(v_Position, 1.);
	vec3 N = normalize((object.normal * vec4(v_Normal, 0.)).xyz);
	vec3 T = normalize((object.normal * vec4(v_Tangent, 0.)).xyz);
	T = normalize(T - dot(T, N) * N);
	vec3 B = normalize(cross(T, N));

	gl_Position = camera.matrix * position;
	o_Position = position;
	o_Color = v_Color;
	o_TexCoord = v_TexCoord;
	o_TBN = mat3(T, B, N);
}
