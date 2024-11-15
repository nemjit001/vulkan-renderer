#version 450
#pragma shader_stage(fragment)

layout(location = 0) in FS_IN
{
	vec3 color;
	vec2 texCoord;
} in_Input;

layout(location = 0) out vec4 out_FragColor;

layout(set = 0, binding = 0) uniform SCENE_DATA_UNIFORM
{
	vec3 cameraPosition;
	mat4 viewproject;
};

void main()
{
	out_FragColor = vec4(in_Input.texCoord, 0, 1);
}
