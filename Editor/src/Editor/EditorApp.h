#pragma once

#include "Aegis/Core/Application.h"
#include "Editor/EditorLayer.h"

namespace aegis
{
	class EditorApp final : public Application
	{
	public:
		explicit EditorApp(const EditorAutomationOptions& automation = {});
		~EditorApp() override;

		EditorApp(const EditorApp&) = delete;
		EditorApp& operator=(const EditorApp&) = delete;

		EditorLayer* GetEditorLayer() const { return m_EditorLayer; }

	protected:
		void OnEvent(Event& event) override;

	private:
		EditorLayer* m_EditorLayer = nullptr;
		EditorAutomationOptions m_Automation;
	};
}
