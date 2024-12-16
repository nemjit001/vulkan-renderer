#version 450
#pragma shader_stage(fragment)

layout(location = 0) in vec3 i_UVW;

layout(location = 0) out vec4 f_FragColor;

layout(set = 0, binding = 1) uniform samplerCube skybox;

void main()
{
	f_FragColor = texture(skybox, i_UVW);
}
