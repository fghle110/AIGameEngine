#include "Aegis/Core/KeyCodes.h"

namespace aegis
{
	std::string_view KeyCodeToString(KeyCode key)
	{
		switch (key)
		{
			case KeyCode::Space: return "Space";
			case KeyCode::Apostrophe: return "Apostrophe";
			case KeyCode::Comma: return "Comma";
			case KeyCode::Minus: return "Minus";
			case KeyCode::Period: return "Period";
			case KeyCode::Slash: return "Slash";
			case KeyCode::D0: return "0";
			case KeyCode::D1: return "1";
			case KeyCode::D2: return "2";
			case KeyCode::D3: return "3";
			case KeyCode::D4: return "4";
			case KeyCode::D5: return "5";
			case KeyCode::D6: return "6";
			case KeyCode::D7: return "7";
			case KeyCode::D8: return "8";
			case KeyCode::D9: return "9";
			case KeyCode::Semicolon: return "Semicolon";
			case KeyCode::Equal: return "Equal";
			case KeyCode::A: return "A";
			case KeyCode::B: return "B";
			case KeyCode::C: return "C";
			case KeyCode::D: return "D";
			case KeyCode::E: return "E";
			case KeyCode::F: return "F";
			case KeyCode::G: return "G";
			case KeyCode::H: return "H";
			case KeyCode::I: return "I";
			case KeyCode::J: return "J";
			case KeyCode::K: return "K";
			case KeyCode::L: return "L";
			case KeyCode::M: return "M";
			case KeyCode::N: return "N";
			case KeyCode::O: return "O";
			case KeyCode::P: return "P";
			case KeyCode::Q: return "Q";
			case KeyCode::R: return "R";
			case KeyCode::S: return "S";
			case KeyCode::T: return "T";
			case KeyCode::U: return "U";
			case KeyCode::V: return "V";
			case KeyCode::W: return "W";
			case KeyCode::X: return "X";
			case KeyCode::Y: return "Y";
			case KeyCode::Z: return "Z";
			case KeyCode::Escape: return "Escape";
			case KeyCode::Enter: return "Enter";
			case KeyCode::Tab: return "Tab";
			case KeyCode::Backspace: return "Backspace";
			case KeyCode::Insert: return "Insert";
			case KeyCode::Delete: return "Delete";
			case KeyCode::Right: return "Right";
			case KeyCode::Left: return "Left";
			case KeyCode::Down: return "Down";
			case KeyCode::Up: return "Up";
			case KeyCode::PageUp: return "PageUp";
			case KeyCode::PageDown: return "PageDown";
			case KeyCode::Home: return "Home";
			case KeyCode::End: return "End";
			case KeyCode::CapsLock: return "CapsLock";
			case KeyCode::LeftShift: return "LeftShift";
			case KeyCode::LeftControl: return "LeftControl";
			case KeyCode::LeftAlt: return "LeftAlt";
			case KeyCode::RightShift: return "RightShift";
			case KeyCode::RightControl: return "RightControl";
			case KeyCode::RightAlt: return "RightAlt";
			case KeyCode::F1: return "F1";
			case KeyCode::F2: return "F2";
			case KeyCode::F3: return "F3";
			case KeyCode::F4: return "F4";
			case KeyCode::F5: return "F5";
			case KeyCode::F6: return "F6";
			case KeyCode::F7: return "F7";
			case KeyCode::F8: return "F8";
			case KeyCode::F9: return "F9";
			case KeyCode::F10: return "F10";
			case KeyCode::F11: return "F11";
			case KeyCode::F12: return "F12";
			default: break;
		}
		return "Unknown";
	}
}
