#pragma once

#include "Aegis/Core/Base.h"

#include <string_view>

namespace aegis
{
	class Scene;
	class Timestep;

	// Systems extend the scene update without the Scene depending on them.
	// Physics, scripting and audio register themselves; the editor and the runtime
	// both drive the same Scene::OnUpdate path.
	class SceneSystem
	{
	public:
		virtual ~SceneSystem() = default;

		virtual std::string_view GetName() const = 0;

		// Lower runs first. Physics uses -100, scripting 100, audio 200.
		virtual i32 GetUpdateOrder() const { return 0; }

		virtual void OnUpdate(Scene& scene, Timestep timestep) = 0;
	};
}
