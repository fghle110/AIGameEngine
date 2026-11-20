#version 460

// Post-processing: exposure, ACES filmic tonemapping and the final display
// encoding. The destination is an sRGB texture, so the linear result here is
// encoded by the hardware exactly once.

layout(set = 0, binding = 0) uniform texture2D uHdrColor;
layout(set = 0, binding = 1) uniform sampler uHdrSampler;

layout(push_constant) uniform ScenePostConstants
{
	vec4 uPostProcess; // x = exposure, y = bloom intensity, z = unused, w = unused
} post;

layout(location = 0) in vec2 vTexCoord;
layout(location = 0) out vec4 oColor;

// Narkowicz's ACES fit: cheap, monotonic and close enough to the reference curve.
vec3 tonemapAces(vec3 color)
{
	const float a = 2.51;
	const float b = 0.03;
	const float c = 2.43;
	const float d = 0.59;
	const float e = 0.14;
	return clamp((color * (a * color + b)) / (color * (c * color + d) + e), 0.0, 1.0);
}

void main()
{
	const vec3 hdr = texture(sampler2D(uHdrColor, uHdrSampler), vTexCoord).rgb;
	const vec3 exposed = max(hdr, vec3(0.0)) * post.uPostProcess.x;
	const vec3 mapped = tonemapAces(exposed);
	oColor = vec4(mapped, 1.0);
}
