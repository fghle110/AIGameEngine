#version 460

// Procedural sky background. Drawn as a fullscreen triangle before the geometry so
// the HDR target starts from the environment instead of the clear colour; the
// direction is reconstructed from the inverse view-projection.

#extension GL_GOOGLE_include_directive : require
#include "SceneFrameConstants.glsl"
#include "SceneSky.glsl"

layout(location = 0) in vec2 vTexCoord;
layout(location = 0) out vec4 oColor;

void main()
{
	const vec2 ndc = vec2(vTexCoord.x * 2.0 - 1.0, (1.0 - vTexCoord.y) * 2.0 - 1.0);
	const vec4 world = frame.uInvViewProjection * vec4(ndc, 1.0, 1.0);
	const vec3 direction = normalize(world.xyz / world.w - frame.uCameraPosition.xyz);
	oColor = vec4(skyRadiance(direction), 1.0);
}
