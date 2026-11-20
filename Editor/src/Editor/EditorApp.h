#pragma once

#include "Aegis/Core/Application.h"

namespace aegis
{
	class EditorApp final : public Application
	{
	public:
		EditorApp();
		~EditorApp() override;

		EditorApp(const EditorApp&) = delete;
		EditorApp& operator=(const EditorApp&) = delete;

	protected:
		void OnEvent(Event& event) override;
	};
}
