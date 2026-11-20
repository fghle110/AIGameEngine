// Frame constants of the SceneRenderer, shared by every scene pass.
//
// Descriptor set 0 is the per-frame set (see AGENTS.md section 6). The C++ mirror
// is aegis::SceneFrameConstants in Renderer/SceneRenderer.h; both sides are packed
// with std140 rules (only mat4/vec4/uvec4 members) and checked by a static_assert.

struct SceneLight
{
	vec4 PositionRange;  // xyz = world position, w = range in metres
	vec4 ColorIntensity; // rgb = colour, w = intensity
	vec4 DirectionType;  // xyz = spot direction, w = type (1 = point, 2 = spot)
	vec4 SpotAngles;     // x = cos(inner cone), y = cos(outer cone)
};

layout(set = 0, binding = 0, std140) uniform SceneFrameConstants
{
	mat4 uViewProjection;
	mat4 uInvViewProjection;
	mat4 uLightViewProjection;
	vec4 uCameraPosition; // xyz = camera world position
	vec4 uAmbient;        // rgb = ambient tint, w = intensity
	vec4 uSunDirection;   // xyz = direction towards the sun, w = intensity
	vec4 uSunColor;       // rgb = sun colour, w = shadow strength
	vec4 uPostProcess;    // x = exposure, y = bloom intensity, z = shadow distance, w = flags
	uvec4 uLightCounts;   // x = point/spot light count, y = shadow map index bound
	SceneLight uLights[8];
} frame;
