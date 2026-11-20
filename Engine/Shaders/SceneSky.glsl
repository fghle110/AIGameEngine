// Procedural sky shared by the background pass and the ambient term of the PBR
// shader, so the environment a surface reflects matches what is behind it.

vec3 skyRadiance(vec3 direction)
{
	const float up = clamp(direction.y, -1.0, 1.0);
	const vec3 zenith = vec3(0.14, 0.22, 0.42);
	const vec3 horizon = vec3(0.58, 0.63, 0.72);
	const vec3 ground = vec3(0.10, 0.09, 0.08);

	if (up >= 0.0)
	{
		return mix(horizon, zenith, pow(up, 0.65));
	}
	return mix(horizon * 0.55, ground, clamp(-up * 2.5, 0.0, 1.0));
}
