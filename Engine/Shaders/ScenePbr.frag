#version 460

// Forward physically based shading: Cook-Torrance specular (GGX distribution,
// Smith geometry term, Schlick Fresnel), metallic-roughness workflow, normal
// mapping, a shadow map with 3x3 PCF, and a procedural sky used as the ambient
// environment. Lighting stays linear here; tonemapping happens only in the post
// pass so nothing is tone-mapped twice.

#extension GL_GOOGLE_include_directive : require
#include "SceneFrameConstants.glsl"
#include "SceneObjectConstants.glsl"
#include "SceneSky.glsl"

layout(set = 0, binding = 1) uniform texture2D uShadowMap;
layout(set = 0, binding = 2) uniform sampler uShadowSampler;
layout(set = 0, binding = 4) uniform textureCube uIrradianceCube;
layout(set = 0, binding = 5) uniform textureCube uPrefilteredCube;
layout(set = 0, binding = 6) uniform texture2D uBrdfLut;
layout(set = 0, binding = 7) uniform sampler uEnvironmentSampler;
layout(set = 0, binding = 8) uniform texture2D uAoTexture;
layout(set = 0, binding = 9) uniform sampler uAoSampler;

layout(set = 1, binding = 0) uniform texture2D uAlbedoTexture;
layout(set = 1, binding = 1) uniform texture2D uNormalTexture;
layout(set = 1, binding = 2) uniform texture2D uMetallicRoughnessTexture;
layout(set = 1, binding = 3) uniform texture2D uOcclusionTexture;
layout(set = 1, binding = 4) uniform texture2D uEmissiveTexture;
layout(set = 1, binding = 5) uniform sampler uAlbedoSampler;
layout(set = 1, binding = 6) uniform sampler uNormalSampler;
layout(set = 1, binding = 7) uniform sampler uMetallicRoughnessSampler;
layout(set = 1, binding = 8) uniform sampler uOcclusionSampler;
layout(set = 1, binding = 9) uniform sampler uEmissiveSampler;

layout(location = 0) in vec3 vWorldPosition;
layout(location = 1) in vec3 vNormal;
layout(location = 2) in vec4 vTangent;
layout(location = 3) in vec2 vTexCoord;
layout(location = 4) in vec2 vScreenUv;

layout(location = 0) out vec4 oColor;

const float c_Pi = 3.14159265359;
const float c_MinRoughness = 0.045;

float distributionGGX(float nDotH, float roughness)
{
	const float alpha = roughness * roughness;
	const float alphaSquared = alpha * alpha;
	const float denominator = nDotH * nDotH * (alphaSquared - 1.0) + 1.0;
	return alphaSquared / max(c_Pi * denominator * denominator, 1.0e-6);
}

float geometrySchlickGGX(float nDotX, float roughness)
{
	const float r = roughness + 1.0;
	const float k = (r * r) / 8.0;
	return nDotX / max(nDotX * (1.0 - k) + k, 1.0e-6);
}

float geometrySmith(float nDotV, float nDotL, float roughness)
{
	return geometrySchlickGGX(nDotV, roughness) * geometrySchlickGGX(nDotL, roughness);
}

vec3 fresnelSchlick(float cosTheta, vec3 f0)
{
	return f0 + (1.0 - f0) * pow(clamp(1.0 - cosTheta, 0.0, 1.0), 5.0);
}

vec3 fresnelSchlickRoughness(float cosTheta, vec3 f0, float roughness)
{
	const vec3 grazing = max(vec3(1.0 - roughness), f0);
	return f0 + (grazing - f0) * pow(clamp(1.0 - cosTheta, 0.0, 1.0), 5.0);
}

// Evaluates one light with the Cook-Torrance BRDF and returns the outgoing
// radiance for the given incoming direction.
vec3 shadeLight(vec3 normal, vec3 view, vec3 lightDirection, vec3 radiance, vec3 baseColor, float metallic,
	float roughness, vec3 f0)
{
	const vec3 halfway = normalize(view + lightDirection);
	const float nDotL = max(dot(normal, lightDirection), 0.0);
	const float nDotV = max(dot(normal, view), 0.0);
	if (nDotL <= 0.0 || nDotV <= 0.0)
	{
		return vec3(0.0);
	}

	const float nDotH = max(dot(normal, halfway), 0.0);
	const float vDotH = max(dot(view, halfway), 0.0);

	const float distribution = distributionGGX(nDotH, roughness);
	const float geometry = geometrySmith(nDotV, nDotL, roughness);
	const vec3 fresnel = fresnelSchlick(vDotH, f0);

	const vec3 specular = (distribution * geometry * fresnel) / max(4.0 * nDotV * nDotL, 1.0e-6);
	const vec3 diffuseWeight = (1.0 - fresnel) * (1.0 - metallic);
	const vec3 diffuse = diffuseWeight * baseColor / c_Pi;

	return (diffuse + specular) * radiance * nDotL;
}

