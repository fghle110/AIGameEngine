#version 460

// 9x9 gaussian blur of the SSAO result. The AO target is half resolution, so a
// wide blur is cheap and turns the 16-sample noise into smooth contact shadows.

layout(set = 0, binding = 0) uniform texture2D uAoTexture;
layout(set = 0, binding = 1) uniform sampler uAoSampler;

layout(push_constant) uniform SceneBlurConstants
{
	vec4 uTexelSize; // xy = texel size of the source
} pc;

layout(location = 0) in vec2 vTexCoord;
layout(location = 0) out vec4 oColor;

void main()
{
	const vec2 texel = pc.uTexelSize.xy;
	float sum = 0.0;
	float weight = 0.0;
	for (int y = -4; y <= 4; ++y)
	{
		for (int x = -4; x <= 4; ++x)
		{
			const float w = exp(-float(x * x + y * y) / 8.0);
			sum += texture(sampler2D(uAoTexture, uAoSampler), vTexCoord + vec2(x, y) * texel).r * w;
			weight += w;
		}
	}
	const float ao = sum / weight;
	oColor = vec4(ao, ao, ao, 1.0);
}
