#include "Aegis/Assets/Mesh.h"

#include <glm/gtc/constants.hpp>

#include <algorithm>
#include <cmath>

namespace aegis
{
	namespace
	{
		// Picks an axis that is not parallel to the normal so a degenerate tangent
		// basis can still be built. Poles and flat caps hit this path.
		glm::vec3 PickOrthogonalAxis(const glm::vec3& normal)
		{
			const glm::vec3 axis = std::abs(normal.y) < 0.9f ? glm::vec3(0.0f, 1.0f, 0.0f) : glm::vec3(1.0f, 0.0f, 0.0f);
			return axis;
		}
	}

	std::string_view PrimitiveTypeToString(PrimitiveType type)
	{
		switch (type)
		{
			case PrimitiveType::Cube: return "Cube";
			case PrimitiveType::Plane: return "Plane";
			case PrimitiveType::Sphere: return "Sphere";
			case PrimitiveType::Cylinder: return "Cylinder";
		}
		return "Unknown";
	}

	bool Mesh::IsValid() const
	{
		return !m_Vertices.empty() && !m_Indices.empty() && !m_Submeshes.empty();
	}

	usize Mesh::GetTriangleCount() const
	{
		return m_Indices.size() / 3;
	}

	void Mesh::AddSubmesh(const Submesh& submesh)
	{
		m_Submeshes.push_back(submesh);
	}

	void Mesh::Clear()
	{
		m_Vertices.clear();
		m_Indices.clear();
		m_Submeshes.clear();
		m_BoundsMin = glm::vec3(0.0f);
		m_BoundsMax = glm::vec3(0.0f);
		m_Name.clear();
	}

	void Mesh::RecalculateBounds()
	{
		if (m_Vertices.empty())
		{
			m_BoundsMin = glm::vec3(0.0f);
			m_BoundsMax = glm::vec3(0.0f);
			return;
		}

		m_BoundsMin = m_Vertices[0].Position;
		m_BoundsMax = m_Vertices[0].Position;
		for (const Vertex& vertex : m_Vertices)
		{
			m_BoundsMin = glm::min(m_BoundsMin, vertex.Position);
			m_BoundsMax = glm::max(m_BoundsMax, vertex.Position);
		}
	}

	Mesh Mesh::CreateCube(f32 size)
	{
		Mesh mesh;
		mesh.SetName("Cube");

		const f32 half = std::abs(size) * 0.5f;
		if (half <= 0.0f)
		{
			return mesh;
		}

		// One quad per face: the normal is constant, so the four corners cannot be
		// shared with the neighbouring faces.
		struct Face
		{
			glm::vec3 Normal;
			glm::vec3 Tangent;
			glm::vec3 Bitangent;
		};
		const Face faces[6] = {
			{ { 1.0f, 0.0f, 0.0f }, { 0.0f, 0.0f, -1.0f }, { 0.0f, 1.0f, 0.0f } },
			{ { -1.0f, 0.0f, 0.0f }, { 0.0f, 0.0f, 1.0f }, { 0.0f, 1.0f, 0.0f } },
			{ { 0.0f, 1.0f, 0.0f }, { 1.0f, 0.0f, 0.0f }, { 0.0f, 0.0f, -1.0f } },
			{ { 0.0f, -1.0f, 0.0f }, { 1.0f, 0.0f, 0.0f }, { 0.0f, 0.0f, 1.0f } },
			{ { 0.0f, 0.0f, 1.0f }, { 1.0f, 0.0f, 0.0f }, { 0.0f, 1.0f, 0.0f } },
			{ { 0.0f, 0.0f, -1.0f }, { -1.0f, 0.0f, 0.0f }, { 0.0f, 1.0f, 0.0f } },
		};

		mesh.m_Vertices.reserve(24);
		mesh.m_Indices.reserve(36);

		for (const Face& face : faces)
		{
			const u32 baseVertex = static_cast<u32>(mesh.m_Vertices.size());
			for (u32 corner = 0; corner < 4; ++corner)
			{
				const f32 signU = (corner == 0 || corner == 3) ? -1.0f : 1.0f;
				const f32 signV = (corner == 0 || corner == 1) ? -1.0f : 1.0f;

				Vertex vertex;
				vertex.Position = face.Normal * half + face.Tangent * (signU * half) + face.Bitangent * (signV * half);
				vertex.Normal = face.Normal;
				// cross(Normal, Tangent) == Bitangent for every face above, so the
				// handedness encoded in Tangent.w is always +1.
				vertex.Tangent = glm::vec4(face.Tangent, 1.0f);
				vertex.Uv = glm::vec2((signU + 1.0f) * 0.5f, (signV + 1.0f) * 0.5f);
				mesh.m_Vertices.push_back(vertex);
			}

			mesh.m_Indices.push_back(baseVertex + 0);
			mesh.m_Indices.push_back(baseVertex + 1);
			mesh.m_Indices.push_back(baseVertex + 2);
			mesh.m_Indices.push_back(baseVertex + 0);
			mesh.m_Indices.push_back(baseVertex + 2);
			mesh.m_Indices.push_back(baseVertex + 3);
		}

		mesh.m_Submeshes.push_back(Submesh{ 0, static_cast<u32>(mesh.m_Indices.size()), 0,
			static_cast<u32>(mesh.m_Vertices.size()), 0 });
		mesh.RecalculateBounds();
		return mesh;
	}

