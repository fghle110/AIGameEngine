#version 460

// Forward PBR vertex stage: transforms the mesh into world space and passes the
// tangent frame through for normal mapping.

#extension GL_GOOGLE_include_directive : require
#include "SceneFrameConstants.glsl"
#include "SceneObjectConstants.glsl"

layout(location = 0) in vec3 aPosition;
layout(location = 1) in vec3 aNormal;
layout(location = 2) in vec4 aTangent;
layout(location = 3) in vec2 aTexCoord;

layout(location = 0) out vec3 vWorldPosition;
layout(location = 1) out vec3 vNormal;
layout(location = 2) out vec4 vTangent;
layout(location = 3) out vec2 vTexCoord;
layout(location = 4) out vec2 vScreenUv;

void main()
{
	const mat4 model = object.uModel;
	const vec4 worldPosition = model * vec4(aPosition, 1.0);

	// Normals and tangents transform with the inverse transpose of the model
	// matrix so non-uniform scale does not skew the lighting frame.
	const mat3 normalMatrix = transpose(inverse(mat3(model)));

	vWorldPosition = worldPosition.xyz;
	vNormal = normalMatrix * aNormal;
	vTangent = vec4(normalMatrix * aTangent.xyz, aTangent.w);
	vTexCoord = aTexCoord;

	gl_Position = frame.uViewProjection * worldPosition;

	// Screen-space UV for the SSAO lookup. Clip y is +1 at the top row under
	// NVRHI's DX-style viewport, so the texture v coordinate flips.
	vScreenUv = gl_Position.xy / gl_Position.w * vec2(0.5, -0.5) + 0.5;
}
