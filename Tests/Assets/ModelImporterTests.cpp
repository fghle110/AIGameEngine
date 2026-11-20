#include "TestSupport.h"

#include <doctest/doctest.h>

#include "Aegis/Assets/ModelImporter.h"

#include <stb_image_write.h>

#include <glm/glm.hpp>

#include <cstring>
#include <format>
#include <string>
#include <vector>

using namespace aegis;

namespace
{
	FileSystem::Path TestAssetPath(const char* fileName)
	{
		return FileSystem::Path("TestAssets") / FileSystem::Path(fileName);
	}

	std::string EncodeBase64(const std::vector<u8>& bytes)
	{
		static const char c_Alphabet[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
		std::string encoded;
		encoded.reserve(((bytes.size() + 2) / 3) * 4);

		usize index = 0;
		while (index + 2 < bytes.size())
		{
			const u32 triple = (static_cast<u32>(bytes[index]) << 16) | (static_cast<u32>(bytes[index + 1]) << 8) |
				static_cast<u32>(bytes[index + 2]);
			encoded.push_back(c_Alphabet[(triple >> 18) & 0x3F]);
			encoded.push_back(c_Alphabet[(triple >> 12) & 0x3F]);
			encoded.push_back(c_Alphabet[(triple >> 6) & 0x3F]);
			encoded.push_back(c_Alphabet[triple & 0x3F]);
			index += 3;
		}

		const usize remaining = bytes.size() - index;
		if (remaining == 1)
		{
			const u32 triple = static_cast<u32>(bytes[index]) << 16;
			encoded.push_back(c_Alphabet[(triple >> 18) & 0x3F]);
			encoded.push_back(c_Alphabet[(triple >> 12) & 0x3F]);
			encoded += "==";
		}
		else if (remaining == 2)
		{
			const u32 triple = (static_cast<u32>(bytes[index]) << 16) | (static_cast<u32>(bytes[index + 1]) << 8);
			encoded.push_back(c_Alphabet[(triple >> 18) & 0x3F]);
			encoded.push_back(c_Alphabet[(triple >> 12) & 0x3F]);
			encoded.push_back(c_Alphabet[(triple >> 6) & 0x3F]);
			encoded += '=';
		}
		return encoded;
	}

	void AppendFloats(std::vector<u8>& buffer, const std::vector<f32>& values)
	{
		for (const f32 value : values)
		{
			u8 bytes[sizeof(f32)];
			std::memcpy(bytes, &value, sizeof(f32));
			buffer.insert(buffer.end(), bytes, bytes + sizeof(f32));
		}
	}

	void AppendU16(std::vector<u8>& buffer, const std::vector<u16>& values)
	{
		for (const u16 value : values)
		{
			u8 bytes[sizeof(u16)];
			std::memcpy(bytes, &value, sizeof(u16));
			buffer.insert(buffer.end(), bytes, bytes + sizeof(u16));
		}
	}

	// One triangle in the XY plane, UV mapped, with a 2x2 PNG embedded as a data URI.
	std::string MinimalGltf(const std::string& imageUri, const std::string& bufferUri)
	{
		return std::format(R"({{
	"asset": {{ "version": "2.0", "generator": "Aegis tests" }},
	"scene": 0,
	"scenes": [{{ "nodes": [0] }}],
	"nodes": [{{ "name": "Node", "mesh": 0, "translation": [1.0, 2.0, 3.0] }}],
	"meshes": [{{
		"name": "Triangle",
		"primitives": [{{
			"attributes": {{ "POSITION": 0, "TEXCOORD_0": 1 }},
			"indices": 2,
			"material": 0
		}}]
	}}],
	"materials": [{{
		"name": "Imported",
		"pbrMetallicRoughness": {{
			"baseColorFactor": [0.5, 0.25, 0.125, 1.0],
			"baseColorTexture": {{ "index": 0 }},
			"metallicFactor": 0.75,
			"roughnessFactor": 0.25
		}},
		"normalTexture": {{ "index": 1, "scale": 0.5 }},
		"emissiveFactor": [1.0, 0.0, 0.0]
	}}],
	"textures": [{{ "source": 0, "sampler": 0 }}, {{ "source": 0 }}],
	"samplers": [{{ "magFilter": 9729, "minFilter": 9987, "wrapS": 10497, "wrapT": 33071 }}],
	"images": [{{ "uri": "{}" }}],
	"accessors": [
		{{ "bufferView": 0, "componentType": 5126, "count": 3, "type": "VEC3", "min": [0, 0, 0], "max": [1, 1, 0] }},
		{{ "bufferView": 1, "componentType": 5126, "count": 3, "type": "VEC2" }},
		{{ "bufferView": 2, "componentType": 5123, "count": 3, "type": "SCALAR" }}
	],
	"bufferViews": [
		{{ "buffer": 0, "byteOffset": 0, "byteLength": 36, "target": 34962 }},
		{{ "buffer": 0, "byteOffset": 36, "byteLength": 24, "target": 34962 }},
		{{ "buffer": 0, "byteOffset": 60, "byteLength": 8, "target": 34963 }}
	],
	"buffers": [{{ "byteLength": 68, "uri": "{}" }}]
}})", imageUri, bufferUri);
	}
}

