#pragma once

#include "Aegis/Core/KeyCodes.h"
#include "Aegis/Events/Event.h"

namespace aegis
{
	class KeyEvent : public Event
	{
	public:
		KeyCode GetKeyCode() const { return m_KeyCode; }

		AE_EVENT_CLASS_CATEGORY(EventCategoryKeyboard | EventCategoryInput)
	protected:
		explicit KeyEvent(KeyCode keycode)
			: m_KeyCode(keycode)
		{
		}

		KeyCode m_KeyCode = KeyCode::Unknown;
	};

	class KeyPressedEvent : public KeyEvent
	{
	public:
		KeyPressedEvent(KeyCode keycode, bool isRepeat)
			: KeyEvent(keycode)
			, m_IsRepeat(isRepeat)
		{
		}

		bool IsRepeat() const { return m_IsRepeat; }

		std::string ToString() const override
		{
			return std::format("KeyPressedEvent: {} (repeat = {})", KeyCodeToString(m_KeyCode), m_IsRepeat);
		}

		AE_EVENT_CLASS_TYPE(KeyPressed)
	private:
		bool m_IsRepeat = false;
	};

	class KeyReleasedEvent : public KeyEvent
	{
	public:
		explicit KeyReleasedEvent(KeyCode keycode)
			: KeyEvent(keycode)
		{
		}

		std::string ToString() const override
		{
			return std::format("KeyReleasedEvent: {}", KeyCodeToString(m_KeyCode));
		}

		AE_EVENT_CLASS_TYPE(KeyReleased)
	};

	class KeyTypedEvent : public KeyEvent
	{
	public:
		explicit KeyTypedEvent(KeyCode keycode)
			: KeyEvent(keycode)
		{
		}

		std::string ToString() const override
		{
			return std::format("KeyTypedEvent: {}", KeyCodeToString(m_KeyCode));
		}

		AE_EVENT_CLASS_TYPE(KeyTyped)
	};
}
