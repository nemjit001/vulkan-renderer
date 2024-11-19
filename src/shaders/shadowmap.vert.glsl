#version 460
#pragma shader_stage(vertex)

#include "shadowmap_uniforms.glsl.h"

layout(location = 0) in vec3 v_Position;
layout(location = 1) in vec3 v_Color;
layout(location = 2) in vec3 v_Normal;
layout(location = 3) in vec3 v_Tangent;
layout(location = 4) in vec2 v_TexCoord;

void main()
{
	gl_Position = viewproject * model * vec4(v_Position, 0.0);
}
