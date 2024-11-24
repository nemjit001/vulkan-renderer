
#define GAMMA		2.2
#define INV_GAMMA	1.0 / GAMMA

vec3 gamma(vec3 color)
{
	return pow(color, vec3(INV_GAMMA));
}

vec3 linear(vec3 color)
{
	return pow(color, vec3(GAMMA));
}
