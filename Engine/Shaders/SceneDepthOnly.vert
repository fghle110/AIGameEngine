#version 460

// Depth-only geometry pass: shadow map rendering and the camera depth prepass.
// Both write nothing but depth, so the fragment stage is omitted entirely.

#extension GL_GOOGLE_include_directive : require
#include "SceneDepthConstants.glsl"
#include "SceneObjectConstants.glsl"

layout(location = 0) in vec3 aPosition;

void main()
{
	gl_Position = depthFrame.uViewProjection * object.uModel * vec4(aPosition, 1.0);
}
