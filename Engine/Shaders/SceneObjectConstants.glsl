// Per-object constants pushed as a push constant block: the model matrix and the
// material factors. Dimensional analysis: 64 + 16 * 3 = 112 bytes, below the
// 128-byte minimum every Vulkan device guarantees.
//
// The C++ mirror is aegis::SceneObjectConstants in Renderer/SceneRenderer.h.

layout(push_constant) uniform SceneObjectConstants
{
	mat4 uModel;
	vec4 uBaseColorFactor;
	vec4 uMetallicRoughnessNormalOcclusion; // x = metallic, y = roughness, z = normal scale, w = occlusion
	vec4 uEmissive;                         // rgb = emissive colour, w = emissive strength
} object;
