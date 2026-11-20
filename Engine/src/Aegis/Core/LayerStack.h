#pragma once

#include "Aegis/Core/Base.h"

#include <vector>

namespace aegis
{
	class Layer;

	class LayerStack
	{
	public:
		LayerStack() = default;
		~LayerStack();

		LayerStack(const LayerStack&) = delete;
		LayerStack& operator=(const LayerStack&) = delete;

		void PushLayer(Layer* layer);
		void PushOverlay(Layer* overlay);
		void PopLayer(Layer* layer);
		void PopOverlay(Layer* overlay);

		// Detaches and destroys every layer, leaving the stack empty.
		void Clear();

		std::vector<Layer*>::iterator begin() { return m_Layers.begin(); }
		std::vector<Layer*>::iterator end() { return m_Layers.end(); }
		std::vector<Layer*>::reverse_iterator rbegin() { return m_Layers.rbegin(); }
		std::vector<Layer*>::reverse_iterator rend() { return m_Layers.rend(); }
		std::vector<Layer*>::const_iterator begin() const { return m_Layers.begin(); }
		std::vector<Layer*>::const_iterator end() const { return m_Layers.end(); }

		usize GetCount() const { return m_Layers.size(); }

	private:
		std::vector<Layer*> m_Layers;
		u32 m_LayerInsertIndex = 0;
	};
}
