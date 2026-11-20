#include "Aegis/Renderer/AssetCache.h"

#include "Aegis/Core/Log.h"

#include <algorithm>

namespace aegis
{
	namespace
	{
		constexpr PrimitiveType c_BuiltinPrimitives[4] = { PrimitiveType::Cube, PrimitiveType::Plane,
			PrimitiveType::Sphere, PrimitiveType::Cylinder };

		// Fixed ids so a scene authored today still finds the built-in primitives after
		// a restart. The last group marks them as engine assets.
		const char* const c_BuiltinPrimitiveIds[4] = {
			"00000000-0000-4000-8000-000000000001",
			"00000000-0000-4000-8000-000000000002",
			"00000000-0000-4000-8000-000000000003",
			"00000000-0000-4000-8000-000000000004",
		};
	}

	Uuid DeriveAssetUuid(const Uuid& parent, u32 index)
	{
		std::array<u8, 16> bytes = parent.GetBytes();
		u64 hash = 1469598103934665603ull;
		for (const u8 byte : bytes)
		{
			hash ^= byte;
			hash *= 1099511628211ull;
		}
		hash ^= static_cast<u64>(index) * 0x9E3779B97F4A7C15ull;
		hash *= 1099511628211ull;

		for (u32 i = 0; i < 8; ++i)
		{
			bytes[8 + i] = static_cast<u8>((hash >> (i * 8)) & 0xFFull);
		}
		// Keep the RFC 4122 variant bits so tooling recognises the id shape.
		bytes[8] = static_cast<u8>((bytes[8] & 0x3F) | 0x80);
		return Uuid(bytes);
	}

	AssetCache::~AssetCache()
	{
		Shutdown();
	}

	bool AssetCache::Initialize(nvrhi::IDevice* device, nvrhi::IBindingLayout* materialLayout)
	{
		if (device == nullptr || materialLayout == nullptr)
		{
			AE_CORE_ERROR("AssetCache::Initialize needs a device and the material binding layout");
			return false;
		}

		m_Device = device;
		m_MaterialLayout = materialLayout;
		m_WhiteTexture = Texture::CreateWhite(device);
		m_FlatNormalTexture = Texture::CreateFlatNormal(device);
		if (m_WhiteTexture == nullptr || m_FlatNormalTexture == nullptr)
		{
			AE_CORE_ERROR("Could not create the material fallback textures");
			Shutdown();
			return false;
		}
		return true;
	}

	void AssetCache::Shutdown()
	{
		m_Meshes.clear();
		m_Materials.clear();
		m_Textures.clear();
		m_WhiteTexture = nullptr;
		m_FlatNormalTexture = nullptr;
		m_MaterialLayout = nullptr;
		m_Device = nullptr;
		for (AssetHandle& handle : m_BuiltinMeshes)
		{
			handle = AssetHandle{};
		}
	}

	Ref<Texture> AssetCache::resolveTexture(const Ref<Texture>& texture) const
	{
		return texture != nullptr ? texture : m_WhiteTexture;
	}

	Ref<Texture> AssetCache::resolveNormalTexture(const Ref<Texture>& texture) const
	{
		return texture != nullptr ? texture : m_FlatNormalTexture;
	}

	bool AssetCache::uploadMeshBuffers(CachedMesh& mesh)
	{
		const std::vector<Vertex>& vertices = mesh.Geometry.GetVertices();
		const std::vector<u32>& indices = mesh.Geometry.GetIndices();

		nvrhi::BufferDesc vertexDesc;
		vertexDesc.byteSize = vertices.size() * sizeof(Vertex);
		vertexDesc.isVertexBuffer = true;
		vertexDesc.initialState = nvrhi::ResourceStates::VertexBuffer;
		vertexDesc.keepInitialState = true;
		vertexDesc.debugName = std::format("{} vertices", mesh.Name);
		mesh.VertexBuffer = m_Device->createBuffer(vertexDesc);

		nvrhi::BufferDesc indexDesc;
		indexDesc.byteSize = indices.size() * sizeof(u32);
		indexDesc.isIndexBuffer = true;
		indexDesc.initialState = nvrhi::ResourceStates::IndexBuffer;
		indexDesc.keepInitialState = true;
		indexDesc.debugName = std::format("{} indices", mesh.Name);
		mesh.IndexBuffer = m_Device->createBuffer(indexDesc);

		if (mesh.VertexBuffer == nullptr || mesh.IndexBuffer == nullptr)
		{
			AE_CORE_ERROR("Could not allocate the geometry buffers of '{}'", mesh.Name);
			return false;
		}

		nvrhi::CommandListHandle commandList = m_Device->createCommandList();
		commandList->open();
		commandList->writeBuffer(mesh.VertexBuffer, vertices.data(), vertexDesc.byteSize);
		commandList->writeBuffer(mesh.IndexBuffer, indices.data(), indexDesc.byteSize);
		commandList->close();
		nvrhi::ICommandList* rawList = commandList;
		m_Device->executeCommandLists(&rawList, 1);
		m_Device->waitForIdle();
		return true;
	}

