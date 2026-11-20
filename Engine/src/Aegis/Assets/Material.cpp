#include "Aegis/Assets/Material.h"

#include "Aegis/Core/Log.h"

namespace aegis
{
	MaterialFactors MaterialFactors::EngineDefault()
	{
		MaterialFactors factors;
		factors.BaseColorFactor = glm::vec4(0.8f, 0.8f, 0.8f, 1.0f);
		factors.MetallicFactor = 0.0f;
		factors.RoughnessFactor = 0.5f;
		return factors;
	}

	MaterialFactors MaterialFactors::GltfDefault()
	{
		MaterialFactors factors;
		factors.BaseColorFactor = glm::vec4(1.0f, 1.0f, 1.0f, 1.0f);
		factors.MetallicFactor = 1.0f;
		factors.RoughnessFactor = 1.0f;
		return factors;
	}

	bool MaterialTextures::HasAnyTexture() const
	{
		return Albedo != nullptr || Normal != nullptr || MetallicRoughness != nullptr || Occlusion != nullptr ||
			   Emissive != nullptr;
	}

	Ref<Material> Material::Create(nvrhi::IDevice* device, nvrhi::IBindingLayout* layout,
		const MaterialFactors& factors, const MaterialTextures& textures, const std::string& debugName)
	{
		if (device == nullptr || layout == nullptr)
		{
			AE_CORE_ERROR("Material::Create needs a device and a material binding layout");
			return nullptr;
		}
		if (textures.Albedo == nullptr || textures.Normal == nullptr || textures.MetallicRoughness == nullptr ||
			textures.Occlusion == nullptr || textures.Emissive == nullptr)
		{
			AE_CORE_ERROR("Material '{}' is missing a map; every slot needs the white fallback", debugName);
			return nullptr;
		}

		nvrhi::BindingSetDesc bindingSetDesc;
		bindingSetDesc.addItem(nvrhi::BindingSetItem::Texture_SRV(0, textures.Albedo->GetTexture()));
		bindingSetDesc.addItem(nvrhi::BindingSetItem::Texture_SRV(1, textures.Normal->GetTexture()));
		bindingSetDesc.addItem(nvrhi::BindingSetItem::Texture_SRV(2, textures.MetallicRoughness->GetTexture()));
		bindingSetDesc.addItem(nvrhi::BindingSetItem::Texture_SRV(3, textures.Occlusion->GetTexture()));
		bindingSetDesc.addItem(nvrhi::BindingSetItem::Texture_SRV(4, textures.Emissive->GetTexture()));
		bindingSetDesc.addItem(nvrhi::BindingSetItem::Sampler(5, textures.Albedo->GetSampler()));
		bindingSetDesc.addItem(nvrhi::BindingSetItem::Sampler(6, textures.Normal->GetSampler()));
		bindingSetDesc.addItem(nvrhi::BindingSetItem::Sampler(7, textures.MetallicRoughness->GetSampler()));
		bindingSetDesc.addItem(nvrhi::BindingSetItem::Sampler(8, textures.Occlusion->GetSampler()));
		bindingSetDesc.addItem(nvrhi::BindingSetItem::Sampler(9, textures.Emissive->GetSampler()));

		Ref<Material> material = CreateRef<Material>();
		material->m_BindingSet = device->createBindingSet(bindingSetDesc, layout);
		if (material->m_BindingSet == nullptr)
		{
			AE_CORE_ERROR("Could not create the binding set of material '{}'", debugName);
			return nullptr;
		}
		material->m_Factors = factors;
		material->m_Textures = textures;
		material->m_DebugName = debugName;
		return material;
	}
}
