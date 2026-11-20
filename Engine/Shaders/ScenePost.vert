#version 460

// Fullscreen triangle for the post-processing passes. The vertical flip keeps the
// sampled image upright: NVRHI renders with a DX-style viewport, so NDC +y is the
// top row of the target and texture v = 0 is that same first row.

layout(location = 0) out vec2 vTexCoord;

void main()
{
	const vec2 position = vec2(float((gl_VertexIndex << 1) & 2), float(gl_VertexIndex & 2));
	vTexCoord = vec2(position.x, 1.0 - position.y);
	gl_Position = vec4(position * 2.0 - 1.0, 0.0, 1.0);
}
