
#define GAMMA		2.2
#define INV_GAMMA	1.0 / GAMMA

vec4 gamma(vec4 color)
{
	return vec4(pow(color.rgb, vec3(INV_GAMMA)), color.a);
}

vec4 linear(vec4 color)
{
	return vec4(pow(color.rgb, vec3(GAMMA)), color.a);
}
