#version 450
#pragma shader_stage(vertex)

layout(location = 0) in vec3 v_Position;
layout(location = 1) in vec3 v_Color;
layout(location = 2) in vec2 v_TexCoord;

layout(location = 0) out FS_IN
{
	vec3 color;
	vec2 texCoord;
} out_Result;

layout(set = 0, binding = 0) uniform SCENE_DATA_UNIFORM
{
	vec3 cameraPosition;
	mat4 viewproject;
};

void main()
{
	vec4 position = vec4(v_Position, 1);

	out_Result.color = v_Color;
	out_Result.texCoord = v_TexCoord;

	gl_Position = viewproject * position;
}
