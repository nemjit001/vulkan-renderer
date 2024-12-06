#version 450
#pragma shader_stage(vertex)

layout(location = 0) in vec3 v_Position;

layout(location = 0) out vec3 o_UVW;

layout(set = 0, binding = 0) uniform CAMERA_DATA
{
	mat4 matrix;
};

void main()
{
	o_UVW = v_Position;
	o_UVW.xy *= -1.0;

	gl_Position = matrix * vec4(v_Position, 1.0);
}
