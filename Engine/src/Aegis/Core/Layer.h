#pragma once

#include "Aegis/Core/Base.h"
#include "Aegis/Core/Time.h"

#include <string>

namespace aegis
{
	class Event;

	// A layer is a slice of application behaviour (game, editor UI, debug tools).
	// Layers are updated in stack order; overlays always update after layers.
	class Layer
	{
	public:
		explicit Layer(std::string name = "Layer");
		virtual ~Layer() = default;

		virtual void OnAttach() {}
		virtual void OnDetach() {}
		virtual void OnUpdate(Timestep timestep) { (void)timestep; }
		virtual void OnImGuiRender() {}
		virtual void OnEvent(Event& event) { (void)event; }

		const std::string& GetName() const { return m_DebugName; }
		void SetName(std::string name) { m_DebugName = std::move(name); }

	protected:
		std::string m_DebugName;
	};
}
