#version 450
#pragma shader_stage(vertex)

layout(location = 0) in vec3 v_Position;
layout(location = 1) in vec3 v_Color;
layout(location = 2) in vec3 v_Normal;
layout(location = 3) in vec3 v_Tangent;
layout(location = 4) in vec2 v_TexCoord;

layout(set = 0, binding = 0) uniform SHADOW_MAP_CAMERA_DATA { mat4 matrix; } camera;
layout(set = 1, binding = 0) uniform SHADOW_MAP_OBJECT_DATA { mat4 model; } object;

void main()
{
	gl_Position = camera.matrix * object.model * vec4(v_Position, 1.);
}
