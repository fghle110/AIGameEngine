#version 460

// Dear ImGui vertex shader. Positions arrive in pixel space; the push constant
// carries the scale/translate that maps them to clip space.

layout(location = 0) in vec2 aPosition;
layout(location = 1) in vec2 aTexCoord;
layout(location = 2) in vec4 aColor;

layout(location = 0) out vec2 vTexCoord;
layout(location = 1) out vec4 vColor;

layout(push_constant) uniform PushConstants
{
	vec2 uScale;
	vec2 uTranslate;
} pc;

void main()
{
	vTexCoord = aTexCoord;
	vColor = aColor;
	gl_Position = vec4(aPosition * pc.uScale + pc.uTranslate, 0.0, 1.0);
}