	Uuid AssetCache::AddTexture(const Uuid& textureId, const Image& image, const SamplerSettings& sampler)
	{
		if (!IsInitialized())
		{
			AE_CORE_ERROR("AssetCache::AddTexture called before Initialize");
			return Uuid::Null();
		}

		const auto existing = m_Textures.find(textureId);
		if (existing != m_Textures.end())
		{
			return textureId;
		}

		Ref<Texture> texture = Texture::Create(m_Device, image, sampler, image.GetName());
		if (texture == nullptr)
		{
			return Uuid::Null();
		}
		m_Textures.emplace(textureId, std::move(texture));
		return textureId;
	}

	Uuid AssetCache::AddMaterial(const Uuid& materialId, const MaterialFactors& factors,
		const MaterialTextures& textures, const std::string& name)
	{
		if (!IsInitialized())
		{
			AE_CORE_ERROR("AssetCache::AddMaterial called before Initialize");
			return Uuid::Null();
		}

		const auto existing = m_Materials.find(materialId);
		if (existing != m_Materials.end())
		{
			return materialId;
		}

		MaterialTextures resolved;
		resolved.Albedo = resolveTexture(textures.Albedo);
		// A missing normal map must stay "flat": the white fallback decodes to a tilted
		// normal, so the normal slot has its own fallback.
		resolved.Normal = resolveNormalTexture(textures.Normal);
		resolved.MetallicRoughness = resolveTexture(textures.MetallicRoughness);
		resolved.Occlusion = resolveTexture(textures.Occlusion);
		resolved.Emissive = resolveTexture(textures.Emissive);

		Ref<Material> material = Material::Create(m_Device, m_MaterialLayout, factors, resolved,
			name.empty() ? materialId.ToString() : name);
		if (material == nullptr)
		{
			return Uuid::Null();
		}
		m_Materials.emplace(materialId, std::move(material));
		return materialId;
	}

	bool AssetCache::AddMesh(const Uuid& meshId, Mesh mesh, const std::vector<MaterialFactors>& materials)
	{
		if (!IsInitialized())
		{
			AE_CORE_ERROR("AssetCache::AddMesh called before Initialize");
			return false;
		}
		if (meshId.IsNull() || !mesh.IsValid())
		{
			AE_CORE_ERROR("AssetCache::AddMesh needs a valid id and geometry");
			return false;
		}
		if (m_Meshes.find(meshId) != m_Meshes.end())
		{
			return true;
		}

		CachedMesh cached;
		cached.Geometry = std::move(mesh);
		cached.Name = cached.Geometry.GetName().empty() ? meshId.ToString() : cached.Geometry.GetName();

		const u32 submeshCount = static_cast<u32>(cached.Geometry.GetSubmeshes().size());
		for (u32 index = 0; index < submeshCount; ++index)
		{
			const Uuid materialId = DeriveAssetUuid(meshId, index);
			if (m_Materials.find(materialId) == m_Materials.end())
			{
				const MaterialFactors factors = index < materials.size() ? materials[index]
																	   : MaterialFactors::EngineDefault();
				if (AddMaterial(materialId, factors, MaterialTextures{}).IsNull())
				{
					return false;
				}
			}
			cached.MaterialIds.push_back(materialId);
		}

		if (!uploadMeshBuffers(cached))
		{
			return false;
		}

		AE_CORE_TRACE("Uploaded mesh '{}' ({} vertices, {} submeshes)", cached.Name,
			cached.Geometry.GetVertices().size(), submeshCount);
		m_Meshes.emplace(meshId, std::move(cached));
		return true;
	}

