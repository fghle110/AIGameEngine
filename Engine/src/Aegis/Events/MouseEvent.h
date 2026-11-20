#pragma once

#include "Aegis/Core/KeyCodes.h"
#include "Aegis/Events/Event.h"

namespace aegis
{
	class MouseMovedEvent : public Event
	{
	public:
		MouseMovedEvent(f32 x, f32 y)
			: m_MouseX(x)
			, m_MouseY(y)
		{
		}

		f32 GetX() const { return m_MouseX; }
		f32 GetY() const { return m_MouseY; }
		std::pair<f32, f32> GetPosition() const { return { m_MouseX, m_MouseY }; }

		std::string ToString() const override
		{
			return std::format("MouseMovedEvent: {}, {}", m_MouseX, m_MouseY);
		}

		AE_EVENT_CLASS_TYPE(MouseMoved)
		AE_EVENT_CLASS_CATEGORY(EventCategoryMouse | EventCategoryInput)
	private:
		f32 m_MouseX = 0.0f;
		f32 m_MouseY = 0.0f;
	};

	class MouseScrolledEvent : public Event
	{
	public:
		MouseScrolledEvent(f32 xOffset, f32 yOffset)
			: m_XOffset(xOffset)
			, m_YOffset(yOffset)
		{
		}

		f32 GetXOffset() const { return m_XOffset; }
		f32 GetYOffset() const { return m_YOffset; }

		std::string ToString() const override
		{
			return std::format("MouseScrolledEvent: {}, {}", m_XOffset, m_YOffset);
		}

		AE_EVENT_CLASS_TYPE(MouseScrolled)
		AE_EVENT_CLASS_CATEGORY(EventCategoryMouse | EventCategoryInput)
	private:
		f32 m_XOffset = 0.0f;
		f32 m_YOffset = 0.0f;
	};

	class MouseButtonEvent : public Event
	{
	public:
		MouseCode GetMouseButton() const { return m_Button; }

		AE_EVENT_CLASS_CATEGORY(EventCategoryMouse | EventCategoryInput | EventCategoryMouseButton)
	protected:
		explicit MouseButtonEvent(MouseCode button)
			: m_Button(button)
		{
		}

		MouseCode m_Button = MouseCode::Left;
	};

	class MouseButtonPressedEvent : public MouseButtonEvent
	{
	public:
		explicit MouseButtonPressedEvent(MouseCode button)
			: MouseButtonEvent(button)
		{
		}

		std::string ToString() const override
		{
			return std::format("MouseButtonPressedEvent: {}", static_cast<i32>(m_Button));
		}

		AE_EVENT_CLASS_TYPE(MouseButtonPressed)
	};

	class MouseButtonReleasedEvent : public MouseButtonEvent
	{
	public:
		explicit MouseButtonReleasedEvent(MouseCode button)
			: MouseButtonEvent(button)
		{
		}

		std::string ToString() const override
		{
			return std::format("MouseButtonReleasedEvent: {}", static_cast<i32>(m_Button));
		}

		AE_EVENT_CLASS_TYPE(MouseButtonReleased)
	};
}
