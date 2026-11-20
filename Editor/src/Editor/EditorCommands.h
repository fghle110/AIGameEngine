#pragma once

#include "Aegis/Core/Base.h"
#include "Aegis/Core/Uuid.h"

#include <functional>
#include <string>

namespace aegis
{
class Scene;
class Renderer;
class AssetCache;

// Everything the editor command handlers need from the running editor. The
// context is passed once to RegisterEditorCommands(); handlers copy it, so all
// pointers must stay valid while the editor runs. The UI and the AI HTTP API
// execute exactly the same commands, which is what keeps the two in sync.
struct EditorCommandContext
{
	aegis::Scene* Scene = nullptr;
	Uuid* Selection = nullptr;
	aegis::Renderer* Renderer = nullptr;
	// GPU asset cache used by import commands; null when no renderer is attached
	// (headless command tests), in which case imports fail with a clear error.
	aegis::AssetCache* Assets = nullptr;

	// Project directory used to resolve relative scene/screenshot paths.
	std::string ProjectRoot;

	// Queues a screenshot of the next rendered frame (Renderer::RequestScreenshot).
	std::function<void(const std::string&)> ScreenshotRequest;

	// Saves the editor's current scene. Returns false and writes a readable
	// reason through the out parameter on failure; on success the string may
	// carry the path that was written.
	std::function<bool(std::string*)> SaveScene;

	// Loads a scene from an absolute or project-relative path. Returns false and
	// writes a readable reason through the out parameter on failure.
	std::function<bool(const std::string&, std::string*)> LoadScene;
};

// Registers the editor command set on CommandBus::Get(). Calling it again with
// a new context (the editor or a test replacing its scene) replaces the
// previous handlers, so the most recent context always wins.
void RegisterEditorCommands(EditorCommandContext& context);
} // namespace aegis