vec3 sampleNormal(vec3 geometricNormal)
{
	const vec3 sampled = texture(sampler2D(uNormalTexture, uNormalSampler), vTexCoord).xyz * 2.0 - 1.0;
	const float scale = object.uMetallicRoughnessNormalOcclusion.z;
	vec3 tangent = vTangent.xyz - geometricNormal * dot(geometricNormal, vTangent.xyz);
	if (dot(tangent, tangent) < 1.0e-10)
	{
		// A degenerate tangent frame (pole vertices) falls back to any orthogonal frame.
		const vec3 axis = abs(geometricNormal.y) < 0.9 ? vec3(0.0, 1.0, 0.0) : vec3(1.0, 0.0, 0.0);
		tangent = cross(axis, geometricNormal);
	}
	tangent = normalize(tangent);
	const vec3 bitangent = cross(geometricNormal, tangent) * vTangent.w;
	const vec3 mapped = vec3(sampled.xy * scale, sampled.z);
	return normalize(mat3(tangent, bitangent, geometricNormal) * mapped);
}

// 3x3 percentage-closer filter around the receiver's light-space position.
float sampleShadow(vec3 normal, vec3 lightDirection)
{
	if (frame.uPostProcess.w < 0.5)
	{
		return 1.0;
	}

	const vec4 lightClip = frame.uLightViewProjection * vec4(vWorldPosition, 1.0);
	const vec3 projected = lightClip.xyz / lightClip.w;
	const vec2 uv = projected.xy * 0.5 + 0.5;
	if (uv.x < 0.0 || uv.x > 1.0 || uv.y < 0.0 || uv.y > 1.0 || projected.z > 1.0)
	{
		return 1.0;
	}

	// Normal offset plus a slope-scaled constant keeps grazing surfaces from
	// shadowing themselves without visible peter-panning.
	const float nDotL = max(dot(normal, lightDirection), 0.0);
	const float bias = 0.0009 + 0.0016 * (1.0 - nDotL);
	const vec2 texel = 1.0 / vec2(textureSize(sampler2D(uShadowMap, uShadowSampler), 0));

	float visibility = 0.0;
	for (int y = -1; y <= 1; ++y)
	{
		for (int x = -1; x <= 1; ++x)
		{
			const float stored = texture(sampler2D(uShadowMap, uShadowSampler), uv + vec2(x, y) * texel).r;
			visibility += projected.z - bias <= stored ? 1.0 : 0.0;
		}
	}
	return visibility / 9.0;
}