	Mesh Mesh::CreatePlane(f32 size, u32 subdivisions)
	{
		Mesh mesh;
		mesh.SetName("Plane");

		const f32 half = std::abs(size) * 0.5f;
		if (half <= 0.0f)
		{
			return mesh;
		}
		const u32 cells = Max(subdivisions, 1u);
		const u32 columns = cells + 1;

		mesh.m_Vertices.reserve(static_cast<usize>(columns) * columns);
		for (u32 row = 0; row < columns; ++row)
		{
			const f32 v = static_cast<f32>(row) / static_cast<f32>(cells);
			for (u32 column = 0; column < columns; ++column)
			{
				const f32 u = static_cast<f32>(column) / static_cast<f32>(cells);

				Vertex vertex;
				vertex.Position = glm::vec3(-half + u * size, 0.0f, -half + v * size);
				vertex.Normal = glm::vec3(0.0f, 1.0f, 0.0f);
				// dP/du = +X and dP/dv = +Z, while cross(Normal, +X) = -Z: the stored
				// bitangent must point towards -Z, so the handedness sign is -1.
				vertex.Tangent = glm::vec4(1.0f, 0.0f, 0.0f, -1.0f);
				vertex.Uv = glm::vec2(u, v);
				mesh.m_Vertices.push_back(vertex);
			}
		}

		mesh.m_Indices.reserve(static_cast<usize>(cells) * cells * 6);
		for (u32 row = 0; row < cells; ++row)
		{
			for (u32 column = 0; column < cells; ++column)
			{
				const u32 v00 = row * columns + column;
				const u32 v10 = v00 + 1;
				const u32 v01 = v00 + columns;
				const u32 v11 = v01 + 1;

				// Counter-clockwise when viewed from +Y so the face normal is +Y.
				mesh.m_Indices.push_back(v00);
				mesh.m_Indices.push_back(v11);
				mesh.m_Indices.push_back(v10);
				mesh.m_Indices.push_back(v00);
				mesh.m_Indices.push_back(v01);
				mesh.m_Indices.push_back(v11);
			}
		}

		mesh.m_Submeshes.push_back(Submesh{ 0, static_cast<u32>(mesh.m_Indices.size()), 0,
			static_cast<u32>(mesh.m_Vertices.size()), 0 });
		mesh.RecalculateBounds();
		return mesh;
	}

