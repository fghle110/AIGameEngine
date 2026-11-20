#version 460

// Screen-space ambient occlusion from the camera depth prepass. World-space
// positions are reconstructed from depth, normals from screen-space derivatives,
// and a fixed hemisphere kernel is rotated per pixel by a hash so banding turns
// into noise that the blur pass removes.

#extension GL_GOOGLE_include_directive : require
#include "SceneFrameConstants.glsl"

layout(set = 0, binding = 1) uniform texture2D uDepthTexture;
layout(set = 0, binding = 2) uniform sampler uDepthSampler;

layout(push_constant) uniform SceneSsaoConstants
{
	vec4 uParams; // x = radius (m), y = bias (m), z = strength, w = unused
} pc;

layout(location = 0) in vec2 vTexCoord;
layout(location = 0) out vec4 oOcclusion;

const int c_SampleCount = 16;
const vec3 c_Kernel[16] = vec3[16](
	vec3(0.5381, 0.1856, 0.4319), vec3(0.1379, 0.2486, 0.4430),
	vec3(0.3371, 0.5679, 0.0057), vec3(-0.6999, -0.0451, 0.0019),
	vec3(0.0689, -0.1598, 0.8547), vec3(0.0560, 0.0069, 0.1843),
	vec3(-0.0146, 0.1402, 0.0762), vec3(0.0100, -0.1924, 0.0344),
	vec3(-0.3577, -0.5301, 0.4358), vec3(-0.3169, 0.1063, 0.0158),
	vec3(0.0103, -0.5869, 0.0046), vec3(-0.0897, -0.4940, 0.3287),
	vec3(0.7119, -0.0154, 0.0918), vec3(-0.0533, 0.0596, 0.5411),
	vec3(0.0352, -0.0631, 0.5460), vec3(-0.4776, 0.2847, 0.0271));

vec3 ReconstructWorld(vec2 uv, float depth)
{
	// NVRHI renders with a DX-style viewport: NDC y is +1 at the top row, and the
	// texture row 0 is that same row, so v flips.
	const vec2 ndc = vec2(uv.x * 2.0 - 1.0, 1.0 - uv.y * 2.0);
	const vec4 world = frame.uInvViewProjection * vec4(ndc, depth, 1.0);
	return world.xyz / world.w;
}

float Hash(vec2 p)
{
	return fract(sin(dot(p, vec2(12.9898, 78.233))) * 43758.5453);
}

void main()
{
	const float depth = texture(sampler2D(uDepthTexture, uDepthSampler), vTexCoord).r;
	if (depth >= 1.0)
	{
		oOcclusion = vec4(1.0);
		return;
	}

	const vec3 worldPosition = ReconstructWorld(vTexCoord, depth);
	const vec3 cameraPosition = frame.uCameraPosition.xyz;
	vec3 normal = normalize(cross(dFdx(worldPosition), dFdy(worldPosition)));
	if (dot(normal, cameraPosition - worldPosition) < 0.0)
	{
		normal = -normal;
	}

	const float angle = Hash(vTexCoord * 1024.0) * 6.28318530718;
	const vec3 randomDirection = vec3(cos(angle), sin(angle), 0.0);
	const vec3 tangent = normalize(randomDirection - normal * dot(randomDirection, normal));
	const vec3 bitangent = cross(normal, tangent);
	const mat3 basis = mat3(tangent, bitangent, normal);

	const float radius = pc.uParams.x;
	const float bias = pc.uParams.y;
	const float cameraDistance = length(worldPosition - cameraPosition);

	float occlusion = 0.0;
	for (int i = 0; i < c_SampleCount; ++i)
	{
		const vec3 samplePosition = worldPosition + basis * c_Kernel[i] * radius;
		const vec4 clip = frame.uViewProjection * vec4(samplePosition, 1.0);
		if (clip.w <= 1.0e-5)
		{
			continue;
		}
		vec2 sampleUv = clip.xy / clip.w * 0.5 + 0.5;
		sampleUv.y = 1.0 - sampleUv.y;
		if (sampleUv.x < 0.0 || sampleUv.x > 1.0 || sampleUv.y < 0.0 || sampleUv.y > 1.0)
		{
			continue;
		}

		const float sampleDepth = texture(sampler2D(uDepthTexture, uDepthSampler), sampleUv).r;
		if (sampleDepth >= 1.0)
		{
			continue;
		}
		const vec3 sampleWorld = ReconstructWorld(sampleUv, sampleDepth);
		const float sampleDistance = length(sampleWorld - cameraPosition);
		const float rangeCheck = smoothstep(0.0, 1.0, radius / max(abs(cameraDistance - sampleDistance), 1.0e-4));
		if (sampleDistance < length(samplePosition - cameraPosition) - bias)
		{
			occlusion += rangeCheck;
		}
	}

	const float ao = 1.0 - (occlusion / float(c_SampleCount)) * clamp(pc.uParams.z, 0.0, 1.0);
	oOcclusion = vec4(ao, ao, ao, 1.0);
}
