// Constants of the depth-only pass. The shadow pass fills this with the light's
// view-projection, the SSAO depth prepass with the camera's.

layout(set = 0, binding = 0, std140) uniform SceneDepthConstants
{
	mat4 uViewProjection;
} depthFrame;
