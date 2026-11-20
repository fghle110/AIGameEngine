#pragma once

#include "Aegis/Core/Base.h"

#include <functional>
#include <string>

namespace aegis
{
	enum class EventType : u32
	{
		None = 0,
		WindowClose,
		WindowResize,
		WindowFocus,
		WindowLostFocus,
		WindowMoved,
		AppTick,
		AppUpdate,
		AppRender,
		KeyPressed,
		KeyReleased,
		KeyTyped,
		MouseButtonPressed,
		MouseButtonReleased,
		MouseMoved,
		MouseScrolled,
		SceneOpened,
		SceneClosed,
		SceneModified,
		ConsoleMessage
	};

	enum EventCategory : u32
	{
		None = 0,
		EventCategoryApplication = BIT(0),
		EventCategoryInput = BIT(1),
		EventCategoryKeyboard = BIT(2),
		EventCategoryMouse = BIT(3),
		EventCategoryMouseButton = BIT(4),
		EventCategoryEditor = BIT(5)
	};

	class Event
	{
	public:
		virtual ~Event() = default;

		bool Handled = false;

		virtual EventType GetEventType() const = 0;
		virtual const char* GetName() const = 0;
		virtual u32 GetCategoryFlags() const = 0;
		virtual std::string ToString() const { return GetName(); }

		bool IsInCategory(EventCategory category) const
		{
			return (GetCategoryFlags() & static_cast<u32>(category)) != 0;
		}
	};

	class EventDispatcher
	{
	public:
		EventDispatcher(Event& event)
			: m_Event(event)
		{
		}

		// Returns true when the event matched the handler's type. The handler can
		// still choose to not mark the event handled.
		template<typename T, typename F>
		bool Dispatch(const F& func)
		{
			if (m_Event.GetEventType() != T::GetStaticType())
			{
				return false;
			}
			m_Event.Handled = func(static_cast<T&>(m_Event));
			return true;
		}

	private:
		Event& m_Event;
	};

	inline std::string FormatEvent(const Event& event)
	{
		return event.ToString();
	}
}

#define AE_EVENT_CLASS_TYPE(type)                                                                                    \
	static ::aegis::EventType GetStaticType() { return ::aegis::EventType::type; }                                  \
	::aegis::EventType GetEventType() const override { return GetStaticType(); }                                    \
	const char* GetName() const override { return #type; }

#define AE_EVENT_CLASS_CATEGORY(category)                                                                            \
	u32 GetCategoryFlags() const override { return static_cast<u32>(category); }