	Mesh Mesh::CreateSphere(f32 radius, u32 segments, u32 rings)
	{
		Mesh mesh;
		mesh.SetName("Sphere");

		const f32 r = std::abs(radius);
		const u32 slices = Max(segments, 3u);
		const u32 bands = Max(rings, 2u);
		if (r <= 0.0f)
		{
			return mesh;
		}

		const u32 columns = slices + 1;
		mesh.m_Vertices.reserve(static_cast<usize>(columns) * (bands + 1));
		for (u32 ring = 0; ring <= bands; ++ring)
		{
			const f32 phi = glm::pi<f32>() * static_cast<f32>(ring) / static_cast<f32>(bands);
			const f32 sinPhi = std::sin(phi);
			const f32 cosPhi = std::cos(phi);
			for (u32 segment = 0; segment <= slices; ++segment)
			{
				const f32 theta = glm::two_pi<f32>() * static_cast<f32>(segment) / static_cast<f32>(slices);
				const glm::vec3 direction(sinPhi * std::cos(theta), cosPhi, sinPhi * std::sin(theta));

				Vertex vertex;
				vertex.Position = direction * r;
				vertex.Normal = direction;
				vertex.Uv = glm::vec2(static_cast<f32>(segment) / static_cast<f32>(slices),
					static_cast<f32>(ring) / static_cast<f32>(bands));
				mesh.m_Vertices.push_back(vertex);
			}
		}

		for (u32 ring = 0; ring < bands; ++ring)
		{
			for (u32 segment = 0; segment < slices; ++segment)
			{
				const u32 topLeft = ring * columns + segment;
				const u32 topRight = topLeft + 1;
				const u32 bottomLeft = topLeft + columns;
				const u32 bottomRight = bottomLeft + 1;

				// The pole rows collapse to a single point, so one triangle of each
				// pole quad would be degenerate and is skipped.
				if (ring > 0)
				{
					mesh.m_Indices.push_back(topLeft);
					mesh.m_Indices.push_back(topRight);
					mesh.m_Indices.push_back(bottomLeft);
				}
				if (ring + 1 < bands)
				{
					mesh.m_Indices.push_back(topRight);
					mesh.m_Indices.push_back(bottomRight);
					mesh.m_Indices.push_back(bottomLeft);
				}
			}
		}

		mesh.m_Submeshes.push_back(Submesh{ 0, static_cast<u32>(mesh.m_Indices.size()), 0,
			static_cast<u32>(mesh.m_Vertices.size()), 0 });
		mesh.GenerateTangents();
		mesh.RecalculateBounds();
		return mesh;
	}

