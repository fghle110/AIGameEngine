#include "Aegis/Assets/ModelImporter.h"

#include "Aegis/Core/Log.h"

#include <cgltf.h>

#include <glm/gtc/matrix_inverse.hpp>
#include <glm/gtc/type_ptr.hpp>

#include <algorithm>
#include <cstdlib>
#include <cstring>
#include <map>
#include <tuple>

namespace aegis
{
	namespace
	{
		// cgltf allocates its own structures; the callbacks below route the file
		// reads through FileSystem so UTF-8 and non-ASCII paths work on Windows.
		void* ImportAlloc(void* user, cgltf_size size)
		{
			(void)user;
			return std::malloc(size);
		}

		void ImportFree(void* user, void* pointer)
		{
			(void)user;
			std::free(pointer);
		}

		cgltf_result ReadFileCallback(const cgltf_memory_options* memoryOptions, const cgltf_file_options* fileOptions,
			const char* path, cgltf_size* size, void** data)
		{
			(void)fileOptions;
			std::vector<u8> bytes;
			if (!FileSystem::TryReadBinaryFile(FileSystem::FromUtf8(path), bytes))
			{
				return cgltf_result_file_not_found;
			}

			void* buffer = memoryOptions->alloc_func(memoryOptions->user_data, bytes.size());
			if (buffer == nullptr)
			{
				return cgltf_result_out_of_memory;
			}
			std::memcpy(buffer, bytes.data(), bytes.size());
			if (size != nullptr)
			{
				*size = bytes.size();
			}
			if (data != nullptr)
			{
				*data = buffer;
			}
			return cgltf_result_success;
		}

		void ReleaseFileCallback(const cgltf_memory_options* memoryOptions, const cgltf_file_options* fileOptions,
			void* data)
		{
			(void)fileOptions;
			memoryOptions->free_func(memoryOptions->user_data, data);
		}

		// Owning wrapper so every early return still frees the parsed document.
		struct CgltfFile
		{
			cgltf_data* Data = nullptr;

			~CgltfFile()
			{
				if (Data != nullptr)
				{
					cgltf_free(Data);
				}
			}
		};

		std::optional<std::vector<u8>> DecodeBase64(const char* text)
		{
			std::vector<u8> output;
			output.reserve(std::strlen(text) * 3 / 4);

			u32 accumulator = 0;
			u32 bits = 0;
			for (const char* cursor = text; *cursor != '\0'; ++cursor)
			{
				const char character = *cursor;
				u32 value = 0;
				if (character >= 'A' && character <= 'Z')
				{
					value = static_cast<u32>(character - 'A');
				}
				else if (character >= 'a' && character <= 'z')
				{
					value = static_cast<u32>(character - 'a') + 26;
				}
				else if (character >= '0' && character <= '9')
				{
					value = static_cast<u32>(character - '0') + 52;
				}
				else if (character == '+')
				{
					value = 62;
				}
				else if (character == '/')
				{
					value = 63;
				}
				else if (character == '=')
				{
					break;
				}
				else if (character == '\n' || character == '\r' || character == ' ' || character == '\t')
				{
					continue;
				}
				else
				{
					return std::nullopt;
				}

				accumulator = (accumulator << 6) | value;
				bits += 6;
				if (bits >= 8)
				{
					bits -= 8;
					output.push_back(static_cast<u8>((accumulator >> bits) & 0xFFu));
				}
			}

			if (output.empty())
			{
				return std::nullopt;
			}
			return output;
		}

		std::optional<std::vector<u8>> DecodeDataUri(const char* uri)
		{
			const char* comma = std::strchr(uri, ',');
			if (comma == nullptr)
			{
				return std::nullopt;
			}
			return DecodeBase64(comma + 1);
		}

