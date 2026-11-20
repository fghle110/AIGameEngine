#include "Editor/EditorLayer.h"

#include "Aegis.h"
#include "Aegis/Core/Application.h"
#include "Aegis/Core/Input.h"
#include "Aegis/Core/KeyCodes.h"
#include "Aegis/Events/ApplicationEvent.h"
#include "Aegis/Renderer/ImGuiRenderer.h"
#include "Aegis/Renderer/Renderer.h"
#include "Aegis/Scene/Entity.h"

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

		m_AssetDirectory = FileSystem::ToUtf8(FileSystem::GetCurrentWorkingDirectory() / "Assets");
		createDefaultScene();
		AE_CORE_INFO("Editor ready");
	}

	void EditorLayer::createDefaultScene()
	{
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

		Entity groundEntity = m_Scene->CreateEntity("Ground");
		MeshRendererComponent groundRenderer;
		groundRenderer.CastShadows = false;
		groundEntity.AddComponent<MeshRendererComponent>(groundRenderer);
		groundEntity.GetComponent<TransformComponent>().LocalScale = glm::vec3(20.0f, 0.1f, 20.0f);

		Entity boxEntity = m_Scene->CreateEntity("Box");
		boxEntity.AddComponent<MeshRendererComponent>(MeshRendererComponent{});
		boxEntity.GetComponent<TransformComponent>().LocalPosition = glm::vec3(0.0f, 1.0f, 0.0f);
		RigidBodyComponent rigidBody;
		rigidBody.Type = BodyType::Dynamic;
		boxEntity.AddComponent<RigidBodyComponent>(rigidBody);
		boxEntity.AddComponent<ColliderComponent>(ColliderComponent{});

		m_Scene->EnsurePrimaryCamera();
		m_Scene->UpdateTransforms();
	}

	void EditorLayer::OnDetach()
	{
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
		ImDrawList* drawList = ImGui::GetWindowDrawList();
		drawList->AddRectFilled(origin, ImVec2(origin.x + size.x, origin.y + size.y), IM_COL32(16, 18, 24, 255));

		if (size.x > 1.0f && size.y > 1.0f)
		{
			const f32 aspect = size.x / size.y;
			const glm::mat4 viewProjection = m_EditorCamera.GetViewProjection(aspect);
			drawViewportGrid(viewProjection, glm::vec2(origin.x, origin.y), glm::vec2(size.x, size.y));
			drawViewportGizmos(viewProjection, glm::vec2(origin.x, origin.y), glm::vec2(size.x, size.y));

			char overlay[256];
			std::snprintf(overlay, sizeof(overlay), "Perspective | %.0f fps | pos %.1f %.1f %.1f", 
				Application::Get() != nullptr ? Application::Get()->GetFramesPerSecond() : 0.0f,
				m_EditorCamera.GetPosition().x, m_EditorCamera.GetPosition().y, m_EditorCamera.GetPosition().z);
			drawList->AddText(ImVec2(origin.x + 10.0f, origin.y + 8.0f), IM_COL32(190, 195, 200, 220), overlay);
		}

		// Clicking in the viewport selects the closest entity under the cursor.
		if (m_ViewportHovered && ImGui::IsMouseClicked(ImGuiMouseButton_Left) && size.x > 1.0f)
		{
			const ImVec2 mouse = ImGui::GetIO().MousePos;
			const f32 ndcX = ((mouse.x - origin.x) / size.x) * 2.0f - 1.0f;
			const f32 ndcY = 1.0f - ((mouse.y - origin.y) / size.y) * 2.0f;
			const f32 aspect = size.x / size.y;
			const f32 tanHalfFov = std::tan(glm::radians(m_EditorCamera.GetFieldOfView()) * 0.5f);
			const glm::vec3 direction = glm::normalize(m_EditorCamera.GetForward() +
				m_EditorCamera.GetRight() * (ndcX * tanHalfFov * aspect) + m_EditorCamera.GetUp() * (ndcY * tanHalfFov));

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
				if (RayIntersectsAabb(m_EditorCamera.GetPosition(), direction, worldToLocal, halfExtents, distance) &&
					distance < closestDistance)
				{
					closestDistance = distance;
					closest = entity.GetUuid();
				}
			}
			m_SelectedEntity = closest;
		}

		// Reserve the whole area so the viewport behaves like a real child window.
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
			AE_CORE_INFO("Scene saving is wired through the command bus ({})", c_SceneExtension);
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
