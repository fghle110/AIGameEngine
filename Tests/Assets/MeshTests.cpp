#include "TestSupport.h"

#include <doctest/doctest.h>

#include "Aegis/Assets/Mesh.h"

#include <glm/glm.hpp>

#include <algorithm>
#include <cmath>

using namespace aegis;

namespace
{
	glm::vec3 TriangleNormal(const Mesh& mesh, usize triangleIndex)
	{
		const std::vector<u32>& indices = mesh.GetIndices();
		const std::vector<Vertex>& vertices = mesh.GetVertices();
		const glm::vec3& a = vertices[indices[triangleIndex * 3 + 0]].Position;
		const glm::vec3& b = vertices[indices[triangleIndex * 3 + 1]].Position;
		const glm::vec3& c = vertices[indices[triangleIndex * 3 + 2]].Position;
		return glm::cross(b - a, c - a);
	}

	void CheckIndicesInRange(const Mesh& mesh)
	{
		const usize vertexCount = mesh.GetVertices().size();
		for (const u32 index : mesh.GetIndices())
		{
			CHECK(index < vertexCount);
		}
		CHECK(mesh.GetIndices().size() % 3 == 0);
	}

	void CheckUnitFrames(const Mesh& mesh)
	{
		for (const Vertex& vertex : mesh.GetVertices())
		{
			CHECK(glm::length(vertex.Normal) == doctest::Approx(1.0f).epsilon(0.001));
			CHECK(glm::length(glm::vec3(vertex.Tangent)) == doctest::Approx(1.0f).epsilon(0.001));
			CHECK(std::abs(glm::dot(vertex.Normal, glm::vec3(vertex.Tangent))) < 1.0e-3f);
			CHECK(std::abs(vertex.Tangent.w) == doctest::Approx(1.0f));
		}
	}

	// A convex primitive is correctly wound when every triangle faces the same way as
	// the normals of its own vertices.
	void CheckOutwardWinding(const Mesh& mesh)
	{
		const std::vector<u32>& indices = mesh.GetIndices();
		for (usize triangle = 0; triangle + 2 < indices.size(); triangle += 3)
		{
			const glm::vec3 geometric = TriangleNormal(mesh, triangle / 3);
			if (glm::length(geometric) < 1.0e-9f)
			{
				continue; // degenerate triangle at a pole or cap centre
			}

			const glm::vec3 vertexNormals = mesh.GetVertices()[indices[triangle + 0]].Normal +
				mesh.GetVertices()[indices[triangle + 1]].Normal + mesh.GetVertices()[indices[triangle + 2]].Normal;
			if (glm::length(vertexNormals) < 1.0e-6f)
			{
				continue;
			}
			CHECK(glm::dot(glm::normalize(geometric), glm::normalize(vertexNormals)) > 0.0f);
		}
	}
}

TEST_CASE("Mesh: cube is a centred unit cube with per-face normals")
{
	const Mesh cube = Mesh::CreateCube(2.0f);
	REQUIRE(cube.IsValid());
	CHECK(cube.GetVertices().size() == 24);
	CHECK(cube.GetIndices().size() == 36);
	CHECK(cube.GetTriangleCount() == 12);
	REQUIRE(cube.GetSubmeshes().size() == 1);
	CHECK(cube.GetSubmeshes()[0].IndexCount == 36);
	CHECK(cube.GetSubmeshes()[0].MaterialIndex == 0);
	CHECK(cube.GetBoundsMin() == glm::vec3(-1.0f));
	CHECK(cube.GetBoundsMax() == glm::vec3(1.0f));
	CHECK(cube.GetBoundsCenter() == glm::vec3(0.0f));
	CHECK(cube.GetBoundsSize() == glm::vec3(2.0f));

	CheckIndicesInRange(cube);
	CheckUnitFrames(cube);
	CheckOutwardWinding(cube);

	// Six faces, four distinct normals per axis pair.
	usize positiveX = 0;
	for (const Vertex& vertex : cube.GetVertices())
	{
		if (vertex.Normal == glm::vec3(1.0f, 0.0f, 0.0f))
		{
			++positiveX;
		}
	}
	CHECK(positiveX == 4);
}

TEST_CASE("Mesh: cube rejects a non-positive size")
{
	const Mesh cube = Mesh::CreateCube(0.0f);
	CHECK_FALSE(cube.IsValid());
	CHECK(cube.GetVertices().empty());
	CHECK(cube.GetIndices().empty());
}

TEST_CASE("Mesh: plane lies in the XZ plane facing up")
{
	const Mesh plane = Mesh::CreatePlane(4.0f, 2);
	REQUIRE(plane.IsValid());
	CHECK(plane.GetVertices().size() == 9);
	CHECK(plane.GetIndices().size() == 24);
	CHECK(plane.GetBoundsMin() == glm::vec3(-2.0f, 0.0f, -2.0f));
	CHECK(plane.GetBoundsMax() == glm::vec3(2.0f, 0.0f, 2.0f));
	for (const Vertex& vertex : plane.GetVertices())
	{
		CHECK(vertex.Normal == glm::vec3(0.0f, 1.0f, 0.0f));
		CHECK(vertex.Position.y == doctest::Approx(0.0f));
	}

	CheckIndicesInRange(plane);
	CheckUnitFrames(plane);
	CheckOutwardWinding(plane);
}

