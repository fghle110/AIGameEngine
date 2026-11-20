#include "Aegis/Core/LayerStack.h"

#include "Aegis/Core/Layer.h"

#include <algorithm>

namespace aegis
{
	LayerStack::~LayerStack()
	{
		Clear();
	}

	void LayerStack::Clear()
	{
		// Detach in reverse order so overlays see a valid stack while shutting down.
		for (auto it = m_Layers.rbegin(); it != m_Layers.rend(); ++it)
		{
			(*it)->OnDetach();
			delete *it;
		}
		m_Layers.clear();
		m_LayerInsertIndex = 0;
	}

	void LayerStack::PushLayer(Layer* layer)
	{
		AE_ASSERT(layer != nullptr);
		m_Layers.insert(m_Layers.begin() + m_LayerInsertIndex, layer);
		++m_LayerInsertIndex;
		layer->OnAttach();
	}

	void LayerStack::PushOverlay(Layer* overlay)
	{
		AE_ASSERT(overlay != nullptr);
		m_Layers.push_back(overlay);
		overlay->OnAttach();
	}

	void LayerStack::PopLayer(Layer* layer)
	{
		const auto it = std::find(m_Layers.begin(), m_Layers.end(), layer);
		if (it == m_Layers.end())
		{
			return;
		}
		layer->OnDetach();
		m_Layers.erase(it);
		--m_LayerInsertIndex;
		delete layer;
	}

	void LayerStack::PopOverlay(Layer* overlay)
	{
		const auto it = std::find(m_Layers.begin(), m_Layers.end(), overlay);
		if (it == m_Layers.end())
		{
			return;
		}
		overlay->OnDetach();
		m_Layers.erase(it);
		delete overlay;
	}
}