	bool AssetCache::AddModel(const Uuid& modelId, ImportedModel model)
	{
		if (!IsInitialized())
		{
			AE_CORE_ERROR("AssetCache::AddModel called before Initialize");
			return false;
		}
		if (modelId.IsNull() || !model.Geometry.IsValid())
		{
			AE_CORE_ERROR("AssetCache::AddModel needs a valid id and imported geometry");
			return false;
		}

		std::vector<Ref<Texture>> textures(model.Textures.size());
		for (usize i = 0; i < model.Textures.size(); ++i)
		{
			const ImportedTexture& imported = model.Textures[i];
			textures[i] = Texture::Create(m_Device, imported.Source, imported.Sampler, imported.Name);
			if (textures[i] == nullptr)
			{
				AE_CORE_WARN("Model '{}': texture '{}' could not be uploaded", model.Name, imported.Name);
			}
		}

		std::vector<MaterialFactors> factors;
		factors.reserve(model.Materials.size());
		for (usize i = 0; i < model.Materials.size(); ++i)
		{
			const ImportedMaterial& imported = model.Materials[i];
			MaterialTextures slots;
			const auto pick = [&textures](i32 index) -> Ref<Texture>
			{
				if (index < 0 || static_cast<usize>(index) >= textures.size())
				{
					return nullptr;
				}
				return textures[static_cast<usize>(index)];
			};
			slots.Albedo = pick(imported.AlbedoTexture);
			slots.Normal = pick(imported.NormalTexture);
			slots.MetallicRoughness = pick(imported.MetallicRoughnessTexture);
			slots.Occlusion = pick(imported.OcclusionTexture);
			slots.Emissive = pick(imported.EmissiveTexture);

			// The submesh of primitive i owns the material id AddMesh derives for i.
			const Uuid materialId = DeriveAssetUuid(modelId, static_cast<u32>(i));
			if (AddMaterial(materialId, imported.Factors, slots, imported.Name).IsNull())
			{
				return false;
			}
			factors.push_back(imported.Factors);
		}

		return AddMesh(modelId, std::move(model.Geometry), factors);
	}

	void AssetCache::RegisterBuiltinPrimitives()
	{
		if (!IsInitialized())
		{
			AE_CORE_ERROR("AssetCache::RegisterBuiltinPrimitives called before Initialize");
			return;
		}

		for (usize i = 0; i < 4; ++i)
		{
			const Uuid meshId = Uuid::FromString(c_BuiltinPrimitiveIds[i]);
			Mesh mesh = Mesh::CreatePrimitive(c_BuiltinPrimitives[i]);
			if (!AddMesh(meshId, std::move(mesh), {}))
			{
				AE_CORE_ERROR("Could not register the built-in '{}' primitive", PrimitiveTypeToString(c_BuiltinPrimitives[i]));
				continue;
			}
			m_BuiltinMeshes[i] = AssetHandle{ meshId, AssetType::Mesh };
		}
	}

	Uuid AssetCache::GetBuiltinMeshUuid(PrimitiveType type)
	{
		const usize index = static_cast<usize>(type);
		return index < 4 ? Uuid::FromString(c_BuiltinPrimitiveIds[index]) : Uuid::Null();
	}

	AssetHandle AssetCache::GetBuiltinMesh(PrimitiveType type) const
	{
		const usize index = static_cast<usize>(type);
		if (index >= 4)
		{
			return AssetHandle{};
		}
		return m_BuiltinMeshes[index];
	}

	const AssetCache::CachedMesh* AssetCache::GetMesh(const Uuid& meshId) const
	{
		const auto it = m_Meshes.find(meshId);
		return it != m_Meshes.end() ? &it->second : nullptr;
	}

	const Material* AssetCache::GetMaterial(const Uuid& materialId) const
	{
		const auto it = m_Materials.find(materialId);
		return it != m_Materials.end() ? it->second.get() : nullptr;
	}

	Ref<Texture> AssetCache::GetTexture(const Uuid& textureId) const
	{
		const auto it = m_Textures.find(textureId);
		return it != m_Textures.end() ? it->second : nullptr;
	}
}
