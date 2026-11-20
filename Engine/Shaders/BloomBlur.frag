#version 460

// Separable 9-tap gaussian used twice (horizontal, then vertical) to blur the bloom
// bright pass. The direction and texel size arrive in the push constant.

layout(set = 0, binding = 0) uniform texture2D uSource;
layout(set = 0, binding = 1) uniform sampler uSourceSampler;

layout(push_constant) uniform BloomBlurConstants
{
	vec4 uParams; // xy = texel size, zw = direction (1,0) or (0,1)
} pc;

layout(location = 0) in vec2 vTexCoord;
layout(location = 0) out vec4 oColor;

void main()
{
	const vec2 step = pc.uParams.xy * pc.uParams.zw;
	const float weights[5] = float[5](0.227027, 0.194595, 0.121622, 0.054054, 0.016216);
	vec3 sum = texture(sampler2D(uSource, uSourceSampler), vTexCoord).rgb * weights[0];
	for (int i = 1; i < 5; ++i)
	{
		sum += texture(sampler2D(uSource, uSourceSampler), vTexCoord + step * float(i)).rgb * weights[i];
		sum += texture(sampler2D(uSource, uSourceSampler), vTexCoord - step * float(i)).rgb * weights[i];
	}
	oColor = vec4(sum, 1.0);
}
