#pragma once

#include "Aegis/Assets/Texture.h"

#include <glm/glm.hpp>
#include <nvrhi/nvrhi.h>

#include <string>

namespace aegis
{
	// Physically based material factors. The parameterisation follows the glTF 2.0
	// metallic-roughness model so imported materials keep their authoring intent.
	struct MaterialFactors
	{
		glm::vec4 BaseColorFactor{ 0.8f, 0.8f, 0.8f, 1.0f };
		f32 MetallicFactor = 0.0f;
		f32 RoughnessFactor = 0.5f;
		f32 NormalScale = 1.0f;
		f32 OcclusionStrength = 1.0f;
		glm::vec3 EmissiveFactor{ 0.0f };
		f32 EmissiveStrength = 1.0f;

		// Neutral dielectric used for meshes that carry no authored material.
		static MaterialFactors EngineDefault();
		// The glTF 2.0 spec defaults: white base colour, fully metallic, fully rough.
		static MaterialFactors GltfDefault();
	};

	// The maps a material references. Slots left null are bound to the shared white
	// fallback by the asset cache, so the shader always has a valid descriptor.
	struct MaterialTextures
	{
		Ref<Texture> Albedo;
		Ref<Texture> Normal;
		Ref<Texture> MetallicRoughness;
		Ref<Texture> Occlusion;
		Ref<Texture> Emissive;

		bool HasAnyTexture() const;
	};

	// GPU material: the factors (passed as push constants) plus the binding set that
	// holds the five texture slots and their samplers.
	class Material
	{
	public:
		static Ref<Material> Create(nvrhi::IDevice* device, nvrhi::IBindingLayout* layout,
			const MaterialFactors& factors, const MaterialTextures& textures, const std::string& debugName = {});

		bool IsValid() const { return m_BindingSet != nullptr; }

		const MaterialFactors& GetFactors() const { return m_Factors; }
		void SetFactors(const MaterialFactors& factors) { m_Factors = factors; }
		const MaterialTextures& GetTextures() const { return m_Textures; }
		nvrhi::IBindingSet* GetBindingSet() const { return m_BindingSet.Get(); }
		const std::string& GetDebugName() const { return m_DebugName; }

	private:
		MaterialFactors m_Factors;
		MaterialTextures m_Textures;
		nvrhi::BindingSetHandle m_BindingSet;
		std::string m_DebugName;
	};
}
