#version 460

// Copies the tonemapped scene target onto the swapchain. The post vertex shader
// already flips v, so this is a straight bilinear sample.

layout(set = 0, binding = 0) uniform texture2D uSourceColor;
layout(set = 0, binding = 1) uniform sampler uSourceSampler;

layout(location = 0) in vec2 vTexCoord;
layout(location = 0) out vec4 oColor;

void main()
{
	oColor = texture(sampler2D(uSourceColor, uSourceSampler), vTexCoord);
}
