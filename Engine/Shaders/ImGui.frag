#version 460

// Dear ImGui pixel shader: samples the font atlas and multiplies by the vertex
// colour supplied by ImGui.

layout(location = 0) in vec2 vTexCoord;
layout(location = 1) in vec4 vColor;

layout(set = 0, binding = 0) uniform texture2D uFontAtlas;
layout(set = 0, binding = 1) uniform sampler uFontSampler;

layout(location = 0) out vec4 oColor;

void main()
{
	oColor = vColor * texture(sampler2D(uFontAtlas, uFontSampler), vTexCoord);
}
