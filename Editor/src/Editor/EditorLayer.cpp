#include "Editor/EditorLayer.h"

#include "Aegis.h"
#include "Aegis/Assets/Material.h"
#include "Aegis/Assets/Mesh.h"
#include "Aegis/Assets/Texture.h"
#include "Aegis/Commands/CommandBus.h"
#include "Aegis/Core/Application.h"
#include "Aegis/Math/EditorGizmo.h"
#include "Aegis/Core/Input.h"
#include "Aegis/Core/KeyCodes.h"
#include "Aegis/Events/ApplicationEvent.h"
#include "Aegis/Audio/AudioEngine.h"
#include "Aegis/Physics/PhysicsWorld.h"
#include "Aegis/Renderer/ImGuiRenderer.h"
#include "Aegis/Scripting/LuaEngine.h"
#include "Aegis/Renderer/Renderer.h"
#include "Aegis/Renderer/SceneRenderer.h"
#include "Aegis/Scene/Camera.h"
#include "Aegis/Scene/Entity.h"
#include "Aegis/Serialization/SceneSerializer.h"

#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/quaternion.hpp>
#include <glm/gtc/type_ptr.hpp>

#include <imgui.h>
#include <imgui_internal.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <vector>

namespace aegis
{
	namespace
	{
		constexpr const char* c_ViewportName = "Viewport";
		constexpr const char* c_HierarchyName = "Hierarchy";
		constexpr const char* c_InspectorName = "Inspector";
		constexpr const char* c_ConsoleName = "Console";
		constexpr const char* c_AssetsName = "Assets";
		constexpr const char* c_StatsName = "Stats";

		ImVec4 ToImVec4(const glm::vec3& color, f32 alpha = 1.0f)
		{
			return ImVec4(color.r, color.g, color.b, alpha);
		}

		void DrawVec3Control(const char* label, glm::vec3& values, f32 resetValue = 0.0f, f32 speed = 0.05f)
		{
			ImGui::PushID(label);
			ImGui::Columns(2);
			ImGui::SetColumnWidth(0, 110.0f);
			ImGui::TextUnformatted(label);
			ImGui::NextColumn();

			const f32 available = ImGui::GetContentRegionAvail().x;
			const f32 itemWidth = available / 3.0f - 2.0f;
			ImGui::PushMultiItemsWidths(3, available);

			ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(2.0f, 2.0f));
			for (int axis = 0; axis < 3; ++axis)
			{
				const char* labels[3] = { "X", "Y", "Z" };
				const ImVec4 colors[3] = { ImVec4(0.85f, 0.3f, 0.3f, 1.0f), ImVec4(0.3f, 0.8f, 0.35f, 1.0f),
					ImVec4(0.35f, 0.5f, 0.95f, 1.0f) };
				ImGui::PushStyleColor(ImGuiCol_Text, colors[axis]);
				ImGui::Button(labels[axis], ImVec2(18.0f, 0.0f));
				ImGui::PopStyleColor();
				ImGui::SameLine();
				ImGui::PushID(axis);
				ImGui::SetNextItemWidth(itemWidth);
				ImGui::DragFloat("##value", &values[axis], speed, 0.0f, 0.0f, "%.3f");
				ImGui::PopID();
				ImGui::PopItemWidth();
				if (axis != 2)
				{
					ImGui::SameLine();
				}
			}
			ImGui::PopStyleVar();

			ImGui::Columns(1);
			ImGui::PopID();
		}

		bool ProjectToViewport(const glm::mat4& viewProjection, const glm::vec3& world, const ImVec2& origin,
			const ImVec2& size, ImVec2& outScreen)
		{
			const glm::vec4 clip = viewProjection * glm::vec4(world, 1.0f);
			if (clip.w <= 0.0001f)
			{
				return false;
			}
			const glm::vec3 ndc = glm::vec3(clip) / clip.w;
			outScreen = ImVec2(origin.x + (ndc.x * 0.5f + 0.5f) * size.x, origin.y + (0.5f - ndc.y * 0.5f) * size.y);
			return true;
		}

		bool RayIntersectsAabb(const glm::vec3& origin, const glm::vec3& direction, const glm::mat4& worldToLocal,
			const glm::vec3& halfExtents, f32& outDistance)
		{
			const glm::vec3 localOrigin = glm::vec3(worldToLocal * glm::vec4(origin, 1.0f));
			const glm::vec3 localDirection = glm::vec3(worldToLocal * glm::vec4(direction, 0.0f));

			f32 tMin = -1.0e30f;
			f32 tMax = 1.0e30f;
			for (int axis = 0; axis < 3; ++axis)
			{
				const f32 extent = halfExtents[axis];
				if (std::abs(localDirection[axis]) < 1.0e-6f)
				{
					if (std::abs(localOrigin[axis]) > extent)
					{
						return false;
					}
					continue;
				}
				f32 t1 = (-extent - localOrigin[axis]) / localDirection[axis];
				f32 t2 = (extent - localOrigin[axis]) / localDirection[axis];
				if (t1 > t2)
				{
					std::swap(t1, t2);
				}
				tMin = std::max(tMin, t1);
				tMax = std::min(tMax, t2);
				if (tMin > tMax)
				{
					return false;
				}
			}
			if (tMax < 0.0f)
			{
				return false;
			}
			outDistance = tMin > 0.0f ? tMin : tMax;
			return true;
		}