		SamplerSettings ToSamplerSettings(const cgltf_sampler* sampler, TextureColorSpace colorSpace)
		{
			SamplerSettings settings = colorSpace == TextureColorSpace::Linear ? SamplerSettings::NormalMap()
																			  : SamplerSettings::Albedo();
			if (sampler == nullptr)
			{
				return settings;
			}

			switch (sampler->mag_filter)
			{
				case cgltf_filter_type_nearest: settings.MagFilter = TextureFilter::Nearest; break;
				case cgltf_filter_type_linear: settings.MagFilter = TextureFilter::Linear; break;
				default: break;
			}
			switch (sampler->min_filter)
			{
				case cgltf_filter_type_nearest:
				case cgltf_filter_type_nearest_mipmap_linear:
					settings.MinFilter = TextureFilter::Nearest;
					break;
				case cgltf_filter_type_linear:
				case cgltf_filter_type_linear_mipmap_nearest:
				case cgltf_filter_type_linear_mipmap_linear:
					settings.MinFilter = TextureFilter::Linear;
					break;
				default: break;
			}
			switch (sampler->min_filter)
			{
				case cgltf_filter_type_nearest_mipmap_nearest:
				case cgltf_filter_type_linear_mipmap_nearest:
					settings.MipFilter = TextureFilter::Nearest;
					break;
				case cgltf_filter_type_nearest_mipmap_linear:
				case cgltf_filter_type_linear_mipmap_linear:
					settings.MipFilter = TextureFilter::Linear;
					break;
				default: break;
			}

			const auto toWrap = [](cgltf_wrap_mode mode, TextureWrapMode fallback)
			{
				switch (mode)
				{
					case cgltf_wrap_mode_clamp_to_edge: return TextureWrapMode::Clamp;
					case cgltf_wrap_mode_mirrored_repeat: return TextureWrapMode::Mirror;
					case cgltf_wrap_mode_repeat: return TextureWrapMode::Repeat;
					default: return fallback;
				}
			};
			settings.AddressU = toWrap(sampler->wrap_s, settings.AddressU);
			settings.AddressV = toWrap(sampler->wrap_t, settings.AddressV);
			return settings;
		}

		// Importer state. Kept as one object so the recursive node walk and the
		// per-primitive material/texture de-duplication share a single cache.
		class Importer
		{
		public:
			Importer(const FileSystem::Path& path, std::string name, cgltf_data* data)
				: m_ModelDirectory(path.parent_path())
				, m_Name(std::move(name))
				, m_Data(data)
			{
			}

			std::optional<ImportedModel> Run()
			{
				m_Model.Name = m_Name;

				if (m_Data->skins_count > 0 || m_Data->animations_count > 0)
				{
					AE_CORE_INFO("'{}': skins and animations are not supported, ignoring {} skins and {} animations",
						m_Name, m_Data->skins_count, m_Data->animations_count);
				}

				const glm::mat4 identity(1.0f);
				if (m_Data->scene != nullptr)
				{
					for (cgltf_size i = 0; i < m_Data->scene->nodes_count; ++i)
					{
						visitNode(m_Data->scene->nodes[i], identity);
					}
				}
				else
				{
					for (cgltf_size i = 0; i < m_Data->nodes_count; ++i)
					{
						if (m_Data->nodes[i].parent == nullptr)
						{
							visitNode(&m_Data->nodes[i], identity);
						}
					}
				}

				if (!m_Model.Geometry.IsValid())
				{
					AE_CORE_ERROR("'{}' contains no triangle geometry that could be imported", m_Name);
					return std::nullopt;
				}

				m_Model.Geometry.SetName(m_Name);
				m_Model.Geometry.RecalculateBounds();
				AE_CORE_INFO("Imported '{}': {} vertices, {} triangles, {} submeshes, {} materials, {} textures",
					m_Name, m_Model.Geometry.GetVertices().size(), m_Model.Geometry.GetTriangleCount(),
					m_Model.Geometry.GetSubmeshes().size(), m_Model.Materials.size(), m_Model.Textures.size());
				return std::move(m_Model);
			}

		private:
			void visitNode(const cgltf_node* node, const glm::mat4& parentWorld)
			{
				if (node == nullptr)
				{
					return;
				}

				cgltf_float localValues[16] = {};
				cgltf_node_transform_local(node, localValues);
				const glm::mat4 world = parentWorld * glm::make_mat4(localValues);

				if (node->mesh != nullptr)
				{
					appendMesh(node->mesh, world);
				}
				for (cgltf_size i = 0; i < node->children_count; ++i)
				{
					visitNode(node->children[i], world);
				}
			}