	Mesh Mesh::CreateCylinder(f32 radius, f32 height, u32 segments)
	{
		Mesh mesh;
		mesh.SetName("Cylinder");

		const f32 r = std::abs(radius);
		const f32 h = std::abs(height);
		const u32 slices = Max(segments, 3u);
		if (r <= 0.0f || h <= 0.0f)
		{
			return mesh;
		}

		const f32 halfHeight = h * 0.5f;
		const u32 columns = slices + 1;

		// Side wall: normals point radially outwards, matching the analytic normal.
		for (u32 row = 0; row < 2; ++row)
		{
			const f32 y = row == 0 ? halfHeight : -halfHeight;
			for (u32 segment = 0; segment <= slices; ++segment)
			{
				const f32 theta = glm::two_pi<f32>() * static_cast<f32>(segment) / static_cast<f32>(slices);
				const glm::vec3 normal(std::cos(theta), 0.0f, std::sin(theta));

				Vertex vertex;
				vertex.Position = glm::vec3(normal.x * r, y, normal.z * r);
				vertex.Normal = normal;
				vertex.Uv = glm::vec2(static_cast<f32>(segment) / static_cast<f32>(slices), static_cast<f32>(row));
				mesh.m_Vertices.push_back(vertex);
			}
		}

		for (u32 segment = 0; segment < slices; ++segment)
		{
			const u32 topLeft = segment;
			const u32 topRight = segment + 1;
			const u32 bottomLeft = columns + segment;
			const u32 bottomRight = bottomLeft + 1;

			mesh.m_Indices.push_back(topLeft);
			mesh.m_Indices.push_back(topRight);
			mesh.m_Indices.push_back(bottomLeft);
			mesh.m_Indices.push_back(topRight);
			mesh.m_Indices.push_back(bottomRight);
			mesh.m_Indices.push_back(bottomLeft);
		}

		// Caps: a centre vertex plus one fan ring each. Top winds clockwise when
		// seen from +Y so its geometric normal points up.
		const u32 topCenter = static_cast<u32>(mesh.m_Vertices.size());
		{
			Vertex center;
			center.Position = glm::vec3(0.0f, halfHeight, 0.0f);
			center.Normal = glm::vec3(0.0f, 1.0f, 0.0f);
			center.Uv = glm::vec2(0.5f, 0.5f);
			mesh.m_Vertices.push_back(center);
		}
		const u32 topRing = static_cast<u32>(mesh.m_Vertices.size());
		for (u32 segment = 0; segment <= slices; ++segment)
		{
			const f32 theta = glm::two_pi<f32>() * static_cast<f32>(segment) / static_cast<f32>(slices);
			Vertex vertex;
			vertex.Position = glm::vec3(std::cos(theta) * r, halfHeight, std::sin(theta) * r);
			vertex.Normal = glm::vec3(0.0f, 1.0f, 0.0f);
			vertex.Uv = glm::vec2(std::cos(theta) * 0.5f + 0.5f, std::sin(theta) * 0.5f + 0.5f);
			mesh.m_Vertices.push_back(vertex);
		}
		for (u32 segment = 0; segment < slices; ++segment)
		{
			mesh.m_Indices.push_back(topCenter);
			mesh.m_Indices.push_back(topRing + segment + 1);
			mesh.m_Indices.push_back(topRing + segment);
		}

		const u32 bottomCenter = static_cast<u32>(mesh.m_Vertices.size());
		{
			Vertex center;
			center.Position = glm::vec3(0.0f, -halfHeight, 0.0f);
			center.Normal = glm::vec3(0.0f, -1.0f, 0.0f);
			center.Uv = glm::vec2(0.5f, 0.5f);
			mesh.m_Vertices.push_back(center);
		}
		const u32 bottomRing = static_cast<u32>(mesh.m_Vertices.size());
		for (u32 segment = 0; segment <= slices; ++segment)
		{
			const f32 theta = glm::two_pi<f32>() * static_cast<f32>(segment) / static_cast<f32>(slices);
			Vertex vertex;
			vertex.Position = glm::vec3(std::cos(theta) * r, -halfHeight, std::sin(theta) * r);
			vertex.Normal = glm::vec3(0.0f, -1.0f, 0.0f);
			vertex.Uv = glm::vec2(std::cos(theta) * 0.5f + 0.5f, std::sin(theta) * 0.5f + 0.5f);
			mesh.m_Vertices.push_back(vertex);
		}
		for (u32 segment = 0; segment < slices; ++segment)
		{
			mesh.m_Indices.push_back(bottomCenter);
			mesh.m_Indices.push_back(bottomRing + segment);
			mesh.m_Indices.push_back(bottomRing + segment + 1);
		}

		mesh.m_Submeshes.push_back(Submesh{ 0, static_cast<u32>(mesh.m_Indices.size()), 0,
			static_cast<u32>(mesh.m_Vertices.size()), 0 });
		mesh.GenerateTangents();
		mesh.RecalculateBounds();
		return mesh;
	}

	Mesh Mesh::CreatePrimitive(PrimitiveType type)
	{
		switch (type)
		{
			case PrimitiveType::Cube: return CreateCube();
			case PrimitiveType::Plane: return CreatePlane();
			case PrimitiveType::Sphere: return CreateSphere();
			case PrimitiveType::Cylinder: return CreateCylinder();
		}
		return Mesh();
	}