		void DrawLine3D(ImDrawList* drawList, const glm::mat4& viewProjection, const ImVec2& origin, const ImVec2& size,
			const glm::vec3& a, const glm::vec3& b, ImU32 color, f32 thickness = 1.0f)
		{
			ImVec2 screenA;
			ImVec2 screenB;
			if (!ProjectToViewport(viewProjection, a, origin, size, screenA) ||
				!ProjectToViewport(viewProjection, b, origin, size, screenB))
			{
				return;
			}
			drawList->AddLine(screenA, screenB, color, thickness);
		}
	}

	EditorLayer::EditorLayer(const EditorAutomationOptions& automation)
		: Layer("Editor")
		, m_Automation(automation)
		, m_RemainingFrames(automation.Frames)
	{
	}

	EditorLayer::~EditorLayer() = default;

	void EditorLayer::OnAttach()
	{
		Application* application = Application::Get();
		AE_ASSERT(application != nullptr, "The editor must be created through an Application");
		Window& window = application->GetWindow();

		RendererSpecification rendererSpecification;
		rendererSpecification.Window = &window;
		rendererSpecification.VSync = window.IsVSync();
		rendererSpecification.EnableValidation = true;
		m_Renderer = CreateScope<Renderer>(rendererSpecification);
		if (!m_Renderer->IsInitialized())
		{
			AE_CORE_CRITICAL("The editor cannot start without a working renderer");
			application->Close();
			return;
		}

		const std::string shaderDirectory = FileSystem::ToUtf8(FileSystem::GetExecutableDirectory() / "shaders");
		m_ImGui = CreateScope<ImGuiRenderer>();
		if (!m_ImGui->Initialize(m_Renderer->GetDevice(), &window, shaderDirectory))
		{
			AE_CORE_CRITICAL("The editor cannot start without the ImGui renderer");
			application->Close();
			return;
		}

		m_SceneRenderer = CreateScope<SceneRenderer>();
		if (!m_SceneRenderer->Initialize(m_Renderer->GetDevice()))
		{
			AE_CORE_ERROR("The scene renderer could not be initialized; the viewport keeps the grid overlay only");
		}

		m_AssetDirectory = FileSystem::ToUtf8(FileSystem::GetCurrentWorkingDirectory() / "Assets");
		createDefaultScene();

		m_Physics = CreateScope<PhysicsWorld>();
		m_Physics->Initialize();
		m_Audio = CreateScope<AudioEngine>();
		m_Audio->Initialize();
		m_Scripting = CreateScope<LuaEngine>();
		m_Scripting->SetScriptRoot(FileSystem::GetCurrentWorkingDirectory() / "Scripts");
		m_Scripting->SetAudioEngine(m_Audio.get());
		m_Scripting->SetPhysicsWorld(m_Physics.get());
		rebindSceneSystems();

		startCommandServer();
		AE_CORE_INFO("Editor ready");
	}

	AssetHandle EditorLayer::createBuiltinMeshHandle(PrimitiveType type) const
	{
		if (m_SceneRenderer == nullptr || !m_SceneRenderer->IsInitialized())
		{
			return AssetHandle{};
		}
		return m_SceneRenderer->GetAssetCache().GetBuiltinMesh(type);
	}

	AssetHandle EditorLayer::createMaterialAsset(const MaterialFactors& factors, const MaterialTextures& textures,
		const char* name)
	{
		if (m_SceneRenderer == nullptr || !m_SceneRenderer->IsInitialized())
		{
			return AssetHandle{};
		}

		AssetCache& cache = m_SceneRenderer->GetAssetCache();
		const Uuid id = Uuid::Generate();
		if (cache.AddMaterial(id, factors, textures, name).IsNull())
		{
			return AssetHandle{};
		}
		return AssetHandle{ id, AssetType::Material };
	}

	Entity EditorLayer::createMeshEntity(const char* name, PrimitiveType primitive, const glm::vec3& position,
		const glm::vec3& scale, const AssetHandle& material, const glm::vec3& rotationDegrees)
	{
		Entity entity = m_Scene->CreateEntity(name);

		MeshRendererComponent renderer;
		renderer.Mesh = createBuiltinMeshHandle(primitive);
		if (material.IsValid())
		{
			renderer.Materials.push_back(material);
		}
		entity.AddComponent<MeshRendererComponent>(renderer);

		TransformComponent& transform = entity.GetComponent<TransformComponent>();
		transform.LocalPosition = position;
		transform.LocalScale = scale;
		transform.SetLocalEulerAngles(rotationDegrees);
		return entity;
	}

	void EditorLayer::createDefaultScene()
	{
		if (m_Automation.DemoScene)
		{
			createDemoScene();
			return;
		}

		m_Scene = CreateScope<Scene>("Untitled Scene");

		Entity cameraEntity = m_Scene->CreateEntity("Main Camera");
		CameraComponent camera;
		camera.Primary = true;
		cameraEntity.AddComponent<CameraComponent>(camera);
		TransformComponent& cameraTransform = cameraEntity.GetComponent<TransformComponent>();
		cameraTransform.LocalPosition = glm::vec3(0.0f, 3.0f, 10.0f);
		cameraTransform.SetLocalEulerAngles(glm::vec3(-15.0f, 0.0f, 0.0f));

		Entity sunEntity = m_Scene->CreateEntity("Sun");
		LightComponent sun;
		sun.Type = LightType::Directional;
		sun.Intensity = 3.0f;
		sun.Color = glm::vec3(1.0f, 0.96f, 0.88f);
		sunEntity.AddComponent<LightComponent>(sun);
		sunEntity.GetComponent<TransformComponent>().SetLocalEulerAngles(glm::vec3(-50.0f, 30.0f, 0.0f));

		MaterialFactors groundFactors;
		groundFactors.BaseColorFactor = glm::vec4(0.34f, 0.36f, 0.4f, 1.0f);
		groundFactors.RoughnessFactor = 0.85f;
		Entity groundEntity = createMeshEntity("Ground", PrimitiveType::Plane, glm::vec3(0.0f, 0.0f, 0.0f),
			glm::vec3(20.0f, 1.0f, 20.0f), createMaterialAsset(groundFactors, MaterialTextures{}, "Ground"));
		groundEntity.GetComponent<MeshRendererComponent>().CastShadows = false;

		MaterialFactors boxFactors;
		boxFactors.BaseColorFactor = glm::vec4(0.72f, 0.42f, 0.18f, 1.0f);
		boxFactors.RoughnessFactor = 0.45f;
		Entity boxEntity = createMeshEntity("Box", PrimitiveType::Cube, glm::vec3(0.0f, 1.0f, 0.0f),
			glm::vec3(1.0f), createMaterialAsset(boxFactors, MaterialTextures{}, "Box"));
		RigidBodyComponent rigidBody;
		rigidBody.Type = BodyType::Dynamic;
		boxEntity.AddComponent<RigidBodyComponent>(rigidBody);
		boxEntity.AddComponent<ColliderComponent>(ColliderComponent{});

		m_Scene->EnsurePrimaryCamera();
		m_Scene->UpdateTransforms();
	}

	void EditorLayer::createDemoScene()
	{
		m_Scene = CreateScope<Scene>("Demo Scene");

		// Match the ambient fill to the procedural sky the background pass draws, so a
		// fully metallic material reflects a visible environment instead of black.
		EnvironmentSettings& environment = m_Scene->GetEnvironment();
		environment.AmbientColor = glm::vec3(0.88f, 0.93f, 1.0f);
		environment.AmbientIntensity = 0.85f;
		environment.Exposure = 1.0f;
		environment.EnableShadows = true;
		environment.ShadowDistance = 40.0f;

		Entity cameraEntity = m_Scene->CreateEntity("Main Camera");
		CameraComponent camera;
		camera.Primary = true;
		camera.FieldOfView = 50.0f;
		cameraEntity.AddComponent<CameraComponent>(camera);
		TransformComponent& cameraTransform = cameraEntity.GetComponent<TransformComponent>();
		cameraTransform.LocalPosition = glm::vec3(0.0f, 3.2f, 9.0f);
		cameraTransform.SetLocalEulerAngles(glm::vec3(-14.0f, 0.0f, 0.0f));

		Entity sunEntity = m_Scene->CreateEntity("Sun");
		LightComponent sun;
		sun.Type = LightType::Directional;
		sun.Intensity = 4.0f;
		sun.Color = glm::vec3(1.0f, 0.95f, 0.86f);
		sun.CastShadows = true;
		sunEntity.AddComponent<LightComponent>(sun);
		sunEntity.GetComponent<TransformComponent>().SetLocalEulerAngles(glm::vec3(-42.0f, 28.0f, 0.0f));

		Entity pointEntity = m_Scene->CreateEntity("Point Light");
		LightComponent point;
		point.Type = LightType::Point;
		point.Color = glm::vec3(1.0f, 0.45f, 0.2f);
		point.Intensity = 30.0f;
		point.Range = 14.0f;
		pointEntity.AddComponent<LightComponent>(point);
		pointEntity.GetComponent<TransformComponent>().LocalPosition = glm::vec3(3.4f, 2.4f, 1.2f);

		// A checkerboard floor proves the texture path end to end; the primitives above
		// it exercise the dielectric and metallic ends of the BRDF.
		MaterialTextures floorTextures;
		if (m_SceneRenderer != nullptr && m_SceneRenderer->IsInitialized())
		{
			AssetCache& cache = m_SceneRenderer->GetAssetCache();
			const Image checkerboard = Image::CreateCheckerboard(256, 256, 32, glm::vec3(0.40f, 0.41f, 0.44f),
				glm::vec3(0.14f, 0.15f, 0.17f));
			const Uuid textureId = Uuid::Generate();
			if (!cache.AddTexture(textureId, checkerboard, SamplerSettings::Albedo()).IsNull())
			{
				floorTextures.Albedo = cache.GetTexture(textureId);
			}
		}

		MaterialFactors floorFactors;
		floorFactors.BaseColorFactor = glm::vec4(1.0f, 1.0f, 1.0f, 1.0f);
		floorFactors.RoughnessFactor = 0.8f;
		const AssetHandle floorMaterial = createMaterialAsset(floorFactors, floorTextures, "Checkered Floor");
		// The floor plane is a single-sided surface seen from above, so it must not try
		// to cast a shadow into itself.
		Entity floorEntity = createMeshEntity("Floor", PrimitiveType::Plane, glm::vec3(0.0f, 0.0f, 0.0f),
			glm::vec3(24.0f, 1.0f, 24.0f), floorMaterial);
		floorEntity.GetComponent<MeshRendererComponent>().CastShadows = false;

		MaterialFactors paintedFactors;
		paintedFactors.BaseColorFactor = glm::vec4(0.85f, 0.22f, 0.18f, 1.0f);
		paintedFactors.RoughnessFactor = 0.32f;
		const AssetHandle paintedMaterial = createMaterialAsset(paintedFactors, MaterialTextures{}, "Painted Red");

		MaterialFactors goldFactors;
		goldFactors.BaseColorFactor = glm::vec4(1.0f, 0.78f, 0.35f, 1.0f);
		goldFactors.MetallicFactor = 1.0f;
		goldFactors.RoughnessFactor = 0.22f;
		const AssetHandle goldMaterial = createMaterialAsset(goldFactors, MaterialTextures{}, "Polished Gold");

		MaterialFactors ceramicFactors;
		ceramicFactors.BaseColorFactor = glm::vec4(0.18f, 0.32f, 0.85f, 1.0f);
		ceramicFactors.RoughnessFactor = 0.12f;
		const AssetHandle ceramicMaterial = createMaterialAsset(ceramicFactors, MaterialTextures{}, "Glazed Blue");

		MaterialFactors grassFactors;
		grassFactors.BaseColorFactor = glm::vec4(0.24f, 0.62f, 0.32f, 1.0f);
		grassFactors.RoughnessFactor = 0.55f;
		const AssetHandle grassMaterial = createMaterialAsset(grassFactors, MaterialTextures{}, "Green Plastic");

		createMeshEntity("Red Cube", PrimitiveType::Cube, glm::vec3(-2.1f, 0.5f, 0.4f), glm::vec3(1.0f),
			paintedMaterial, glm::vec3(0.0f, 20.0f, 0.0f));
		createMeshEntity("Gold Cube", PrimitiveType::Cube, glm::vec3(0.1f, 0.7f, -1.1f),
			glm::vec3(1.4f), goldMaterial, glm::vec3(0.0f, -12.0f, 0.0f));
		createMeshEntity("Blue Sphere", PrimitiveType::Sphere, glm::vec3(2.2f, 0.6f, 0.5f),
			glm::vec3(1.2f), ceramicMaterial);
		createMeshEntity("Green Cylinder", PrimitiveType::Cylinder, glm::vec3(-1.4f, 0.75f, -2.3f),
			glm::vec3(0.9f, 1.5f, 0.9f), grassMaterial);

		m_Scene->EnsurePrimaryCamera();
		m_Scene->UpdateTransforms();
	}

	void EditorLayer::startCommandServer()
	{
		if (m_CommandServerStarted || m_Scene == nullptr)
		{
			return;
		}

		m_CommandContext.Scene = m_Scene.get();
		m_CommandContext.Selection = &m_SelectedEntity;
		m_CommandContext.Renderer = m_Renderer.get();
		m_CommandContext.ProjectRoot = FileSystem::ToUtf8(FileSystem::GetCurrentWorkingDirectory());
		m_CommandContext.ScreenshotRequest = [this](const std::string& path)
		{
			if (m_Renderer != nullptr)
			{
				m_Renderer->RequestScreenshot(path);
			}
		};
		m_CommandContext.SaveScene = [this](std::string* error)
		{
			const std::string path = m_ScenePath.empty() ? "Saved/EditorScene.aescene" : m_ScenePath;
			return saveSceneToFile(path, error);
		};
		m_CommandContext.LoadScene = [this](const std::string& path, std::string* error)
		{
			return loadSceneFromFile(path, error);
		};

		RegisterEditorCommands(m_CommandContext);
		m_CommandServerStarted = m_CommandServer.Start(c_DefaultCommandPort);
		if (m_CommandServerStarted)
		{
			AE_CORE_INFO("Editor command API listening on 127.0.0.1:{}", m_CommandServer.GetPort());
		}
		else
		{
			AE_CORE_ERROR("Could not start the editor command API on port {}", c_DefaultCommandPort);
		}
	}

	void EditorLayer::stopCommandServer()
	{
		if (!m_CommandServerStarted)
		{
			return;
		}
		m_CommandServer.Stop();
		m_CommandServerStarted = false;
	}

	bool EditorLayer::saveSceneToFile(const std::string& path, std::string* error)
	{
		if (m_Scene == nullptr)
		{
			if (error != nullptr)
			{
				*error = "No scene is open";
			}
			return false;
		}
		if (!SceneSerializer::Save(*m_Scene, FileSystem::FromUtf8(path)))
		{
			if (error != nullptr)
			{
				*error = std::format("Could not save the scene to '{}'", path);
			}
			return false;
		}
		m_ScenePath = path;
		if (error != nullptr)
		{
			*error = path;
		}
		AE_CORE_INFO("Scene saved to '{}'", path);
		return true;
	}

	bool EditorLayer::loadSceneFromFile(const std::string& path, std::string* error)
	{
		auto scene = CreateScope<Scene>();
		std::string loadError;
		if (!SceneSerializer::Load(*scene, FileSystem::FromUtf8(path), &loadError))
		{
			if (error != nullptr)
			{
				*error = loadError;
			}
			return false;
		}
		m_Scene = std::move(scene);
		m_ScenePath = path;
		m_SelectedEntity = Uuid::Null();
		rebindSceneSystems();
		AE_CORE_INFO("Scene loaded from '{}'", path);
		return true;
	}

	void EditorLayer::rebindSceneSystems()
	{
		if (m_Scene == nullptr)
		{
			return;
		}
		const bool sceneChanged = m_BoundScene != nullptr && m_BoundScene != m_Scene.get();
		m_BoundScene = m_Scene.get();
		m_CommandContext.Scene = m_Scene.get();

		if (m_Scripting != nullptr)
		{
			if (sceneChanged)
			{
				m_Scripting->Shutdown();
			}
			m_Scripting->Initialize(*m_Scene);
			m_Scene->AddSystem(m_Scripting.get());
		}
		if (m_Physics != nullptr)
		{
			m_Scene->AddSystem(m_Physics.get());
		}
		if (m_Audio != nullptr)
		{
			m_Scene->AddSystem(m_Audio.get());
		}
	}

	void EditorLayer::OnDetach()
	{
		stopCommandServer();
		if (m_Scene != nullptr)
		{
			m_Scene->RemoveSystem(m_Physics.get());
			m_Scene->RemoveSystem(m_Scripting.get());
			m_Scene->RemoveSystem(m_Audio.get());
		}
		if (m_Scripting != nullptr)
		{
			m_Scripting->Shutdown();
		}
		if (m_Audio != nullptr)
		{
			m_Audio->Shutdown();
		}
		if (m_Physics != nullptr)
		{
			m_Physics->Shutdown();
		}
		m_Scripting = nullptr;
		m_Audio = nullptr;
		m_Physics = nullptr;
		m_SceneRenderer = nullptr;
		m_ImGui = nullptr;
		m_Renderer = nullptr;
		m_Scene = nullptr;
	}

	void EditorLayer::OnUpdate(Timestep timestep)
	{
		if (m_Scene == nullptr || m_Renderer == nullptr)
		{
			return;
		}
		if (m_CommandServerStarted)
		{
			m_CommandServer.ProcessPending();
		}
		if (m_BoundScene != m_Scene.get())
		{
			rebindSceneSystems();
		}
		handleShortcuts();
		m_Scene->OnUpdate(timestep);
		m_EditorCamera.OnUpdate(timestep, m_ViewportHovered);
	}

	void EditorLayer::OnEvent(Event& event)
	{
		if (event.GetEventType() == EventType::WindowResize)
		{
			auto& resize = static_cast<WindowResizeEvent&>(event);
			if (m_Renderer != nullptr)
			{
				m_Renderer->Resize(resize.GetWidth(), resize.GetHeight());
			}
		}
	}

	void EditorLayer::OnImGuiRender()
	{
		if (m_ImGui == nullptr || !m_ImGui->IsInitialized() || m_Scene == nullptr)
		{
			return;
		}

		m_ImGui->NewFrame();

		drawMenuBar();

		const ImGuiViewport* mainViewport = ImGui::GetMainViewport();
		const ImGuiID dockspaceId = ImGui::GetID("AegisDockSpace");
		if (ImGui::DockBuilderGetNode(dockspaceId) == nullptr || m_ResetLayoutRequested)
		{
			buildDefaultLayout(dockspaceId);
			m_ResetLayoutRequested = false;
		}
		ImGui::DockSpaceOverViewport(dockspaceId, mainViewport, ImGuiDockNodeFlags_PassthruCentralNode);

		drawHierarchy();
		drawInspector();
		drawViewport();
		if (m_ShowConsole)
		{
			drawConsole();
		}
		if (m_ShowStats)
		{
			drawStats();
		}
		if (m_ShowAssetBrowser)
		{
			drawAssetBrowser();
		}
		if (m_ShowImGuiDemo)
		{
			ImGui::ShowDemoWindow(&m_ShowImGuiDemo);
		}
		drawAutomationFooter();

		ImGui::Render();

		const glm::vec4 clearColor(0.055f, 0.06f, 0.075f, 1.0f);
		if (m_Renderer->BeginFrame(clearColor))
		{
			nvrhi::ICommandList* commandList = m_Renderer->GetCommandList();
			if (!m_Automation.ScreenshotPath.empty() && m_RemainingFrames == 1)
			{
				m_Renderer->RequestScreenshot(m_Automation.ScreenshotPath);
			}
			// The scene renders into its offscreen targets first; ImGui then composites
			// the viewport image and the rest of the UI over the swapchain image.
			renderScene(commandList);
			m_ImGui->Render(commandList, m_Renderer->GetFramebuffer(), static_cast<f32>(m_Renderer->GetWidth()),
				static_cast<f32>(m_Renderer->GetHeight()));
			m_Renderer->EndFrame();
		}

		if (m_RemainingFrames > 0)
		{
			--m_RemainingFrames;
			if (m_RemainingFrames == 0)
			{
				AE_CORE_INFO("Automation finished after {} frames", m_Automation.Frames);
				if (Application* application = Application::Get())
				{
					application->Close();
				}
			}
		}
	}

	void EditorLayer::drawMenuBar()
	{
		if (!ImGui::BeginMainMenuBar())
		{
			return;
		}

		if (ImGui::BeginMenu("File"))
		{
			if (ImGui::MenuItem("New Scene", "Ctrl+N"))
			{
				createDefaultScene();
			}
			ImGui::Separator();
			if (ImGui::MenuItem("Exit", "Alt+F4"))
			{
				if (Application* application = Application::Get())
				{
					application->Close();
				}
			}
			ImGui::EndMenu();
		}

		if (ImGui::BeginMenu("GameObject"))
		{
			if (ImGui::MenuItem("Empty Entity"))
			{
				Entity entity = m_Scene->CreateEntity("Entity");
				m_SelectedEntity = entity.GetUuid();
			}
			if (ImGui::BeginMenu("Create"))
			{
				if (ImGui::MenuItem("Camera"))
				{
					Entity entity = m_Scene->CreateEntity("Camera");
					entity.AddComponent<CameraComponent>(CameraComponent{});
					m_SelectedEntity = entity.GetUuid();
				}
				if (ImGui::MenuItem("Directional Light"))
				{
					Entity entity = m_Scene->CreateEntity("Directional Light");
					LightComponent light;
					light.Type = LightType::Directional;
					entity.AddComponent<LightComponent>(light);
					m_SelectedEntity = entity.GetUuid();
				}
				if (ImGui::MenuItem("Point Light"))
				{
					Entity entity = m_Scene->CreateEntity("Point Light");
					LightComponent light;
					light.Type = LightType::Point;
					entity.AddComponent<LightComponent>(light);
					m_SelectedEntity = entity.GetUuid();
				}
				if (ImGui::MenuItem("Spot Light"))
				{
					Entity entity = m_Scene->CreateEntity("Spot Light");
					LightComponent light;
					light.Type = LightType::Spot;
					entity.AddComponent<LightComponent>(light);
					m_SelectedEntity = entity.GetUuid();
				}
				if (ImGui::MenuItem("Mesh Renderer"))
				{
					Entity entity = m_Scene->CreateEntity("Mesh");
					entity.AddComponent<MeshRendererComponent>(MeshRendererComponent{});
					m_SelectedEntity = entity.GetUuid();
				}
				ImGui::EndMenu();
			}
			ImGui::EndMenu();
		}

		if (ImGui::BeginMenu("View"))
		{
			ImGui::MenuItem("Stats", nullptr, &m_ShowStats);
			ImGui::MenuItem("Console", nullptr, &m_ShowConsole);
			ImGui::MenuItem("Assets", nullptr, &m_ShowAssetBrowser);
			ImGui::MenuItem("ImGui Demo", nullptr, &m_ShowImGuiDemo);
			ImGui::Separator();
			if (ImGui::MenuItem("Reset Layout"))
			{
				m_ResetLayoutRequested = true;
			}
			ImGui::EndMenu();
		}

		if (ImGui::BeginMenu("Help"))
		{
			ImGui::MenuItem("About Aegis", nullptr, &m_ShowAbout);
			ImGui::EndMenu();
		}

		if (Application* application = Application::Get())
		{
			char fpsText[64];
			std::snprintf(fpsText, sizeof(fpsText), "%.0f fps | %.2f ms", application->GetFramesPerSecond(),
				application->GetAverageFrameTime() * 1000.0f);
			const f32 textWidth = ImGui::CalcTextSize(fpsText).x;
			ImGui::SameLine(ImGui::GetWindowWidth() - textWidth - 16.0f);
			ImGui::TextUnformatted(fpsText);
		}

		ImGui::EndMainMenuBar();

		if (m_ShowAbout)
		{
			ImGui::OpenPopup("About Aegis");
			m_ShowAbout = false;
		}
		if (ImGui::BeginPopupModal("About Aegis", nullptr, ImGuiWindowFlags_AlwaysAutoResize))
		{
			ImGui::Text("%s %s", std::string(c_EngineName).c_str(), std::string(c_EngineVersion).c_str());
			ImGui::Separator();
			ImGui::TextUnformatted("A production-grade 3D game engine built for humans and AI agents.");
			ImGui::Text("GPU: %s", m_Renderer != nullptr ? m_Renderer->GetGpuName().c_str() : "not initialized");
			if (ImGui::Button("Close"))
			{
				ImGui::CloseCurrentPopup();
			}
			ImGui::EndPopup();
		}
	}

	void EditorLayer::buildDefaultLayout(u32 dockspaceId)
	{
		ImGui::DockBuilderRemoveNode(dockspaceId);
		ImGui::DockBuilderAddNode(dockspaceId, ImGuiDockNodeFlags_DockSpace | ImGuiDockNodeFlags_PassthruCentralNode);
		ImGui::DockBuilderSetNodeSize(dockspaceId, ImGui::GetMainViewport()->Size);

		ImGuiID center = dockspaceId;
		ImGuiID left = 0;
		ImGuiID right = 0;
		ImGuiID bottom = 0;
		ImGui::DockBuilderSplitNode(center, ImGuiDir_Left, 0.18f, &left, &center);
		ImGui::DockBuilderSplitNode(center, ImGuiDir_Right, 0.26f, &right, &center);
		ImGui::DockBuilderSplitNode(center, ImGuiDir_Down, 0.30f, &bottom, &center);

		ImGuiID bottomRight = 0;
		ImGui::DockBuilderSplitNode(bottom, ImGuiDir_Right, 0.35f, &bottomRight, &bottom);

		ImGui::DockBuilderDockWindow(c_HierarchyName, left);
		ImGui::DockBuilderDockWindow(c_InspectorName, right);
		ImGui::DockBuilderDockWindow(c_ViewportName, center);
		ImGui::DockBuilderDockWindow(c_ConsoleName, bottom);
		ImGui::DockBuilderDockWindow(c_AssetsName, bottom);
		ImGui::DockBuilderDockWindow(c_StatsName, bottomRight);
		ImGui::DockBuilderFinish(dockspaceId);
	}

	Entity EditorLayer::GetSelectedEntity()
	{
		return m_Scene != nullptr ? m_Scene->FindEntity(m_SelectedEntity) : Entity();
	}

	void EditorLayer::drawHierarchy()
	{
		if (!ImGui::Begin(c_HierarchyName))
		{
			ImGui::End();
			return;
		}

		if (ImGui::Button("+ Create"))
		{
			Entity entity = m_Scene->CreateEntity("Entity");
			m_SelectedEntity = entity.GetUuid();
		}
		ImGui::SameLine();
		if (ImGui::Button("Delete") )
		{
			Entity selected = GetSelectedEntity();
			if (selected.IsValid())
			{
				m_Scene->DestroyEntity(selected);
				m_SelectedEntity = Uuid::Null();
			}
		}

		ImGui::Separator();

		if (ImGui::BeginChild("##hierarchy-scroll"))
		{
			for (Entity entity : m_Scene->GetEntities())
			{
				if (!entity.GetParent().IsValid())
				{
					drawEntityNode(entity);
				}
			}

			if (ImGui::IsMouseClicked(ImGuiMouseButton_Left) && ImGui::IsWindowHovered() && !ImGui::IsAnyItemHovered())
			{
				m_SelectedEntity = Uuid::Null();
			}
		}
		ImGui::EndChild();
		ImGui::End();
	}

	void EditorLayer::drawEntityNode(Entity entity)
	{
		if (!entity.IsValid())
		{
			return;
		}

		const std::vector<Entity> children = entity.GetChildren();
		ImGuiTreeNodeFlags flags = ImGuiTreeNodeFlags_OpenOnArrow | ImGuiTreeNodeFlags_SpanAvailWidth;
		if (children.empty())
		{
			flags |= ImGuiTreeNodeFlags_Leaf;
		}
		if (m_SelectedEntity == entity.GetUuid())
		{
			flags |= ImGuiTreeNodeFlags_Selected;
		}
		if (!entity.IsActive())
		{
			ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.6f, 0.6f, 0.6f, 1.0f));
		}

		const std::string uuidText = entity.GetUuid().ToString();
		ImGui::PushID(uuidText.c_str());
		const bool open = ImGui::TreeNodeEx("##node", flags, "%s", entity.GetName().c_str());
		ImGui::PopID();
		if (!entity.IsActive())
		{
			ImGui::PopStyleColor();
		}

		if (ImGui::IsItemClicked())
		{
			m_SelectedEntity = entity.GetUuid();
		}

		if (ImGui::BeginPopupContextItem("##entity-context"))
		{
			if (ImGui::MenuItem("Rename"))
			{
				m_SelectedEntity = entity.GetUuid();
			}
			if (ImGui::MenuItem("Delete"))
			{
				if (m_SelectedEntity == entity.GetUuid())
				{
					m_SelectedEntity = Uuid::Null();
				}
				m_Scene->DestroyEntity(entity);
				if (open)
				{
					ImGui::TreePop();
				}
				ImGui::EndPopup();
				return;
			}
			ImGui::EndPopup();
		}

		if (open)
		{
			for (Entity child : children)
			{
				drawEntityNode(child);
			}
			ImGui::TreePop();
		}
	}

	void EditorLayer::drawInspector()
	{
		if (!ImGui::Begin(c_InspectorName))
		{
			ImGui::End();
			return;
		}

		Entity entity = GetSelectedEntity();
		if (!entity.IsValid())
		{
			ImGui::TextDisabled("Nothing selected");
			ImGui::End();
			return;
		}

		char nameBuffer[256];
		std::snprintf(nameBuffer, sizeof(nameBuffer), "%s", entity.GetName().c_str());
		if (ImGui::InputText("Name", nameBuffer, sizeof(nameBuffer),
				ImGuiInputTextFlags_EnterReturnsTrue))
		{
			entity.SetName(nameBuffer);
		}

		bool active = entity.IsActive();
		if (ImGui::Checkbox("Active", &active))
		{
			entity.SetActive(active);
		}

		ImGui::TextDisabled("UUID: %s", entity.GetUuid().ToString().c_str());

		ImGui::Separator();
		if (ImGui::CollapsingHeader("Transform", ImGuiTreeNodeFlags_DefaultOpen))
		{
			drawTransform(entity);
		}

		if (entity.HasComponent<CameraComponent>())
		{
			ImGui::Separator();
			drawCameraComponent(entity);
		}
		if (entity.HasComponent<LightComponent>())
		{
			ImGui::Separator();
			drawLightComponent(entity);
		}
		if (entity.HasComponent<MeshRendererComponent>())
		{
			ImGui::Separator();
			drawMeshRendererComponent(entity);
		}
		if (entity.HasComponent<RigidBodyComponent>() || entity.HasComponent<ColliderComponent>())
		{
			ImGui::Separator();
			drawPhysicsComponents(entity);
		}
		if (entity.HasComponent<AudioSourceComponent>() || entity.HasComponent<AudioListenerComponent>())
		{
			ImGui::Separator();
			drawAudioComponents(entity);
		}
		if (entity.HasComponent<LuaScriptComponent>())
		{
			ImGui::Separator();
			if (ImGui::CollapsingHeader("Lua Script", ImGuiTreeNodeFlags_DefaultOpen))
			{
				LuaScriptComponent& script = entity.GetComponent<LuaScriptComponent>();
				ImGui::Text("Script: %s", script.Script.IsValid() ? script.Script.Id.ToString().c_str() : "(none)");
				ImGui::Checkbox("Enabled", &script.Enabled);
			}
		}
		if (entity.HasComponent<FlyCameraComponent>())
		{
			ImGui::Separator();
			if (ImGui::CollapsingHeader("Fly Camera"))
			{
				FlyCameraComponent& fly = entity.GetComponent<FlyCameraComponent>();
				ImGui::Checkbox("Active", &fly.Active);
				ImGui::DragFloat("Move Speed", &fly.MoveSpeed, 0.1f, 0.1f, 100.0f);
				ImGui::DragFloat("Look Sensitivity", &fly.LookSensitivity, 0.01f, 0.01f, 2.0f);
			}
		}

		ImGui::Separator();
		if (ImGui::Button("Add Component"))
		{
			ImGui::OpenPopup("AddComponent");
		}
		drawAddComponentPopup(entity);

		ImGui::End();
	}

	void EditorLayer::drawTransform(Entity entity)
	{
		TransformComponent& transform = entity.GetComponent<TransformComponent>();
		glm::vec3 position = transform.LocalPosition;
		if (ImGui::DragFloat3("Position", glm::value_ptr(position), 0.02f))
		{
			transform.LocalPosition = position;
		}

		glm::vec3 rotation = transform.GetLocalEulerAngles();
		if (ImGui::DragFloat3("Rotation", glm::value_ptr(rotation), 0.25f))
		{
			transform.SetLocalEulerAngles(rotation);
		}

		glm::vec3 scale = transform.LocalScale;
		if (ImGui::DragFloat3("Scale", glm::value_ptr(scale), 0.02f))
		{
			transform.LocalScale = glm::max(scale, glm::vec3(0.0001f));
		}

		const glm::vec3 world = transform.WorldPosition;
		ImGui::TextDisabled("World: %.2f, %.2f, %.2f", world.x, world.y, world.z);

		Entity parent = entity.GetParent();
		if (parent.IsValid())
		{
			ImGui::Text("Parent: %s", parent.GetName().c_str());
			if (ImGui::Button("Detach from Parent"))
			{
				entity.ClearParent();
			}
		}
	}

	void EditorLayer::drawCameraComponent(Entity entity)
	{
		if (!ImGui::CollapsingHeader("Camera", ImGuiTreeNodeFlags_DefaultOpen))
		{
			return;
		}
		CameraComponent& camera = entity.GetComponent<CameraComponent>();
		const char* projections[] = { "Perspective", "Orthographic" };
		int projection = static_cast<int>(camera.Projection);
		if (ImGui::Combo("Projection", &projection, projections, 2))
		{
			camera.Projection = static_cast<CameraProjection>(projection);
		}
		if (camera.Projection == CameraProjection::Perspective)
		{
			ImGui::DragFloat("Field of View", &camera.FieldOfView, 0.2f, 5.0f, 170.0f);
		}
		else
		{
			ImGui::DragFloat("Orthographic Size", &camera.OrthographicSize, 0.05f, 0.1f, 1000.0f);
		}
		ImGui::DragFloat("Near Clip", &camera.NearClip, 0.005f, 0.001f, 10.0f, "%.3f");
		ImGui::DragFloat("Far Clip", &camera.FarClip, 0.5f, 1.0f, 100000.0f);
		ImGui::Checkbox("Primary", &camera.Primary);
		ImGui::ColorEdit4("Clear Color", glm::value_ptr(camera.ClearColor));
		if (ImGui::Button("Remove Camera"))
		{
			entity.RemoveComponent<CameraComponent>();
		}
	}

	void EditorLayer::drawLightComponent(Entity entity)
	{
		if (!ImGui::CollapsingHeader("Light", ImGuiTreeNodeFlags_DefaultOpen))
		{
			return;
		}
		LightComponent& light = entity.GetComponent<LightComponent>();
		const char* types[] = { "Directional", "Point", "Spot" };
		int type = static_cast<int>(light.Type);
		if (ImGui::Combo("Type", &type, types, 3))
		{
			light.Type = static_cast<LightType>(type);
		}
		ImGui::ColorEdit3("Color", glm::value_ptr(light.Color));
		ImGui::DragFloat("Intensity", &light.Intensity, 0.05f, 0.0f, 1000.0f);
		if (light.Type != LightType::Directional)
		{
			ImGui::DragFloat("Range", &light.Range, 0.1f, 0.0f, 10000.0f);
		}
		if (light.Type == LightType::Spot)
		{
			ImGui::DragFloat("Inner Cone", &light.InnerConeDegrees, 0.25f, 0.0f, 89.0f);
			ImGui::DragFloat("Outer Cone", &light.OuterConeDegrees, 0.25f, 0.0f, 89.0f);
			if (light.OuterConeDegrees < light.InnerConeDegrees)
			{
				light.OuterConeDegrees = light.InnerConeDegrees;
			}
		}
		ImGui::Checkbox("Cast Shadows", &light.CastShadows);
		if (ImGui::Button("Remove Light"))
		{
			entity.RemoveComponent<LightComponent>();
		}
	}

	void EditorLayer::drawMeshRendererComponent(Entity entity)
	{
		if (!ImGui::CollapsingHeader("Mesh Renderer", ImGuiTreeNodeFlags_DefaultOpen))
		{
			return;
		}
		MeshRendererComponent& renderer = entity.GetComponent<MeshRendererComponent>();
		ImGui::Text("Mesh: %s", renderer.Mesh.IsValid() ? renderer.Mesh.Id.ToString().c_str() : "(none)");
		ImGui::Checkbox("Visible", &renderer.Visible);
		ImGui::SameLine();
		ImGui::Checkbox("Cast Shadows", &renderer.CastShadows);
		ImGui::Checkbox("Receive Shadows", &renderer.ReceiveShadows);
		if (!renderer.Materials.empty())
		{
			ImGui::Text("Materials: %zu", renderer.Materials.size());
		}
		if (ImGui::Button("Remove Mesh Renderer"))
		{
			entity.RemoveComponent<MeshRendererComponent>();
		}
	}

	void EditorLayer::drawPhysicsComponents(Entity entity)
	{
		if (entity.HasComponent<RigidBodyComponent>())
		{
			if (ImGui::CollapsingHeader("Rigid Body", ImGuiTreeNodeFlags_DefaultOpen))
			{
				RigidBodyComponent& body = entity.GetComponent<RigidBodyComponent>();
				const char* types[] = { "Static", "Kinematic", "Dynamic" };
				int type = static_cast<int>(body.Type);
				if (ImGui::Combo("Body Type", &type, types, 3))
				{
					body.Type = static_cast<BodyType>(type);
				}
				ImGui::DragFloat("Mass", &body.Mass, 0.05f, 0.01f, 10000.0f);
				ImGui::DragFloat("Friction", &body.Friction, 0.01f, 0.0f, 1.0f);
				ImGui::DragFloat("Restitution", &body.Restitution, 0.01f, 0.0f, 1.0f);
				ImGui::Checkbox("Use Gravity", &body.UseGravity);
				if (ImGui::Button("Remove Rigid Body"))
				{
					entity.RemoveComponent<RigidBodyComponent>();
				}
			}
		}
		if (entity.HasComponent<ColliderComponent>())
		{
			if (ImGui::CollapsingHeader("Collider", ImGuiTreeNodeFlags_DefaultOpen))
			{
				ColliderComponent& collider = entity.GetComponent<ColliderComponent>();
				const char* shapes[] = { "Box", "Sphere", "Capsule", "Mesh" };
				int shape = static_cast<int>(collider.Shape);
				if (ImGui::Combo("Shape", &shape, shapes, 4))
				{
					collider.Shape = static_cast<ColliderShape>(shape);
				}
				switch (collider.Shape)
				{
					case ColliderShape::Box: ImGui::DragFloat3("Half Extents", glm::value_ptr(collider.HalfExtents), 0.01f, 0.001f, 1000.0f); break;
					case ColliderShape::Sphere: ImGui::DragFloat("Radius", &collider.Radius, 0.01f, 0.001f, 1000.0f); break;
					case ColliderShape::Capsule:
						ImGui::DragFloat("Radius", &collider.Radius, 0.01f, 0.001f, 1000.0f);
						ImGui::DragFloat("Half Height", &collider.HalfHeight, 0.01f, 0.001f, 1000.0f);
						break;
					case ColliderShape::Mesh: ImGui::TextDisabled("Tri-mesh colliders use the mesh asset"); break;
				}
				ImGui::DragFloat3("Offset", glm::value_ptr(collider.Offset), 0.01f);
				ImGui::Checkbox("Is Trigger", &collider.IsTrigger);
				if (ImGui::Button("Remove Collider"))
				{
					entity.RemoveComponent<ColliderComponent>();
				}
			}
		}
	}

	void EditorLayer::drawAudioComponents(Entity entity)
	{
		if (entity.HasComponent<AudioSourceComponent>())
		{
			if (ImGui::CollapsingHeader("Audio Source", ImGuiTreeNodeFlags_DefaultOpen))
			{
				AudioSourceComponent& source = entity.GetComponent<AudioSourceComponent>();
				ImGui::Text("Clip: %s", source.Clip.IsValid() ? source.Clip.Id.ToString().c_str() : "(none)");
				ImGui::DragFloat("Volume", &source.Volume, 0.01f, 0.0f, 4.0f);
				ImGui::DragFloat("Pitch", &source.Pitch, 0.01f, 0.01f, 4.0f);
				ImGui::Checkbox("Loop", &source.Loop);
				ImGui::SameLine();
				ImGui::Checkbox("Play On Start", &source.PlayOnStart);
				ImGui::Checkbox("Spatial", &source.Spatial);
				if (source.Spatial)
				{
					ImGui::DragFloat("Min Distance", &source.MinDistance, 0.05f, 0.0f, 1000.0f);
					ImGui::DragFloat("Max Distance", &source.MaxDistance, 0.5f, 0.0f, 10000.0f);
				}
				if (ImGui::Button("Remove Audio Source"))
				{
					entity.RemoveComponent<AudioSourceComponent>();
				}
			}
		}
		if (entity.HasComponent<AudioListenerComponent>())
		{
			if (ImGui::CollapsingHeader("Audio Listener", ImGuiTreeNodeFlags_DefaultOpen))
			{
				AudioListenerComponent& listener = entity.GetComponent<AudioListenerComponent>();
				ImGui::Checkbox("Primary", &listener.Primary);
				if (ImGui::Button("Remove Audio Listener"))
				{
					entity.RemoveComponent<AudioListenerComponent>();
				}
			}
		}
	}

	void EditorLayer::drawAddComponentPopup(Entity entity)
	{
		if (!ImGui::BeginPopup("AddComponent"))
		{
			return;
		}

		if (!entity.HasComponent<CameraComponent>() && ImGui::MenuItem("Camera"))
		{
			entity.AddComponent<CameraComponent>(CameraComponent{});
		}
		if (!entity.HasComponent<LightComponent>() && ImGui::MenuItem("Light"))
		{
			entity.AddComponent<LightComponent>(LightComponent{});
		}
		if (!entity.HasComponent<MeshRendererComponent>() && ImGui::MenuItem("Mesh Renderer"))
		{
			entity.AddComponent<MeshRendererComponent>(MeshRendererComponent{});
		}
		if (!entity.HasComponent<RigidBodyComponent>() && ImGui::MenuItem("Rigid Body"))
		{
			entity.AddComponent<RigidBodyComponent>(RigidBodyComponent{});
		}
		if (!entity.HasComponent<ColliderComponent>() && ImGui::MenuItem("Collider"))
		{
			entity.AddComponent<ColliderComponent>(ColliderComponent{});
		}
		if (!entity.HasComponent<AudioSourceComponent>() && ImGui::MenuItem("Audio Source"))
		{
			entity.AddComponent<AudioSourceComponent>(AudioSourceComponent{});
		}
		if (!entity.HasComponent<AudioListenerComponent>() && ImGui::MenuItem("Audio Listener"))
		{
			entity.AddComponent<AudioListenerComponent>(AudioListenerComponent{});
		}
		if (!entity.HasComponent<LuaScriptComponent>() && ImGui::MenuItem("Lua Script"))
		{
			entity.AddComponent<LuaScriptComponent>(LuaScriptComponent{});
		}
		if (!entity.HasComponent<FlyCameraComponent>() && ImGui::MenuItem("Fly Camera"))
		{
			entity.AddComponent<FlyCameraComponent>(FlyCameraComponent{});
		}
		ImGui::EndPopup();
	}

	glm::mat4 EditorLayer::computeViewProjection(f32 aspectRatio, glm::vec3& outCameraPosition) const
	{
		outCameraPosition = m_EditorCamera.GetPosition();
		if (m_UseSceneCamera && m_Scene != nullptr)
		{
			const Entity primaryCamera = m_Scene->GetPrimaryCamera();
			if (primaryCamera.IsValid() && primaryCamera.HasComponent<CameraComponent>() &&
				primaryCamera.HasComponent<TransformComponent>())
			{
				const CameraComponent& cameraComponent = primaryCamera.GetComponent<CameraComponent>();
				const TransformComponent& cameraTransform = primaryCamera.GetComponent<TransformComponent>();
				outCameraPosition = cameraTransform.WorldPosition;
				return Camera::ComputeProjection(cameraComponent, aspectRatio) * Camera::ComputeView(cameraTransform);
			}
		}
		return m_EditorCamera.GetViewProjection(aspectRatio);
	}

	void EditorLayer::renderScene(nvrhi::ICommandList* commandList)
	{
		if (m_SceneRenderer == nullptr || !m_SceneRenderer->IsInitialized() || m_Scene == nullptr ||
			m_ImGui == nullptr || commandList == nullptr)
		{
			return;
		}

		const u32 width = m_SceneRenderer->GetWidth();
		const u32 height = m_SceneRenderer->GetHeight();
		if (width != m_RegisteredViewportWidth || height != m_RegisteredViewportHeight)
		{
			// Resizing recreates the output texture, so ImGui needs a fresh binding set.
			m_ImGui->RegisterTexture(static_cast<ImTextureID>(m_ViewportTextureId),
				m_SceneRenderer->GetFinalColor());
			m_RegisteredViewportWidth = width;
			m_RegisteredViewportHeight = height;
		}

		const f32 aspect = static_cast<f32>(width) / static_cast<f32>(height);
		glm::vec3 cameraPosition = m_EditorCamera.GetPosition();
		const glm::mat4 viewProjection = computeViewProjection(aspect, cameraPosition);

		const EnvironmentSettings& environment = m_Scene->GetEnvironment();
		const glm::mat4 lightViewProjection = SceneRenderer::ComputeDirectionalShadowMatrix(*m_Scene,
			cameraPosition, environment.ShadowDistance);
		m_SceneRenderer->Render(commandList, *m_Scene, viewProjection, cameraPosition, lightViewProjection,
			environment.EnableShadows);
	}

	void EditorLayer::drawViewport()
	{
		ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));
		const bool open = ImGui::Begin(c_ViewportName, nullptr, ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);
		ImGui::PopStyleVar();
		if (!open)
		{
			ImGui::End();
			return;
		}

		const ImVec2 available = ImGui::GetContentRegionAvail();
		if (available.x > 0.0f && available.y > 0.0f)
		{
			m_ViewportSize = glm::vec2(available.x, available.y);
		}
		m_ViewportHovered = ImGui::IsWindowHovered();
		m_ViewportFocused = ImGui::IsWindowFocused();

		const ImVec2 origin = ImGui::GetCursorScreenPos();
		const ImVec2 size(available.x, available.y);
		const u32 targetWidth = static_cast<u32>(Max(available.x, 1.0f));
		const u32 targetHeight = static_cast<u32>(Max(available.y, 1.0f));
		// Integer target dimensions drive the aspect ratio so the grid and gizmos line
		// up with the rendered pixels exactly.
		const f32 aspect = static_cast<f32>(targetWidth) / static_cast<f32>(targetHeight);

		ImDrawList* drawList = ImGui::GetWindowDrawList();
		drawList->AddRectFilled(origin, ImVec2(origin.x + size.x, origin.y + size.y), IM_COL32(12, 14, 19, 255));

		const bool hasSceneRenderer = m_SceneRenderer != nullptr && m_SceneRenderer->IsInitialized();
		if (hasSceneRenderer && size.x > 1.0f && size.y > 1.0f)
		{
			// Size the offscreen targets to the viewport before recording the frame.
			m_SceneRenderer->Resize(targetWidth, targetHeight);
			ImGui::Image(static_cast<ImTextureID>(m_ViewportTextureId), size);
		}

		const Entity primaryCamera = m_Scene != nullptr ? m_Scene->GetPrimaryCamera() : Entity();
		const bool hasSceneCamera = primaryCamera.IsValid() && primaryCamera.HasComponent<CameraComponent>() &&
			primaryCamera.HasComponent<TransformComponent>();
		glm::vec3 cameraPosition = m_EditorCamera.GetPosition();
		const glm::mat4 viewProjection = computeViewProjection(aspect, cameraPosition);

		if (size.x > 1.0f && size.y > 1.0f)
		{
			drawViewportGrid(viewProjection, glm::vec2(origin.x, origin.y), glm::vec2(size.x, size.y));
			drawViewportGizmos(viewProjection, glm::vec2(origin.x, origin.y), glm::vec2(size.x, size.y));
			drawTranslationGizmo(viewProjection, cameraPosition, glm::vec2(origin.x, origin.y),
				glm::vec2(size.x, size.y));

			char overlay[256];
			std::snprintf(overlay, sizeof(overlay), "%s | %.0f fps | pos %.1f %.1f %.1f",
				m_UseSceneCamera && hasSceneCamera ? "Scene camera" : "Editor camera",
				Application::Get() != nullptr ? Application::Get()->GetFramesPerSecond() : 0.0f,
				cameraPosition.x, cameraPosition.y, cameraPosition.z);
			const f32 textWidth = ImGui::CalcTextSize(overlay).x;
			drawList->AddText(ImVec2(origin.x + Max(size.x - textWidth - 12.0f, 8.0f), origin.y + 10.0f),
				IM_COL32(200, 205, 212, 220), overlay);
		}

		// Toolbar overlay: pick which camera drives the viewport.
		ImGui::SetCursorScreenPos(ImVec2(origin.x + 8.0f, origin.y + 8.0f));
		ImGui::PushStyleColor(ImGuiCol_ChildBg, IM_COL32(14, 16, 21, 205));
		ImGui::PushStyleVar(ImGuiStyleVar_ChildRounding, 4.0f);
		ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(8.0f, 5.0f));
		const bool toolbarOpen = ImGui::BeginChild("##ViewportToolbar", ImVec2(0.0f, 0.0f),
			ImGuiChildFlags_AutoResizeX | ImGuiChildFlags_AutoResizeY,
			ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoSavedSettings);
		if (toolbarOpen)
		{
			if (ImGui::RadioButton("Editor Camera", !m_UseSceneCamera || !hasSceneCamera))
			{
				m_UseSceneCamera = false;
			}
			ImGui::SameLine();
			ImGui::BeginDisabled(!hasSceneCamera);
			if (ImGui::RadioButton("Scene Camera", m_UseSceneCamera && hasSceneCamera))
			{
				m_UseSceneCamera = true;
			}
			ImGui::EndDisabled();
			ImGui::SameLine();
			ImGui::Checkbox("Gizmo", &m_GizmoEnabled);
		}
		ImGui::EndChild();
		ImGui::PopStyleVar(2);
		ImGui::PopStyleColor();

		// Clicking in the viewport selects the closest entity under the cursor.
		if (m_ViewportHovered && ImGui::IsMouseClicked(ImGuiMouseButton_Left) && size.x > 1.0f &&
			!ImGui::IsAnyItemHovered() && !m_GizmoConsumedClick)
		{
			const ImVec2 mouse = ImGui::GetIO().MousePos;
			const glm::vec2 viewportPosition((mouse.x - origin.x) / size.x, (mouse.y - origin.y) / size.y);

			glm::vec3 rayOrigin = cameraPosition;
			glm::vec3 rayDirection = m_EditorCamera.GetForward();
			if (m_UseSceneCamera && hasSceneCamera)
			{
				const CameraComponent& cameraComponent = primaryCamera.GetComponent<CameraComponent>();
				const TransformComponent& cameraTransform = primaryCamera.GetComponent<TransformComponent>();
				Camera::ComputeRay(cameraTransform, cameraComponent, aspect, viewportPosition, rayOrigin,
					rayDirection);
			}
			else
			{
				const f32 ndcX = viewportPosition.x * 2.0f - 1.0f;
				const f32 ndcY = 1.0f - viewportPosition.y * 2.0f;
				const f32 tanHalfFov = std::tan(glm::radians(m_EditorCamera.GetFieldOfView()) * 0.5f);
				rayDirection = glm::normalize(m_EditorCamera.GetForward() +
					m_EditorCamera.GetRight() * (ndcX * tanHalfFov * aspect) +
					m_EditorCamera.GetUp() * (ndcY * tanHalfFov));
			}

			Uuid closest;
			f32 closestDistance = 1.0e30f;
			for (Entity entity : m_Scene->GetEntities())
			{
				if (!entity.HasComponent<TransformComponent>())
				{
					continue;
				}
				const TransformComponent& transform = entity.GetComponent<TransformComponent>();
				glm::vec3 halfExtents(0.5f);
				if (const ColliderComponent* collider = entity.TryGetComponent<ColliderComponent>())
				{
					if (collider->Shape == ColliderShape::Box)
					{
						halfExtents = collider->HalfExtents;
					}
					else
					{
						halfExtents = glm::vec3(collider->Radius);
					}
				}
				const glm::mat4 world = transform.GetWorldTransform();
				const glm::mat4 worldToLocal = glm::inverse(world);
				f32 distance = 0.0f;
				if (RayIntersectsAabb(rayOrigin, rayDirection, worldToLocal, halfExtents, distance) &&
					distance < closestDistance)
				{
					closestDistance = distance;
					closest = entity.GetUuid();
				}
			}
			m_SelectedEntity = closest;
		}

		// Reserve the whole area so the viewport behaves like a real child window.
		ImGui::SetCursorScreenPos(origin);
		ImGui::Dummy(ImVec2(size.x, size.y));
		ImGui::End();
	}

	void EditorLayer::drawViewportGrid(const glm::mat4& viewProjection, const glm::vec2& origin, const glm::vec2& size)
	{
		ImDrawList* drawList = ImGui::GetWindowDrawList();
		const ImVec2 originVec(origin.x, origin.y);
		const ImVec2 sizeVec(size.x, size.y);
		constexpr int gridExtent = 20;
		constexpr f32 gridStep = 1.0f;

		for (int i = -gridExtent; i <= gridExtent; ++i)
		{
			const f32 offset = static_cast<f32>(i) * gridStep;
			const bool axis = i == 0;
			const ImU32 color = axis ? IM_COL32(90, 90, 100, 180) : IM_COL32(46, 49, 58, 160);
			DrawLine3D(drawList, viewProjection, originVec, sizeVec, glm::vec3(offset, 0.0f, -gridExtent),
				glm::vec3(offset, 0.0f, gridExtent), color);
			DrawLine3D(drawList, viewProjection, originVec, sizeVec, glm::vec3(-gridExtent, 0.0f, offset),
				glm::vec3(gridExtent, 0.0f, offset), color);
		}

		DrawLine3D(drawList, viewProjection, originVec, sizeVec, glm::vec3(-gridExtent, 0.0f, 0.0f),
			glm::vec3(gridExtent, 0.0f, 0.0f), IM_COL32(180, 70, 70, 220), 1.5f);
		DrawLine3D(drawList, viewProjection, originVec, sizeVec, glm::vec3(0.0f, 0.0f, -gridExtent),
			glm::vec3(0.0f, 0.0f, gridExtent), IM_COL32(70, 100, 200, 220), 1.5f);
	}

	void EditorLayer::drawViewportGizmos(const glm::mat4& viewProjection, const glm::vec2& origin, const glm::vec2& size)
	{
		const ImVec2 originVec(origin.x, origin.y);
		const ImVec2 sizeVec(size.x, size.y);
		ImDrawList* drawList = ImGui::GetWindowDrawList();

		for (Entity entity : m_Scene->GetEntities())
		{
			if (!entity.HasComponent<TransformComponent>())
			{
				continue;
			}
			const TransformComponent& transform = entity.GetComponent<TransformComponent>();
			const glm::mat4 world = transform.GetWorldTransform();
			const bool selected = entity.GetUuid() == m_SelectedEntity;
			const ImU32 color = selected ? IM_COL32(255, 165, 60, 255) : IM_COL32(140, 150, 170, 190);

			if (entity.HasComponent<CameraComponent>())
			{
				const CameraComponent& camera = entity.GetComponent<CameraComponent>();
				const f32 near = std::max(camera.NearClip, 0.1f);
				const f32 far = near * 4.0f;
				const f32 halfHeight = std::tan(glm::radians(camera.FieldOfView) * 0.5f);
				const f32 halfWidth = halfHeight * 1.6f;
				const glm::vec3 corners[4] = {
					glm::vec3(world * glm::vec4(-halfWidth * far, -halfHeight * far, -far, 1.0f)),
					glm::vec3(world * glm::vec4(halfWidth * far, -halfHeight * far, -far, 1.0f)),
					glm::vec3(world * glm::vec4(halfWidth * far, halfHeight * far, -far, 1.0f)),
					glm::vec3(world * glm::vec4(-halfWidth * far, halfHeight * far, -far, 1.0f)),
				};
				const glm::vec3 apex = transform.WorldPosition;
				for (int i = 0; i < 4; ++i)
				{
					DrawLine3D(drawList, viewProjection, originVec, sizeVec, apex, corners[i], color);
					DrawLine3D(drawList, viewProjection, originVec, sizeVec, corners[i], corners[(i + 1) % 4], color);
				}
			}
			else if (entity.HasComponent<LightComponent>())
			{
				const LightComponent& light = entity.GetComponent<LightComponent>();
				const ImU32 lightColor = selected ? IM_COL32(255, 190, 70, 255)
												  : IM_COL32(230, 200, 110, 200);
				const glm::vec3 position = transform.WorldPosition;
				const f32 radius = light.Type == LightType::Directional ? 0.6f : std::max(0.3f, light.Range * 0.15f);
				const glm::vec3 a = glm::vec3(world * glm::vec4(radius, 0.0f, 0.0f, 1.0f));
				const glm::vec3 b = glm::vec3(world * glm::vec4(-radius, 0.0f, 0.0f, 1.0f));
				const glm::vec3 c = glm::vec3(world * glm::vec4(0.0f, radius, 0.0f, 1.0f));
				const glm::vec3 d = glm::vec3(world * glm::vec4(0.0f, -radius, 0.0f, 1.0f));
				DrawLine3D(drawList, viewProjection, originVec, sizeVec, a, b, lightColor);
				DrawLine3D(drawList, viewProjection, originVec, sizeVec, c, d, lightColor);
				if (light.Type == LightType::Directional)
				{
					const glm::vec3 direction = glm::normalize(glm::vec3(world * glm::vec4(0.0f, 0.0f, -1.0f, 0.0f)));
					DrawLine3D(drawList, viewProjection, originVec, sizeVec, position,
						position + direction * 3.0f, lightColor);
				}
			}
			else if (entity.HasComponent<MeshRendererComponent>() || entity.HasComponent<ColliderComponent>())
			{
				glm::vec3 halfExtents(0.5f);
				if (const ColliderComponent* collider = entity.TryGetComponent<ColliderComponent>())
				{
					if (collider->Shape == ColliderShape::Box)
					{
						halfExtents = collider->HalfExtents;
					}
					else
					{
						halfExtents = glm::vec3(collider->Radius);
					}
				}

				const glm::vec3 corners[8] = {
					glm::vec3(world * glm::vec4(-halfExtents.x, -halfExtents.y, -halfExtents.z, 1.0f)),
					glm::vec3(world * glm::vec4(halfExtents.x, -halfExtents.y, -halfExtents.z, 1.0f)),
					glm::vec3(world * glm::vec4(halfExtents.x, -halfExtents.y, halfExtents.z, 1.0f)),
					glm::vec3(world * glm::vec4(-halfExtents.x, -halfExtents.y, halfExtents.z, 1.0f)),
					glm::vec3(world * glm::vec4(-halfExtents.x, halfExtents.y, -halfExtents.z, 1.0f)),
					glm::vec3(world * glm::vec4(halfExtents.x, halfExtents.y, -halfExtents.z, 1.0f)),
					glm::vec3(world * glm::vec4(halfExtents.x, halfExtents.y, halfExtents.z, 1.0f)),
					glm::vec3(world * glm::vec4(-halfExtents.x, halfExtents.y, halfExtents.z, 1.0f)),
				};
				const int edges[12][2] = { { 0, 1 }, { 1, 2 }, { 2, 3 }, { 3, 0 }, { 4, 5 }, { 5, 6 },
					{ 6, 7 }, { 7, 4 }, { 0, 4 }, { 1, 5 }, { 2, 6 }, { 3, 7 } };
				for (const auto& edge : edges)
				{
					DrawLine3D(drawList, viewProjection, originVec, sizeVec, corners[edge[0]], corners[edge[1]], color);
				}
			}
		}
	}


	void EditorLayer::drawTranslationGizmo(const glm::mat4& viewProjection, const glm::vec3& cameraPosition,
		const glm::vec2& origin, const glm::vec2& size)
	{
		m_GizmoConsumedClick = false;
		m_HoveredGizmoAxis = -1;
		m_GizmoHoveredCenter = false;

		if (!m_GizmoEnabled || m_Scene == nullptr || m_SelectedEntity.IsNull())
		{
			m_GizmoDragging = false;
			m_GizmoDragAxis = -1;
			return;
		}

		Entity entity = m_Scene->FindEntity(m_SelectedEntity);
		if (!entity.IsValid() || !entity.HasComponent<TransformComponent>())
		{
			m_GizmoDragging = false;
			m_GizmoDragAxis = -1;
			return;
		}
		TransformComponent& transform = entity.GetComponent<TransformComponent>();
		const glm::vec3 worldPosition = glm::vec3(transform.GetWorldTransform()[3]);
		const TranslationGizmo gizmo = TranslationGizmo::Build(viewProjection, worldPosition, cameraPosition, size);

		const auto rayFromMouse = [&](const glm::vec2& point, glm::vec3& outOrigin, glm::vec3& outDirection)
		{
			const glm::mat4 inverseViewProjection = glm::inverse(viewProjection);
			const glm::vec2 ndc(point.x / size.x * 2.0f - 1.0f, 1.0f - point.y / size.y * 2.0f);
			glm::vec4 nearPoint = inverseViewProjection * glm::vec4(ndc, 0.0f, 1.0f);
			glm::vec4 farPoint = inverseViewProjection * glm::vec4(ndc, 1.0f, 1.0f);
			nearPoint /= nearPoint.w;
			farPoint /= farPoint.w;
			outOrigin = glm::vec3(nearPoint);
			outDirection = glm::normalize(glm::vec3(farPoint) - glm::vec3(nearPoint));
		};

		const ImVec2 mousePosition = ImGui::GetIO().MousePos;
		const glm::vec2 mouse(mousePosition.x - origin.x, mousePosition.y - origin.y);
		const bool mouseInside = mouse.x >= 0.0f && mouse.y >= 0.0f && mouse.x <= size.x && mouse.y <= size.y;
		const bool canInteract = mouseInside && m_ViewportHovered && !ImGui::IsAnyItemHovered();

		const i32 hoveredAxis = gizmo.HitTestAxis(mouse);
		const bool hoveredCentre = gizmo.HitTestCenter(mouse);
		if (canInteract && !m_GizmoDragging)
		{
			m_HoveredGizmoAxis = hoveredAxis;
			m_GizmoHoveredCenter = hoveredAxis < 0 && hoveredCentre;
		}

		// Start a drag on a handle click. The click is consumed so the entity picker
		// underneath does not change the selection.
		if (canInteract && !m_GizmoDragging && ImGui::IsMouseClicked(ImGuiMouseButton_Left) &&
			(hoveredAxis >= 0 || hoveredCentre))
		{
			glm::vec3 rayOrigin(0.0f);
			glm::vec3 rayDirection(0.0f, 0.0f, -1.0f);
			rayFromMouse(mouse, rayOrigin, rayDirection);

			glm::vec3 planeNormal = worldPosition - cameraPosition;
			if (glm::dot(planeNormal, planeNormal) < 1.0e-6f)
			{
				planeNormal = -rayDirection;
			}
			planeNormal = glm::normalize(planeNormal);
			const std::optional<glm::vec3> grab = TranslationGizmo::IntersectPlane(rayOrigin, rayDirection,
				worldPosition, planeNormal);
			if (grab.has_value())
			{
				m_GizmoDragging = true;
				m_GizmoConsumedClick = true;
				m_GizmoDragAxis = hoveredAxis;
				m_GizmoDragStartWorld = worldPosition;
				m_GizmoDragStartLocal = transform.LocalPosition;
				m_GizmoDragGrab = *grab;
				m_GizmoDragPlaneNormal = planeNormal;
				m_GizmoLastLocal = transform.LocalPosition;
			}
		}

		// Continue or finish the drag.
		if (m_GizmoDragging)
		{
			if (!ImGui::IsMouseDown(ImGuiMouseButton_Left))
			{
				m_GizmoDragging = false;
				m_GizmoDragAxis = -1;
			}
			else
			{
				glm::vec3 rayOrigin(0.0f);
				glm::vec3 rayDirection(0.0f, 0.0f, -1.0f);
				rayFromMouse(mouse, rayOrigin, rayDirection);
				const std::optional<glm::vec3> hit = TranslationGizmo::IntersectPlane(rayOrigin, rayDirection,
					m_GizmoDragStartWorld, m_GizmoDragPlaneNormal);
				if (hit.has_value())
				{
					const glm::vec3 delta = TranslationGizmo::Constrain(*hit - m_GizmoDragGrab, m_GizmoDragAxis);
					glm::vec3 localDelta = delta;
					if (!transform.Parent.IsNull())
					{
						const Entity parent = m_Scene->FindEntity(transform.Parent);
						if (parent.IsValid() && parent.HasComponent<TransformComponent>())
						{
							const glm::mat4 parentWorld = parent.GetComponent<TransformComponent>().GetWorldTransform();
							localDelta = glm::vec3(glm::inverse(parentWorld) * glm::vec4(delta, 0.0f));
						}
					}
					const glm::vec3 newLocal = m_GizmoDragStartLocal + localDelta;
					if (glm::dot(newLocal - m_GizmoLastLocal, newLocal - m_GizmoLastLocal) > 1.0e-10f)
					{
						m_GizmoLastLocal = newLocal;
						JsonValue parameters = JsonValue::MakeObject();
						parameters.Set("uuid", entity.GetUuid().ToString());
						JsonValue position = JsonValue::MakeArray();
						position.Push(static_cast<f64>(newLocal.x));
						position.Push(static_cast<f64>(newLocal.y));
						position.Push(static_cast<f64>(newLocal.z));
						parameters.Set("position", std::move(position));
						const CommandResult result = CommandBus::Get().Execute("transform.set", parameters);
						if (!result.Success)
						{
							AE_CORE_WARN("Gizmo drag could not move '{}': {}", entity.GetName(), result.Error);
						}
					}
				}
			}
		}

		// Draw the handles.
		static const ImU32 c_AxisColors[3] = { IM_COL32(226, 74, 74, 255), IM_COL32(106, 196, 86, 255),
			IM_COL32(74, 132, 230, 255) };
		static const char* c_AxisLabels[3] = { "X", "Y", "Z" };
		const ImU32 highlightColor = IM_COL32(255, 214, 86, 255);
		ImDrawList* drawList = ImGui::GetWindowDrawList();

		for (i32 index = 0; index < 3; ++index)
		{
			const ScreenGizmoAxis& axis = gizmo.GetAxis(static_cast<usize>(index));
			if (!axis.Visible)
			{
				continue;
			}
			const bool active = m_GizmoDragging && m_GizmoDragAxis == index;
			const bool hovered = !m_GizmoDragging && m_HoveredGizmoAxis == index;
			const ImU32 color = (active || hovered) ? highlightColor : c_AxisColors[index];
			const ImVec2 start(origin.x + axis.ScreenOrigin.x, origin.y + axis.ScreenOrigin.y);
			const ImVec2 end(origin.x + axis.ScreenEnd.x, origin.y + axis.ScreenEnd.y);

			drawList->AddLine(start, end, color, (active || hovered) ? 3.5f : 2.5f);

			const glm::vec2 direction = axis.ScreenEnd - axis.ScreenOrigin;
			const f32 length = glm::length(direction);
			if (length > 1.0f)
			{
				const glm::vec2 unit = direction / length;
				const glm::vec2 perpendicular(-unit.y, unit.x);
				const f32 arrowLength = 11.0f;
				const f32 arrowWidth = 5.5f;
				drawList->AddTriangleFilled(end,
					ImVec2(end.x - unit.x * arrowLength + perpendicular.x * arrowWidth,
						end.y - unit.y * arrowLength + perpendicular.y * arrowWidth),
					ImVec2(end.x - unit.x * arrowLength - perpendicular.x * arrowWidth,
						end.y - unit.y * arrowLength - perpendicular.y * arrowWidth),
					color);
				drawList->AddText(ImVec2(end.x + 5.0f, end.y - 7.0f), color, c_AxisLabels[index]);
			}
		}

		const ImVec2 centre(origin.x + gizmo.GetScreenOrigin().x, origin.y + gizmo.GetScreenOrigin().y);
		const bool centreActive = m_GizmoDragAxis < 0 && m_GizmoDragging;
		drawList->AddRectFilled(ImVec2(centre.x - 5.0f, centre.y - 5.0f),
			ImVec2(centre.x + 5.0f, centre.y + 5.0f),
			(centreActive || m_GizmoHoveredCenter) ? highlightColor : IM_COL32(220, 224, 230, 200));
	}

	void EditorLayer::drawConsole()
	{
		if (!ImGui::Begin(c_ConsoleName))
		{
			ImGui::End();
			return;
		}

		if (ImGui::Button("Clear"))
		{
			Log::ClearRecentMessages();
		}
		ImGui::SameLine();
		ImGui::Checkbox("Trace", &m_ConsoleShowTrace);
		ImGui::SameLine();
		ImGui::Checkbox("Debug", &m_ConsoleShowDebug);
		ImGui::SameLine();
		ImGui::Checkbox("Info", &m_ConsoleShowInfo);
		ImGui::SameLine();
		ImGui::Checkbox("Warn", &m_ConsoleShowWarn);
		ImGui::SameLine();
		ImGui::Checkbox("Error", &m_ConsoleShowError);
		ImGui::SameLine();
		ImGui::Checkbox("Auto Scroll", &m_ConsoleAutoScroll);
		ImGui::SameLine();
		ImGui::SetNextItemWidth(180.0f);
		char filterBuffer[128];
		std::snprintf(filterBuffer, sizeof(filterBuffer), "%s", m_ConsoleFilter.c_str());
		if (ImGui::InputTextWithHint("##filter", "Filter...", filterBuffer, sizeof(filterBuffer)))
		{
			m_ConsoleFilter = filterBuffer;
		}

		ImGui::Separator();

		const std::vector<LogMessage> messages = Log::GetRecentMessages();
		if (ImGui::BeginChild("##console-scroll", ImVec2(0.0f, 0.0f), false, ImGuiWindowFlags_HorizontalScrollbar))
		{
			for (const LogMessage& message : messages)
			{
				if (message.Level == LogLevel::Trace && !m_ConsoleShowTrace) { continue; }
				if (message.Level == LogLevel::Debug && !m_ConsoleShowDebug) { continue; }
				if (message.Level == LogLevel::Info && !m_ConsoleShowInfo) { continue; }
				if (message.Level == LogLevel::Warn && !m_ConsoleShowWarn) { continue; }
				if ((message.Level == LogLevel::Error || message.Level == LogLevel::Critical) && !m_ConsoleShowError)
				{
					continue;
				}
				if (!m_ConsoleFilter.empty() && message.Message.find(m_ConsoleFilter) == std::string::npos &&
					message.Logger.find(m_ConsoleFilter) == std::string::npos)
				{
					continue;
				}

				ImVec4 color(0.85f, 0.85f, 0.85f, 1.0f);
				if (message.Level == LogLevel::Warn) { color = ImVec4(0.95f, 0.78f, 0.35f, 1.0f); }
				if (message.Level == LogLevel::Error) { color = ImVec4(0.95f, 0.42f, 0.38f, 1.0f); }
				if (message.Level == LogLevel::Critical) { color = ImVec4(1.0f, 0.25f, 0.25f, 1.0f); }
				if (message.Level == LogLevel::Trace) { color = ImVec4(0.55f, 0.55f, 0.55f, 1.0f); }
				if (message.Level == LogLevel::Debug) { color = ImVec4(0.6f, 0.75f, 0.85f, 1.0f); }

				ImGui::TextColored(color, "[%s] %s", message.Timestamp.c_str(), message.Message.c_str());
			}
			if (m_ConsoleAutoScroll && ImGui::GetScrollY() >= ImGui::GetScrollMaxY() - 1.0f)
			{
				ImGui::SetScrollHereY(1.0f);
			}
		}
		ImGui::EndChild();
		ImGui::End();
	}

	void EditorLayer::drawStats()
	{
		if (!ImGui::Begin(c_StatsName))
		{
			ImGui::End();
			return;
		}

		Application* application = Application::Get();
		ImGui::Text("FPS: %.1f", application != nullptr ? application->GetFramesPerSecond() : 0.0f);
		ImGui::Text("Frame: %.2f ms", application != nullptr ? application->GetAverageFrameTime() * 1000.0f : 0.0f);
		ImGui::Separator();
		ImGui::Text("GPU: %s", m_Renderer != nullptr ? m_Renderer->GetGpuName().c_str() : "n/a");
		ImGui::Text("Resolution: %ux%u", m_Renderer != nullptr ? m_Renderer->GetWidth() : 0,
			m_Renderer != nullptr ? m_Renderer->GetHeight() : 0);
		ImGui::Text("Viewport: %.0fx%.0f", m_ViewportSize.x, m_ViewportSize.y);
		ImGui::Separator();
		ImGui::Text("Entities: %zu", m_Scene != nullptr ? m_Scene->GetEntityCount() : static_cast<usize>(0));
		ImGui::Text("Camera: %.2f %.2f %.2f", m_EditorCamera.GetPosition().x, m_EditorCamera.GetPosition().y,
			m_EditorCamera.GetPosition().z);
		ImGui::Text("Selected: %s", GetSelectedEntity().IsValid() ? GetSelectedEntity().GetName().c_str() : "none");
		ImGui::Separator();
		if (m_SceneRenderer != nullptr && m_SceneRenderer->IsInitialized())
		{
			const SceneRendererStats& stats = m_SceneRenderer->GetStats();
			ImGui::Text("Draw calls: %u (+%u shadow)", stats.DrawCalls, stats.ShadowDrawCalls);
			ImGui::Text("Visible meshes: %u", stats.VisibleMeshes);
			ImGui::Text("Triangles: %u", stats.Triangles);
			ImGui::Text("Point lights: %u / %u", stats.PointLights, c_MaxSceneLights);
			ImGui::Separator();
		}
		ImGui::Text("Warnings: %llu", static_cast<unsigned long long>(Log::GetMessageCount(LogLevel::Warn)));
		ImGui::Text("Errors: %llu", static_cast<unsigned long long>(Log::GetMessageCount(LogLevel::Error)));
		ImGui::End();
	}

	void EditorLayer::drawAssetBrowser()
	{
		if (!ImGui::Begin(c_AssetsName))
		{
			ImGui::End();
			return;
		}

		static std::string currentDirectory = m_AssetDirectory;
		if (currentDirectory.empty())
		{
			currentDirectory = m_AssetDirectory;
		}

		if (ImGui::Button("Assets"))
		{
			currentDirectory = m_AssetDirectory;
		}
		ImGui::SameLine();
		ImGui::TextDisabled("%s", currentDirectory.c_str());

		ImGui::Separator();

		const auto entries = FileSystem::ListDirectory(FileSystem::FromUtf8(currentDirectory), false, true);
		if (entries.empty())
		{
			ImGui::TextDisabled("The Assets folder is empty.");
		}

		for (const auto& entry : entries)
		{
			const std::string name = FileSystem::GetFileName(entry);
			if (FileSystem::IsDirectory(entry))
			{
				ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.75f, 0.8f, 0.95f, 1.0f));
				if (ImGui::Selectable((name + "/").c_str(), false, ImGuiSelectableFlags_AllowDoubleClick))
				{
					if (ImGui::IsMouseDoubleClicked(0))
					{
						currentDirectory = FileSystem::ToUtf8(entry);
					}
				}
				ImGui::PopStyleColor();
				continue;
			}

			const std::string extension = FileSystem::GetExtension(entry);
			ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.85f, 0.7f, 0.45f, 1.0f));
			ImGui::Selectable(name.c_str());
			ImGui::PopStyleColor();
			ImGui::SameLine(ImGui::GetContentRegionAvail().x - 80.0f);
			ImGui::TextDisabled("%s", extension.c_str());
		}

		ImGui::End();
	}

	void EditorLayer::handleShortcuts()
	{
		if (ImGui::GetCurrentContext() == nullptr || ImGui::GetIO().WantTextInput)
		{
			return;
		}

		Entity selected = GetSelectedEntity();
		const bool control = Input::IsControlDown();

		if (Input::IsKeyPressed(KeyCode::Delete) && selected.IsValid())
		{
			m_Scene->DestroyEntity(selected);
			m_SelectedEntity = Uuid::Null();
		}

		if (Input::IsKeyPressed(KeyCode::F) && selected.IsValid())
		{
			m_Scene->UpdateTransforms();
			if (const TransformComponent* transform = selected.TryGetComponent<TransformComponent>())
			{
				m_EditorCamera.Focus(transform->WorldPosition);
			}
		}

		if (control && Input::IsKeyPressed(KeyCode::S))
		{
			const std::string path = m_ScenePath.empty() ? "Saved/EditorScene.aescene" : m_ScenePath;
			std::string error;
			if (!saveSceneToFile(path, &error))
			{
				AE_CORE_ERROR("{}", error);
			}
		}
		if (control && Input::IsKeyPressed(KeyCode::N))
		{
			createDefaultScene();
		}
	}

	void EditorLayer::drawAutomationFooter()
	{
		if (m_Automation.Frames <= 0)
		{
			return;
		}
		ImGui::SetNextWindowPos(ImVec2(12.0f, ImGui::GetIO().DisplaySize.y - 70.0f));
		ImGui::SetNextWindowBgAlpha(0.75f);
		ImGui::Begin("Automation", nullptr, ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_AlwaysAutoResize |
												 ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoFocusOnAppearing |
												 ImGuiWindowFlags_NoNav);
		ImGui::Text("Self-test: %d frames remaining", m_RemainingFrames);
		if (!m_Automation.ScreenshotPath.empty())
		{
			ImGui::Text("Screenshot: %s", m_Automation.ScreenshotPath.c_str());
		}
		ImGui::End();
	}
}
