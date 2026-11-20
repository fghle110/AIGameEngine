#pragma once

#include "Aegis/Assets/AssetHandle.h"
#include "Aegis/Assets/Material.h"
#include "Aegis/Assets/Mesh.h"
#include "Aegis/Assets/ModelImporter.h"
#include "Aegis/Core/Uuid.h"

#include <nvrhi/nvrhi.h>

#include <string>
#include <unordered_map>
#include <vector>

namespace aegis
{
	// Derives a stable asset id from a parent id and an index, so the materials a
	// mesh owns keep their identity across sessions without extra metadata.
	Uuid DeriveAssetUuid(const Uuid& parent, u32 index);

	// GPU residency for the meshes, materials and textures a scene references.
	// Components only store handles, so this cache is the single place where an
	// asset id becomes an NVRHI resource; uploads happen when an asset is added.
	class AssetCache final
	{
	public:
		struct CachedMesh
		{
			Mesh Geometry;
			nvrhi::BufferHandle VertexBuffer;
			nvrhi::BufferHandle IndexBuffer;
			// One material per submesh, in submesh order.
			std::vector<Uuid> MaterialIds;
			std::string Name;
		};

		AssetCache() = default;
		~AssetCache();

		AssetCache(const AssetCache&) = delete;
		AssetCache& operator=(const AssetCache&) = delete;

		// The material binding layout belongs to the SceneRenderer and must outlive
		// the cache; materials use it to build their descriptor sets.
		bool Initialize(nvrhi::IDevice* device, nvrhi::IBindingLayout* materialLayout);
		void Shutdown();
		bool IsInitialized() const { return m_Device != nullptr && m_MaterialLayout != nullptr; }

		// Registers a mesh and one material per submesh. Materials are keyed by
		// DeriveAssetUuid(meshId, submeshIndex) and reused when they already exist.
		bool AddMesh(const Uuid& meshId, Mesh mesh, const std::vector<MaterialFactors>& materials = {});
		// Registers a full imported glTF model: its textures, its materials and its
		// geometry. Submesh materials keep the ids AddMesh derives.
		bool AddModel(const Uuid& modelId, ImportedModel model);

		Uuid AddTexture(const Uuid& textureId, const Image& image, const SamplerSettings& sampler);
		Uuid AddMaterial(const Uuid& materialId, const MaterialFactors& factors, const MaterialTextures& textures,
			const std::string& name = {});

		// Uploads the four built-in primitives with a neutral material each. Called by
		// SceneRenderer::Initialize so the editor and tests always have geometry.
		void RegisterBuiltinPrimitives();
		AssetHandle GetBuiltinMesh(PrimitiveType type) const;
		// Stable UUID of a built-in primitive, so a serialised scene can reference
		// "cube"/"plane"/"sphere"/"cylinder" without shipping mesh files.
		[[nodiscard]] static Uuid GetBuiltinMeshUuid(PrimitiveType type);

		const CachedMesh* GetMesh(const Uuid& meshId) const;
		const Material* GetMaterial(const Uuid& materialId) const;
		// Shared so a Material can hold on to the texture it is bound to.
		Ref<Texture> GetTexture(const Uuid& textureId) const;

		usize GetMeshCount() const { return m_Meshes.size(); }
		usize GetMaterialCount() const { return m_Materials.size(); }
		usize GetTextureCount() const { return m_Textures.size(); }

	private:
		bool uploadMeshBuffers(CachedMesh& mesh);
		Ref<Texture> resolveTexture(const Ref<Texture>& texture) const;
		Ref<Texture> resolveNormalTexture(const Ref<Texture>& texture) const;

		nvrhi::IDevice* m_Device = nullptr;
		nvrhi::BindingLayoutHandle m_MaterialLayout;
		Ref<Texture> m_WhiteTexture;
		Ref<Texture> m_FlatNormalTexture;
		std::unordered_map<Uuid, CachedMesh, UuidHash> m_Meshes;
		std::unordered_map<Uuid, Ref<Material>, UuidHash> m_Materials;
		std::unordered_map<Uuid, Ref<Texture>, UuidHash> m_Textures;
		AssetHandle m_BuiltinMeshes[4];
	};
}