TEST_CASE("ModelImporter: imports an embedded glTF with baked node transforms")
{
	std::vector<u8> geometry;
	AppendFloats(geometry, { 0.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f, 0.0f, 1.0f, 0.0f });
	AppendFloats(geometry, { 0.0f, 0.0f, 1.0f, 0.0f, 0.0f, 1.0f });
	AppendU16(geometry, { 0, 1, 2 });
	AppendU16(geometry, { 0 }); // keep the buffer at the declared 68 bytes
	REQUIRE(geometry.size() == 68);

	// stbi_write_png_to_mem is only declared inside the implementation block, so the
	// fixture goes through a real file and is read back as bytes.
	const std::vector<u8> pngSource = { 255, 0, 0, 255, 0, 255, 0, 255, 0, 0, 255, 255, 255, 255, 255, 255 };
	const FileSystem::Path pngPath = TestAssetPath("ae_model_embedded.png");
	REQUIRE(stbi_write_png(FileSystem::ToUtf8(pngPath).c_str(), 2, 2, 4, pngSource.data(), 2 * 4) != 0);
	std::vector<u8> png;
	REQUIRE(FileSystem::TryReadBinaryFile(pngPath, png));
	CHECK(FileSystem::RemoveFile(pngPath));

	const std::string document = MinimalGltf("data:image/png;base64," + EncodeBase64(png),
		"data:application/octet-stream;base64," + EncodeBase64(geometry));

	const FileSystem::Path path = TestAssetPath("ae_model_embedded.gltf");
	REQUIRE(FileSystem::WriteTextFile(path, document));

	const std::optional<ImportedModel> model = ModelImporter::Import(path);
	REQUIRE(model.has_value());

	CHECK(model->Name == "ae_model_embedded");
	REQUIRE(model->Geometry.IsValid());
	CHECK(model->Geometry.GetVertices().size() == 3);
	CHECK(model->Geometry.GetIndices().size() == 3);
	CHECK(model->SkippedPrimitiveCount == 0);

	REQUIRE(model->Geometry.GetSubmeshes().size() == 1);
	CHECK(model->Geometry.GetSubmeshes()[0].IndexCount == 3);
	CHECK(model->Geometry.GetSubmeshes()[0].VertexCount == 3);
	CHECK(model->Geometry.GetSubmeshes()[0].MaterialIndex == 0);

	// The node translation is baked into the vertex positions.
	CHECK(model->Geometry.GetBoundsMin() == glm::vec3(1.0f, 2.0f, 3.0f));
	CHECK(model->Geometry.GetBoundsMax() == glm::vec3(2.0f, 3.0f, 3.0f));

	// No NORMAL or TANGENT attribute: both are generated.
	for (const Vertex& vertex : model->Geometry.GetVertices())
	{
		CHECK(vertex.Normal.z == doctest::Approx(1.0f).epsilon(0.001));
		CHECK(vertex.Tangent.x == doctest::Approx(1.0f).epsilon(0.001));
		CHECK(vertex.Tangent.w == doctest::Approx(1.0f));
	}

	REQUIRE(model->Materials.size() == 1);
	const ImportedMaterial& material = model->Materials[0];
	CHECK(material.Name == "Imported");
	CHECK(material.Factors.BaseColorFactor == glm::vec4(0.5f, 0.25f, 0.125f, 1.0f));
	CHECK(material.Factors.MetallicFactor == doctest::Approx(0.75f));
	CHECK(material.Factors.RoughnessFactor == doctest::Approx(0.25f));
	CHECK(material.Factors.NormalScale == doctest::Approx(0.5f));
	CHECK(material.Factors.EmissiveFactor == glm::vec3(1.0f, 0.0f, 0.0f));
	CHECK(material.AlbedoTexture == 0);
	CHECK(material.NormalTexture == 1);
	CHECK(material.MetallicRoughnessTexture == -1);
	CHECK(material.OcclusionTexture == -1);

	// The same image is used as an sRGB colour map and as linear data, so it is
	// decoded twice - once per colour space.
	REQUIRE(model->Textures.size() == 2);
	CHECK(model->Textures[0].ColorSpace == TextureColorSpace::Srgb);
	CHECK(model->Textures[0].Source.GetWidth() == 2);
	CHECK(model->Textures[0].Source.GetHeight() == 2);
	// srgb 255,0,0 -> linear 1,0,0
	CHECK(model->Textures[0].Source.GetMipPixels(0)[0] == doctest::Approx(1.0f).epsilon(0.01));
	CHECK(model->Textures[0].Sampler.AddressU == TextureWrapMode::Repeat);
	CHECK(model->Textures[0].Sampler.AddressV == TextureWrapMode::Clamp);
	CHECK(model->Textures[0].Sampler.MagFilter == TextureFilter::Linear);
	CHECK(model->Textures[0].Sampler.MipFilter == TextureFilter::Linear);
	CHECK(model->Textures[1].ColorSpace == TextureColorSpace::Linear);

	CHECK(FileSystem::RemoveFile(path));
}

