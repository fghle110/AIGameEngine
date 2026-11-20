#pragma once

#include "Aegis/Core/Base.h"

#include <glm/glm.hpp>

#include <string>
#include <string_view>
#include <vector>

namespace aegis
{
	// Vertex layout consumed by the SceneRenderer PBR pipeline. Tangent.w holds
	// the handedness sign so the bitangent can be rebuilt in the shader, which is
	// also the glTF convention.
	struct Vertex
	{
		glm::vec3 Position{ 0.0f, 0.0f, 0.0f };
		glm::vec3 Normal{ 0.0f, 1.0f, 0.0f };
		glm::vec4 Tangent{ 1.0f, 0.0f, 0.0f, 1.0f };
		glm::vec2 Uv{ 0.0f, 0.0f };
	};

	// A contiguous index range inside a Mesh drawn with exactly one material, so
	// one imported glTF primitive becomes one Submesh.
	struct Submesh
	{
		u32 FirstIndex = 0;
		u32 IndexCount = 0;
		u32 FirstVertex = 0;
		u32 VertexCount = 0;
		u32 MaterialIndex = 0;
	};

	enum class PrimitiveType : u8
	{
		Cube = 0,
		Plane,
		Sphere,
		Cylinder
	};

	std::string_view PrimitiveTypeToString(PrimitiveType type);

	// Generates tangents for a loose vertex/index block (Lengyel's method). The
	// importer assembles primitives before they become one Mesh, so it needs the
	// algorithm on its own.
	void GenerateTangents(std::vector<Vertex>& vertices, const std::vector<u32>& indices);
	// Area-weighted vertex normals, used when an imported primitive has none.
	void GenerateNormals(std::vector<Vertex>& vertices, const std::vector<u32>& indices);

	// Indexed triangle mesh. Builders produce geometry centred on the origin so a
	// transform's scale directly expresses the size of the object.
	class Mesh
	{
	public:
		// Unit cube: 1 metre edges, 24 vertices (one per face corner) and 36 indices.
		static Mesh CreateCube(f32 size = 1.0f);
		// Ground-style plane in the XZ plane facing +Y. Subdivisions split each axis.
		static Mesh CreatePlane(f32 size = 1.0f, u32 subdivisions = 1);
		// UV sphere; rings are latitude bands, segments are longitude slices.
		static Mesh CreateSphere(f32 radius = 0.5f, u32 segments = 32, u32 rings = 16);
		// Capped cylinder along the Y axis, centred on the origin.
		static Mesh CreateCylinder(f32 radius = 0.5f, f32 height = 1.0f, u32 segments = 32);
		static Mesh CreatePrimitive(PrimitiveType type);

		bool IsValid() const;
		usize GetTriangleCount() const;

		const std::vector<Vertex>& GetVertices() const { return m_Vertices; }
		const std::vector<u32>& GetIndices() const { return m_Indices; }
		const std::vector<Submesh>& GetSubmeshes() const { return m_Submeshes; }

		std::vector<Vertex>& GetVertices() { return m_Vertices; }
		std::vector<u32>& GetIndices() { return m_Indices; }
		std::vector<Submesh>& GetSubmeshes() { return m_Submeshes; }

		// Appends one draw range. The importer calls this per glTF primitive.
		void AddSubmesh(const Submesh& submesh);

		glm::vec3 GetBoundsMin() const { return m_BoundsMin; }
		glm::vec3 GetBoundsMax() const { return m_BoundsMax; }
		glm::vec3 GetBoundsCenter() const { return (m_BoundsMin + m_BoundsMax) * 0.5f; }
		glm::vec3 GetBoundsSize() const { return m_BoundsMax - m_BoundsMin; }

		const std::string& GetName() const { return m_Name; }
		void SetName(std::string name) { m_Name = std::move(name); }

		// Rebuilds per-vertex tangents from positions, normals and UVs (Lengyel's
		// method). The importer calls this when the source mesh has none.
		void GenerateTangents();
		void RecalculateBounds();
		void Clear();

	private:
		std::vector<Vertex> m_Vertices;
		std::vector<u32> m_Indices;
		std::vector<Submesh> m_Submeshes;
		glm::vec3 m_BoundsMin{ 0.0f };
		glm::vec3 m_BoundsMax{ 0.0f };
		std::string m_Name;
	};
}