void main()
{
	const vec3 geometricNormal = normalize(vNormal);
	const vec3 normal = sampleNormal(geometricNormal);
	const vec3 view = normalize(frame.uCameraPosition.xyz - vWorldPosition);

	const vec4 albedoSample = texture(sampler2D(uAlbedoTexture, uAlbedoSampler), vTexCoord);
	// Both the factor and the sampled map are already linear (the texture loader
	// linearises sRGB files), so no transfer function is applied here.
	const vec3 baseColor = object.uBaseColorFactor.rgb * albedoSample.rgb;
	const vec4 metallicRoughness = texture(sampler2D(uMetallicRoughnessTexture, uMetallicRoughnessSampler),
		vTexCoord);
	const float metallic = clamp(object.uMetallicRoughnessNormalOcclusion.x * metallicRoughness.b, 0.0, 1.0);
	const float roughness = clamp(object.uMetallicRoughnessNormalOcclusion.y * metallicRoughness.g, c_MinRoughness,
		1.0);
	const float occlusion = mix(1.0,
		texture(sampler2D(uOcclusionTexture, uOcclusionSampler), vTexCoord).r,
		clamp(object.uMetallicRoughnessNormalOcclusion.w, 0.0, 1.0));

	const vec3 f0 = mix(vec3(0.04), baseColor, metallic);
	vec3 outgoing = vec3(0.0);

	// Directional sun with a shadow map.
	const vec3 sunDirection = normalize(frame.uSunDirection.xyz);
	if (frame.uSunDirection.w > 0.0)
	{
		const float shadow = sampleShadow(normal, sunDirection) *
			clamp(frame.uSunColor.w, 0.0, 1.0) + (1.0 - clamp(frame.uSunColor.w, 0.0, 1.0));
		const vec3 radiance = frame.uSunColor.rgb * frame.uSunDirection.w * shadow;
		outgoing += shadeLight(normal, view, sunDirection, radiance, baseColor, metallic, roughness, f0);
	}

	// Point and spot lights.
	const uint lightCount = min(frame.uLightCounts.x, 8u);
	for (uint index = 0u; index < lightCount; ++index)
	{
		const SceneLight light = frame.uLights[index];
		const vec3 toLight = light.PositionRange.xyz - vWorldPosition;
		const float distance = length(toLight);
		if (distance > light.PositionRange.w || distance <= 1.0e-5)
		{
			continue;
		}

		const vec3 lightDirection = toLight / distance;
		float attenuation = 1.0 / (distance * distance + 1.0);
		const float rangeFade = clamp(1.0 - pow(distance / light.PositionRange.w, 4.0), 0.0, 1.0);
		attenuation *= rangeFade * rangeFade;

		if (light.DirectionType.w > 1.5)
		{
			const float coneAngle = dot(-lightDirection, normalize(light.DirectionType.xyz));
			attenuation *= smoothstep(light.SpotAngles.y, light.SpotAngles.x, coneAngle);
		}

		const vec3 radiance = light.ColorIntensity.rgb * light.ColorIntensity.w * attenuation;
		outgoing += shadeLight(normal, view, lightDirection, radiance, baseColor, metallic, roughness, f0);
	}

	// Ambient: image-based lighting when the environment map is active, otherwise the
	// procedural sky. The cubes are prefiltered on the CPU when the environment
	// changes, so this is the split-sum approximation with a real BRDF LUT.
	const vec3 ambientTint = frame.uAmbient.rgb * frame.uAmbient.w;
	vec3 ambientLighting = vec3(0.0);
	if (frame.uEnvironment.x > 0.5)
	{
		const float intensity = max(frame.uEnvironment.z, 0.0);
		const vec3 irradiance = texture(samplerCube(uIrradianceCube, uEnvironmentSampler), normal).rgb;
		const vec3 diffuseAmbient = irradiance * baseColor * (1.0 - metallic);
		const vec3 reflected = reflect(-view, normal);
		const float mipCount = max(frame.uEnvironment.y, 1.0);
		const vec3 prefiltered = textureLod(samplerCube(uPrefilteredCube, uEnvironmentSampler), reflected,
			roughness * (mipCount - 1.0)).rgb;
		const vec2 brdf = texture(sampler2D(uBrdfLut, uEnvironmentSampler),
			vec2(max(dot(normal, view), 0.0), roughness)).rg;
		const vec3 specularAmbient = prefiltered * (f0 * brdf.x + brdf.y);
		ambientLighting = (diffuseAmbient + specularAmbient) * intensity;
	}
	else
	{
		const vec3 diffuseAmbient = skyRadiance(normal) * baseColor * (1.0 - metallic);
		const vec3 reflected = reflect(-view, normal);
		const vec3 specularAmbient = skyRadiance(reflected) *
			fresnelSchlickRoughness(max(dot(normal, view), 0.0), f0, roughness) * (1.0 - roughness * 0.7);
		ambientLighting = (diffuseAmbient + specularAmbient) * ambientTint;
	}
	// Screen-space ambient occlusion darkens only the indirect term, weighted by
	// the environment flag's w component (0 = SSAO disabled).
	const float ssaoStrength = clamp(frame.uEnvironment.w, 0.0, 1.0);
	float screenAo = 1.0;
	if (ssaoStrength > 0.0)
	{
		screenAo = mix(1.0, texture(sampler2D(uAoTexture, uAoSampler), vScreenUv).r, ssaoStrength);
	}
	outgoing += ambientLighting * occlusion * screenAo;

	const vec3 emissive = object.uEmissive.rgb * object.uEmissive.w *
		texture(sampler2D(uEmissiveTexture, uEmissiveSampler), vTexCoord).rgb;
	outgoing += emissive;

	oColor = vec4(outgoing, albedoSample.a);
}
