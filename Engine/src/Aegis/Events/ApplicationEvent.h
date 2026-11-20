#pragma once

#include "Aegis/Events/Event.h"

namespace aegis
{
	class WindowResizeEvent : public Event
	{
	public:
		WindowResizeEvent(u32 width, u32 height)
			: m_Width(width)
			, m_Height(height)
		{
		}

		u32 GetWidth() const { return m_Width; }
		u32 GetHeight() const { return m_Height; }

		std::string ToString() const override
		{
			return std::format("WindowResizeEvent: {}, {}", m_Width, m_Height);
		}

		AE_EVENT_CLASS_TYPE(WindowResize)
		AE_EVENT_CLASS_CATEGORY(EventCategoryApplication)
	private:
		u32 m_Width = 0;
		u32 m_Height = 0;
	};

	class WindowCloseEvent : public Event
	{
	public:
		WindowCloseEvent() = default;
		AE_EVENT_CLASS_TYPE(WindowClose)
		AE_EVENT_CLASS_CATEGORY(EventCategoryApplication)
	};

	class WindowFocusEvent : public Event
	{
	public:
		WindowFocusEvent() = default;
		AE_EVENT_CLASS_TYPE(WindowFocus)
		AE_EVENT_CLASS_CATEGORY(EventCategoryApplication)
	};

	class WindowLostFocusEvent : public Event
	{
	public:
		WindowLostFocusEvent() = default;
		AE_EVENT_CLASS_TYPE(WindowLostFocus)
		AE_EVENT_CLASS_CATEGORY(EventCategoryApplication)
	};

	class WindowMovedEvent : public Event
	{
	public:
		WindowMovedEvent(i32 x, i32 y)
			: m_X(x)
			, m_Y(y)
		{
		}

		i32 GetX() const { return m_X; }
		i32 GetY() const { return m_Y; }

		std::string ToString() const override { return std::format("WindowMovedEvent: {}, {}", m_X, m_Y); }

		AE_EVENT_CLASS_TYPE(WindowMoved)
		AE_EVENT_CLASS_CATEGORY(EventCategoryApplication)
	private:
		i32 m_X = 0;
		i32 m_Y = 0;
	};

	class AppTickEvent : public Event
	{
	public:
		AppTickEvent() = default;
		AE_EVENT_CLASS_TYPE(AppTick)
		AE_EVENT_CLASS_CATEGORY(EventCategoryApplication)
	};

	class AppUpdateEvent : public Event
	{
	public:
		AppUpdateEvent() = default;
		AE_EVENT_CLASS_TYPE(AppUpdate)
		AE_EVENT_CLASS_CATEGORY(EventCategoryApplication)
	};

	class AppRenderEvent : public Event
	{
	public:
		AppRenderEvent() = default;
		AE_EVENT_CLASS_TYPE(AppRender)
		AE_EVENT_CLASS_CATEGORY(EventCategoryApplication)
	};
}