TEST_CASE("ModelImporter: external geometry buffers resolve next to the model")
{
	const FileSystem::Path directory = TestAssetPath("ae_model_external");
	REQUIRE(FileSystem::CreateDirectories(directory));

	std::vector<u8> geometry;
	AppendFloats(geometry, { 0.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f, 0.0f, 1.0f, 0.0f });
	AppendFloats(geometry, { 0.0f, 0.0f, 1.0f, 0.0f, 0.0f, 1.0f });
	AppendU16(geometry, { 0, 1, 2 });
	AppendU16(geometry, { 0 });
	REQUIRE(FileSystem::WriteBinaryFile(directory / "geometry.bin", geometry.data(), geometry.size()));

	const std::string document = MinimalGltf("", "geometry.bin");
	const FileSystem::Path path = directory / "model.gltf";
	REQUIRE(FileSystem::WriteTextFile(path, document));

	const std::optional<ImportedModel> model = ModelImporter::Import(path);
	REQUIRE(model.has_value());
	CHECK(model->Geometry.GetVertices().size() == 3);
	CHECK(model->Geometry.GetIndices().size() == 3);
	// No image could be loaded, so the material simply has no maps.
	CHECK(model->Textures.empty());
	REQUIRE(model->Materials.size() == 1);
	CHECK(model->Materials[0].AlbedoTexture == -1);

	CHECK(FileSystem::RemoveFile(path));
	CHECK(FileSystem::RemoveFile(directory / "geometry.bin"));
	CHECK(FileSystem::DeleteDirectory(directory));
}

TEST_CASE("ModelImporter: rejects missing and malformed files")
{
	const std::optional<ImportedModel> missing = ModelImporter::Import(TestAssetPath("ae_model_missing.gltf"));
	CHECK_FALSE(missing.has_value());

	const FileSystem::Path path = TestAssetPath("ae_model_broken.gltf");
	REQUIRE(FileSystem::WriteTextFile(path, "{ this is not json"));
	const std::optional<ImportedModel> broken = ModelImporter::Import(path);
	CHECK_FALSE(broken.has_value());
	CHECK(FileSystem::RemoveFile(path));

	// Valid JSON that carries no geometry must also be rejected.
	const FileSystem::Path emptyPath = TestAssetPath("ae_model_empty.gltf");
	REQUIRE(FileSystem::WriteTextFile(emptyPath, R"({ "asset": { "version": "2.0" } })"));
	const std::optional<ImportedModel> empty = ModelImporter::Import(emptyPath);
	CHECK_FALSE(empty.has_value());
	CHECK(FileSystem::RemoveFile(emptyPath));
}

TEST_CASE("ModelImporter: tolerates a UTF-8 BOM in a glTF document")
{
	std::vector<u8> geometry;
	AppendFloats(geometry, { 0.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f, 0.0f, 1.0f, 0.0f });
	AppendFloats(geometry, { 0.0f, 0.0f, 1.0f, 0.0f, 0.0f, 1.0f });
	AppendU16(geometry, { 0, 1, 2 });
	AppendU16(geometry, { 0 });
	REQUIRE(geometry.size() == 68);

	// The fixture declares a base-color and a normal texture, so the image must be a
	// real PNG data URI even though the BOM is what this test exercises.
	const std::vector<u8> pngSource = { 255, 0, 0, 255, 0, 255, 0, 255, 0, 0, 255, 255, 255, 255, 255, 255 };
	const FileSystem::Path pngPath = TestAssetPath("ae_model_bom.png");
	REQUIRE(stbi_write_png(FileSystem::ToUtf8(pngPath).c_str(), 2, 2, 4, pngSource.data(), 2 * 4) != 0);
	std::vector<u8> png;
	REQUIRE(FileSystem::TryReadBinaryFile(pngPath, png));
	CHECK(FileSystem::RemoveFile(pngPath));

	const std::string document = MinimalGltf("data:image/png;base64," + EncodeBase64(png),
		"data:application/octet-stream;base64," + EncodeBase64(geometry));
	std::string withBom = "\xEF\xBB\xBF";
	withBom += document;
	const FileSystem::Path path = TestAssetPath("ae_model_bom.gltf");
	REQUIRE(FileSystem::WriteTextFile(path, withBom));

	const std::optional<ImportedModel> model = ModelImporter::Import(path);
	REQUIRE(model.has_value());
	CHECK(model->Geometry.IsValid());
	CHECK(model->Geometry.GetSubmeshes().size() == 1);
	CHECK(FileSystem::RemoveFile(path));
}