	void GenerateTangents(std::vector<Vertex>& vertices, const std::vector<u32>& indices)
	{
		if (vertices.empty() || indices.size() < 3)
		{
			return;
		}

		std::vector<glm::vec3> tangentSum(vertices.size(), glm::vec3(0.0f));
		std::vector<glm::vec3> bitangentSum(vertices.size(), glm::vec3(0.0f));

		for (usize index = 0; index + 2 < indices.size(); index += 3)
		{
			Vertex& v0 = vertices[indices[index + 0]];
			Vertex& v1 = vertices[indices[index + 1]];
			Vertex& v2 = vertices[indices[index + 2]];

			const glm::vec3 edge1 = v1.Position - v0.Position;
			const glm::vec3 edge2 = v2.Position - v0.Position;
			const glm::vec2 deltaUv1 = v1.Uv - v0.Uv;
			const glm::vec2 deltaUv2 = v2.Uv - v0.Uv;

			const f32 determinant = deltaUv1.x * deltaUv2.y - deltaUv1.y * deltaUv2.x;
			if (std::abs(determinant) < 1.0e-8f)
			{
				continue; // degenerate UV triangle, the fallback below handles the vertex
			}
			const f32 scale = 1.0f / determinant;
			const glm::vec3 tangent = (edge1 * deltaUv2.y - edge2 * deltaUv1.y) * scale;
			const glm::vec3 bitangent = (edge2 * deltaUv1.x - edge1 * deltaUv2.x) * scale;

			const u32 triangleIndices[3] = { indices[index + 0], indices[index + 1], indices[index + 2] };
			for (const u32 vertexIndex : triangleIndices)
			{
				tangentSum[vertexIndex] += tangent;
				bitangentSum[vertexIndex] += bitangent;
			}
		}

		for (usize i = 0; i < vertices.size(); ++i)
		{
			Vertex& vertex = vertices[i];
			const glm::vec3 normal = glm::length(vertex.Normal) > 0.0f ? glm::normalize(vertex.Normal)
																	  : glm::vec3(0.0f, 1.0f, 0.0f);

			glm::vec3 tangent = tangentSum[i] - normal * glm::dot(normal, tangentSum[i]);
			if (glm::length(tangent) < 1.0e-6f)
			{
				tangent = glm::cross(PickOrthogonalAxis(normal), normal);
			}
			tangent = glm::normalize(tangent);

			const glm::vec3 bitangent = glm::cross(normal, tangent);
			const f32 handedness = glm::dot(bitangent, bitangentSum[i]) < 0.0f ? -1.0f : 1.0f;
			vertex.Tangent = glm::vec4(tangent, handedness);
		}
	}

	void GenerateNormals(std::vector<Vertex>& vertices, const std::vector<u32>& indices)
	{
		if (vertices.empty())
		{
			return;
		}

		std::vector<glm::vec3> accumulated(vertices.size(), glm::vec3(0.0f));
		for (usize index = 0; index + 2 < indices.size(); index += 3)
		{
			const u32 i0 = indices[index + 0];
			const u32 i1 = indices[index + 1];
			const u32 i2 = indices[index + 2];
			if (i0 >= vertices.size() || i1 >= vertices.size() || i2 >= vertices.size())
			{
				continue;
			}

			const glm::vec3& p0 = vertices[i0].Position;
			const glm::vec3& p1 = vertices[i1].Position;
			const glm::vec3& p2 = vertices[i2].Position;
			// Unnormalised cross product: the triangle area weights the contribution.
			const glm::vec3 faceNormal = glm::cross(p1 - p0, p2 - p0);
			accumulated[i0] += faceNormal;
			accumulated[i1] += faceNormal;
			accumulated[i2] += faceNormal;
		}

		for (usize i = 0; i < vertices.size(); ++i)
		{
			vertices[i].Normal = glm::length(accumulated[i]) > 1.0e-8f ? glm::normalize(accumulated[i])
																	  : glm::vec3(0.0f, 1.0f, 0.0f);
		}
	}

	void Mesh::GenerateTangents()
	{
		aegis::GenerateTangents(m_Vertices, m_Indices);
	}
}