			void appendMesh(const cgltf_mesh* mesh, const glm::mat4& world)
			{
				const glm::mat3 worldBasis(world);
				const glm::mat3 normalMatrix = glm::transpose(glm::inverse(worldBasis));
				// A mirrored node transform reverses the winding; flip the triangles so
				// the baked geometry keeps facing outwards.
				const bool isMirrored = glm::determinant(worldBasis) < 0.0f;

				for (cgltf_size primitiveIndex = 0; primitiveIndex < mesh->primitives_count; ++primitiveIndex)
				{
					const cgltf_primitive& primitive = mesh->primitives[primitiveIndex];
					if (primitive.type != cgltf_primitive_type_triangles)
					{
						++m_Model.SkippedPrimitiveCount;
						AE_CORE_WARN("'{}': skipping a primitive whose mode is not TRIANGLES (mode {})", m_Name,
							static_cast<int>(primitive.type));
						continue;
					}

					const cgltf_accessor* positions = cgltf_find_accessor(&primitive, cgltf_attribute_type_position, 0);
					if (positions == nullptr || positions->count == 0)
					{
						AE_CORE_WARN("'{}': skipping a primitive without positions", m_Name);
						continue;
					}

					const cgltf_size vertexCount = positions->count;
					std::vector<Vertex> vertices(vertexCount);
					for (cgltf_size i = 0; i < vertexCount; ++i)
					{
						cgltf_float value[3] = { 0.0f, 0.0f, 0.0f };
						cgltf_accessor_read_float(positions, i, value, 3);
						vertices[i].Position = glm::vec3(world * glm::vec4(value[0], value[1], value[2], 1.0f));
					}

					const cgltf_accessor* normals = cgltf_find_accessor(&primitive, cgltf_attribute_type_normal, 0);
					bool hasNormals = normals != nullptr && normals->count == vertexCount;
					if (hasNormals)
					{
						for (cgltf_size i = 0; i < vertexCount; ++i)
						{
							cgltf_float value[3] = { 0.0f, 0.0f, 0.0f };
							if (!cgltf_accessor_read_float(normals, i, value, 3))
							{
								hasNormals = false;
								break;
							}
							const glm::vec3 transformed = normalMatrix * glm::vec3(value[0], value[1], value[2]);
							vertices[i].Normal = glm::length(transformed) > 1.0e-8f ? glm::normalize(transformed)
																					: glm::vec3(0.0f, 1.0f, 0.0f);
						}
					}

					const cgltf_accessor* tangents = cgltf_find_accessor(&primitive, cgltf_attribute_type_tangent, 0);
					bool hasTangents = tangents != nullptr && tangents->count == vertexCount;
					if (hasTangents)
					{
						for (cgltf_size i = 0; i < vertexCount; ++i)
						{
							cgltf_float value[4] = { 1.0f, 0.0f, 0.0f, 1.0f };
							if (!cgltf_accessor_read_float(tangents, i, value, 4))
							{
								hasTangents = false;
								break;
							}
							const glm::vec3 direction = worldBasis * glm::vec3(value[0], value[1], value[2]);
							vertices[i].Tangent = glm::vec4(glm::length(direction) > 1.0e-8f ? glm::normalize(direction)
																						  : glm::vec3(1.0f, 0.0f, 0.0f),
								value[3] < 0.0f ? -1.0f : 1.0f);
						}
					}

					const cgltf_accessor* uvs = cgltf_find_accessor(&primitive, cgltf_attribute_type_texcoord, 0);
					if (uvs != nullptr && uvs->count == vertexCount)
					{
						for (cgltf_size i = 0; i < vertexCount; ++i)
						{
							cgltf_float value[2] = { 0.0f, 0.0f };
							cgltf_accessor_read_float(uvs, i, value, 2);
							vertices[i].Uv = glm::vec2(value[0], value[1]);
						}
					}

					std::vector<u32> indices;
					if (primitive.indices != nullptr)
					{
						indices.resize(static_cast<usize>(primitive.indices->count));
						for (usize i = 0; i < indices.size(); ++i)
						{
							indices[i] = static_cast<u32>(cgltf_accessor_read_index(primitive.indices, i));
						}
					}
					else
					{
						indices.resize(static_cast<usize>(vertexCount));
						for (usize i = 0; i < indices.size(); ++i)
						{
							indices[i] = static_cast<u32>(i);
						}
					}
					indices.resize(indices.size() - (indices.size() % 3));

					if (indices.empty())
					{
						AE_CORE_WARN("'{}': skipping a primitive without triangle indices", m_Name);
						continue;
					}
					for (const u32 index : indices)
					{
						if (index >= vertexCount)
						{
							AE_CORE_ERROR("'{}': primitive index {} exceeds the vertex count {}", m_Name, index,
								vertexCount);
							return;
						}
					}

					if (!hasNormals)
					{
						GenerateNormals(vertices, indices);
					}
					if (!hasTangents)
					{
						GenerateTangents(vertices, indices);
					}
					if (isMirrored)
					{
						for (usize i = 0; i + 2 < indices.size(); i += 3)
						{
							std::swap(indices[i + 1], indices[i + 2]);
						}
					}

					const u32 firstVertex = static_cast<u32>(m_Model.Geometry.GetVertices().size());
					const u32 firstIndex = static_cast<u32>(m_Model.Geometry.GetIndices().size());
					{
						std::vector<Vertex>& destination = m_Model.Geometry.GetVertices();
						destination.insert(destination.end(), vertices.begin(), vertices.end());
					}
					{
						std::vector<u32>& destination = m_Model.Geometry.GetIndices();
						destination.insert(destination.end(), indices.begin(), indices.end());
						for (usize i = firstIndex; i < destination.size(); ++i)
						{
							destination[i] += firstVertex;
						}
					}

					m_Model.Materials.push_back(importMaterial(primitive.material));

					Submesh submesh;
					submesh.FirstVertex = firstVertex;
					submesh.VertexCount = static_cast<u32>(vertices.size());
					submesh.FirstIndex = firstIndex;
					submesh.IndexCount = static_cast<u32>(indices.size());
					submesh.MaterialIndex = static_cast<u32>(m_Model.Materials.size() - 1);
					m_Model.Geometry.AddSubmesh(submesh);
				}
			}