TEST_CASE("Mesh: sphere keeps radial normals and skips the pole degenerates")
{
	const Mesh sphere = Mesh::CreateSphere(2.0f, 16, 8);
	REQUIRE(sphere.IsValid());

	for (const Vertex& vertex : sphere.GetVertices())
	{
		CHECK(glm::length(vertex.Position) == doctest::Approx(2.0f).epsilon(0.001));
		const glm::vec3 radial = glm::normalize(vertex.Position);
		CHECK(glm::length(radial - vertex.Normal) < 1.0e-3f);
	}
	CHECK(sphere.GetBoundsMin().y == doctest::Approx(-2.0f));
	CHECK(sphere.GetBoundsMax().y == doctest::Approx(2.0f));

	// 16 slices x (8 bands x 2 - 2 pole triangles) triangles.
	CHECK(sphere.GetTriangleCount() == 16 * (8 * 2 - 2));
	for (usize triangle = 0; triangle < sphere.GetTriangleCount(); ++triangle)
	{
		CHECK(glm::length(TriangleNormal(sphere, triangle)) > 0.0f);
	}

	CheckIndicesInRange(sphere);
	CheckUnitFrames(sphere);
	CheckOutwardWinding(sphere);
}

TEST_CASE("Mesh: cylinder has side wall and both caps")
{
	const Mesh cylinder = Mesh::CreateCylinder(1.0f, 2.0f, 12);
	REQUIRE(cylinder.IsValid());
	CHECK(cylinder.GetBoundsMin() == glm::vec3(-1.0f, -1.0f, -1.0f));
	CHECK(cylinder.GetBoundsMax() == glm::vec3(1.0f, 1.0f, 1.0f));

	// 12 side quads = 24 triangles, plus two caps of 12 triangles each.
	CHECK(cylinder.GetTriangleCount() == 24 + 24);
	CheckIndicesInRange(cylinder);
	CheckUnitFrames(cylinder);
	CheckOutwardWinding(cylinder);
}

TEST_CASE("Mesh: primitive factory maps every enum value")
{
	for (const PrimitiveType type : { PrimitiveType::Cube, PrimitiveType::Plane, PrimitiveType::Sphere,
			 PrimitiveType::Cylinder })
	{
		const Mesh mesh = Mesh::CreatePrimitive(type);
		CHECK(mesh.IsValid());
		CHECK(mesh.GetName() == PrimitiveTypeToString(type));
		CHECK(mesh.GetSubmeshes().size() == 1);
	}
}

TEST_CASE("Mesh: generated tangents stay orthonormal to the normal")
{
	Mesh mesh = Mesh::CreatePlane(1.0f, 3);
	for (Vertex& vertex : mesh.GetVertices())
	{
		vertex.Tangent = glm::vec4(0.0f);
	}
	mesh.GenerateTangents();
	CheckUnitFrames(mesh);

	// dP/du runs along +X, and cross(Normal, +X) points towards -Z while dP/dv is +Z,
	// so the handedness sign is -1.
	for (const Vertex& vertex : mesh.GetVertices())
	{
		CHECK(vertex.Tangent.x == doctest::Approx(1.0f).epsilon(0.001));
		CHECK(vertex.Tangent.w == doctest::Approx(-1.0f));
	}
}

TEST_CASE("Mesh: degenerate tangent frames still produce a unit basis")
{
	std::vector<Vertex> vertices(3);
	vertices[0].Position = glm::vec3(0.0f, 0.0f, 0.0f);
	vertices[1].Position = glm::vec3(1.0f, 0.0f, 0.0f);
	vertices[2].Position = glm::vec3(0.0f, 1.0f, 0.0f);
	// All UVs equal: the UV determinant is zero, so the fallback frame must kick in.
	const std::vector<u32> indices = { 0, 1, 2 };
	GenerateTangents(vertices, indices);
	for (const Vertex& vertex : vertices)
	{
		CHECK(glm::length(glm::vec3(vertex.Tangent)) == doctest::Approx(1.0f).epsilon(0.001));
		CHECK(std::abs(glm::dot(vertex.Normal, glm::vec3(vertex.Tangent))) < 1.0e-3f);
	}
}

TEST_CASE("Mesh: generated normals follow the triangle winding")
{
	std::vector<Vertex> vertices(3);
	vertices[0].Position = glm::vec3(0.0f, 0.0f, 0.0f);
	vertices[1].Position = glm::vec3(1.0f, 0.0f, 0.0f);
	vertices[2].Position = glm::vec3(0.0f, 0.0f, -1.0f);
	const std::vector<u32> indices = { 0, 1, 2 };
	GenerateNormals(vertices, indices);
	// cross((1,0,0), (0,0,-1)) = (0,1,0)
	for (const Vertex& vertex : vertices)
	{
		CHECK(vertex.Normal.y == doctest::Approx(1.0f).epsilon(0.001));
	}

	// Flipping the winding flips the normal.
	GenerateNormals(vertices, std::vector<u32>{ 0, 2, 1 });
	for (const Vertex& vertex : vertices)
	{
		CHECK(vertex.Normal.y == doctest::Approx(-1.0f).epsilon(0.001));
	}
}

TEST_CASE("Mesh: submeshes can be appended and cleared")
{
	Mesh mesh = Mesh::CreateCube();
	Submesh extra;
	extra.FirstIndex = 0;
	extra.IndexCount = 3;
	extra.MaterialIndex = 1;
	mesh.AddSubmesh(extra);
	CHECK(mesh.GetSubmeshes().size() == 2);

	mesh.Clear();
	CHECK_FALSE(mesh.IsValid());
	CHECK(mesh.GetSubmeshes().empty());
	CHECK(mesh.GetBoundsMin() == glm::vec3(0.0f));
}
