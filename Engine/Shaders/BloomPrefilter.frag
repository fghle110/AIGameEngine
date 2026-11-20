#version 460

// Bloom bright pass: downsamples the HDR image 2x2 and keeps only the part above a
// soft-knee threshold, so the blur chain below spreads genuine highlights and not
// the whole scene.

layout(set = 0, binding = 0) uniform texture2D uHdrColor;
layout(set = 0, binding = 1) uniform sampler uHdrSampler;

layout(push_constant) uniform BloomPrefilterConstants
{
	vec4 uParams; // x = threshold, zw = HDR texel size
} pc;

layout(location = 0) in vec2 vTexCoord;
layout(location = 0) out vec4 oColor;

void main()
{
	const vec2 texel = pc.uParams.zw;
	vec3 color = texture(sampler2D(uHdrColor, uHdrSampler), vTexCoord + vec2(-0.5, -0.5) * texel).rgb;
	color += texture(sampler2D(uHdrColor, uHdrSampler), vTexCoord + vec2(0.5, -0.5) * texel).rgb;
	color += texture(sampler2D(uHdrColor, uHdrSampler), vTexCoord + vec2(-0.5, 0.5) * texel).rgb;
	color += texture(sampler2D(uHdrColor, uHdrSampler), vTexCoord + vec2(0.5, 0.5) * texel).rgb;
	color *= 0.25;

	const float threshold = max(pc.uParams.x, 0.0);
	const float brightness = max(color.r, max(color.g, color.b));
	const float knee = max(threshold * 0.5, 1.0e-3);
	float soft = clamp(brightness - threshold + knee, 0.0, 2.0 * knee);
	soft = soft * soft / (4.0 * knee);
	const float contribution = max(soft, brightness - threshold) / max(brightness, 1.0e-4);
	oColor = vec4(color * contribution, 1.0);
}