			ImportedMaterial importMaterial(const cgltf_material* material)
			{
				ImportedMaterial imported;
				imported.Factors = MaterialFactors::GltfDefault();
				imported.Factors.EmissiveFactor = glm::vec3(0.0f);

				if (material == nullptr)
				{
					// glTF defines a default material for primitives without one.
					imported.Name = "Default";
					return imported;
				}

				imported.Name = material->name != nullptr ? material->name : "Material";
				if (material->has_pbr_metallic_roughness)
				{
					const cgltf_pbr_metallic_roughness& pbr = material->pbr_metallic_roughness;
					imported.Factors.BaseColorFactor = glm::vec4(pbr.base_color_factor[0], pbr.base_color_factor[1],
						pbr.base_color_factor[2], pbr.base_color_factor[3]);
					imported.Factors.MetallicFactor = pbr.metallic_factor;
					imported.Factors.RoughnessFactor = pbr.roughness_factor;
					imported.AlbedoTexture = loadTexture(pbr.base_color_texture.texture, TextureColorSpace::Srgb,
						pbr.base_color_texture.texcoord);
					imported.MetallicRoughnessTexture = loadTexture(pbr.metallic_roughness_texture.texture,
						TextureColorSpace::Linear, pbr.metallic_roughness_texture.texcoord);
				}

				if (material->normal_texture.texture != nullptr)
				{
					imported.NormalTexture = loadTexture(material->normal_texture.texture, TextureColorSpace::Linear,
						material->normal_texture.texcoord);
					imported.Factors.NormalScale = material->normal_texture.scale;
				}
				if (material->occlusion_texture.texture != nullptr)
				{
					imported.OcclusionTexture = loadTexture(material->occlusion_texture.texture,
						TextureColorSpace::Linear, material->occlusion_texture.texcoord);
					imported.Factors.OcclusionStrength = material->occlusion_texture.scale;
				}
				if (material->emissive_texture.texture != nullptr)
				{
					imported.EmissiveTexture = loadTexture(material->emissive_texture.texture, TextureColorSpace::Srgb,
						material->emissive_texture.texcoord);
				}

				imported.Factors.EmissiveFactor = glm::vec3(material->emissive_factor[0],
					material->emissive_factor[1], material->emissive_factor[2]);
				if (material->has_emissive_strength)
				{
					imported.Factors.EmissiveStrength = material->emissive_strength.emissive_strength;
				}
				return imported;
			}

