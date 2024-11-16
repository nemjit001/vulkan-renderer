#version 460
#pragma shader_stage(fragment)

layout(location = 0) in FS_IN
{
	vec3 vertexPos;
	vec3 color;
	vec2 texCoord;
	mat3 TBN;
} in_Input;

layout(location = 0) out vec4 out_FragColor;

void main()
{
	vec3 N = normalize(in_Input.TBN * vec3(0, 0, 1));
	out_FragColor = vec4(0.5 + 0.5 * N, 1);
}
