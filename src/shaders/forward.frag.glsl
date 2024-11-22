#version 450
#pragma shader_stage(fragment)

layout(location = 0) in FS_IN
{
	vec4 i_Position;
	vec3 i_Color;
	vec2 i_TexCoord;
	mat3 i_TBN;
};

layout(location = 0) out vec4 f_FragColor;

void main()
{
	vec3 N = normalize(i_TBN * vec3(0, 0, 1));
	f_FragColor = vec4(0.5 + 0.5 * N, 1.0);
}