			i32 loadTexture(const cgltf_texture* texture, TextureColorSpace colorSpace, cgltf_int texcoord)
			{
				if (texture == nullptr || texture->image == nullptr)
				{
					return -1;
				}
				if (texcoord != 0)
				{
					AE_CORE_WARN("'{}': a texture uses TEXCOORD_{}; only TEXCOORD_0 is supported", m_Name,
						static_cast<int>(texcoord));
				}

				const cgltf_size imageIndex = cgltf_image_index(m_Data, texture->image);
				const cgltf_size samplerIndex = texture->sampler != nullptr
													? cgltf_sampler_index(m_Data, texture->sampler)
													: static_cast<cgltf_size>(-1);
				const auto key = std::make_tuple(imageIndex, samplerIndex,
					static_cast<std::underlying_type_t<TextureColorSpace>>(colorSpace));
				const auto existing = m_TextureIndices.find(key);
				if (existing != m_TextureIndices.end())
				{
					return existing->second;
				}

				std::optional<Image> image = decodeImage(texture->image, colorSpace);
				if (!image.has_value())
				{
					return -1;
				}

				ImportedTexture imported;
				imported.Source = std::move(*image);
				imported.Sampler = ToSamplerSettings(texture->sampler, colorSpace);
				imported.ColorSpace = colorSpace;
				imported.Name = imported.Source.GetName();

				m_Model.Textures.push_back(std::move(imported));
				const i32 index = static_cast<i32>(m_Model.Textures.size() - 1);
				m_TextureIndices.emplace(key, index);
				return index;
			}

			std::optional<Image> decodeImage(const cgltf_image* image, TextureColorSpace colorSpace)
			{
				if (image->buffer_view != nullptr)
				{
					const u8* bytes = cgltf_buffer_view_data(image->buffer_view);
					if (bytes == nullptr)
					{
						AE_CORE_ERROR("'{}': an embedded image buffer could not be resolved", m_Name);
						return std::nullopt;
					}
					return Image::DecodeFromMemory(bytes, image->buffer_view->size, colorSpace, "embedded image");
				}

				if (image->uri == nullptr)
				{
					AE_CORE_WARN("'{}': an image has neither a URI nor a buffer view", m_Name);
					return std::nullopt;
				}

				if (std::strncmp(image->uri, "data:", 5) == 0)
				{
					const std::optional<std::vector<u8>> decoded = DecodeDataUri(image->uri);
					if (!decoded.has_value())
					{
						AE_CORE_ERROR("'{}': a data URI image could not be decoded", m_Name);
						return std::nullopt;
					}
					return Image::DecodeFromMemory(decoded->data(), decoded->size(), colorSpace, "embedded image");
				}

				std::string uri = image->uri;
				uri.resize(cgltf_decode_uri(uri.data()));
				const FileSystem::Path path = m_ModelDirectory / FileSystem::FromUtf8(uri);
				std::optional<Image> loaded = Image::LoadFromFile(path, colorSpace);
				if (!loaded.has_value())
				{
					AE_CORE_WARN("'{}': the external image '{}' could not be loaded", m_Name, uri);
				}
				return loaded;
			}

			FileSystem::Path m_ModelDirectory;
			std::string m_Name;
			cgltf_data* m_Data = nullptr;
			ImportedModel m_Model;
			std::map<std::tuple<cgltf_size, cgltf_size, u8>, i32> m_TextureIndices;
		};
	}

	std::optional<ImportedModel> ModelImporter::Import(const FileSystem::Path& path)
	{
		std::vector<u8> fileBytes;
		if (!FileSystem::TryReadBinaryFile(path, fileBytes) || fileBytes.empty())
		{
			AE_CORE_ERROR("Could not read the model file '{}'", FileSystem::ToUtf8(path));
			return std::nullopt;
		}

		cgltf_options options{};
		options.memory.alloc_func = &ImportAlloc;
		options.memory.free_func = &ImportFree;
		options.file.read = &ReadFileCallback;
		options.file.release = &ReleaseFileCallback;

		CgltfFile file;
		const cgltf_result parseResult = cgltf_parse(&options, fileBytes.data(), fileBytes.size(), &file.Data);
		if (parseResult != cgltf_result_success || file.Data == nullptr)
		{
			AE_CORE_ERROR("'{}' is not a valid glTF/GLB file (cgltf result {})", FileSystem::ToUtf8(path),
				static_cast<int>(parseResult));
			return std::nullopt;
		}

		const std::string utf8Path = FileSystem::ToUtf8(path);
		const cgltf_result bufferResult = cgltf_load_buffers(&options, file.Data, utf8Path.c_str());
		if (bufferResult != cgltf_result_success)
		{
			AE_CORE_ERROR("'{}': the geometry buffers could not be loaded (cgltf result {})", utf8Path,
				static_cast<int>(bufferResult));
			return std::nullopt;
		}

		Importer importer(path, FileSystem::GetStem(path), file.Data);
		return importer.Run();
	}
}
