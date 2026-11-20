#include "Aegis/Core/Json.h"

#include "Aegis/Core/Log.h"

#include <charconv>
#include <cmath>
#include <cstring>

namespace aegis
{
	std::string_view JsonTypeToString(JsonType type)
	{
		switch (type)
		{
			case JsonType::Null: return "null";
			case JsonType::Bool: return "bool";
			case JsonType::Number: return "number";
			case JsonType::String: return "string";
			case JsonType::Array: return "array";
			case JsonType::Object: return "object";
		}
		return "unknown";
	}

	JsonValue::JsonValue(bool value) noexcept
		: m_Type(JsonType::Bool)
		, m_Bool(value)
	{
	}

	JsonValue::JsonValue(i8 value) noexcept
		: JsonValue(static_cast<i64>(value))
	{
	}

	JsonValue::JsonValue(u8 value) noexcept
		: JsonValue(static_cast<i64>(value))
	{
	}

	JsonValue::JsonValue(i16 value) noexcept
		: JsonValue(static_cast<i64>(value))
	{
	}

	JsonValue::JsonValue(u16 value) noexcept
		: JsonValue(static_cast<i64>(value))
	{
	}

	JsonValue::JsonValue(i32 value) noexcept
		: JsonValue(static_cast<i64>(value))
	{
	}

	JsonValue::JsonValue(u32 value) noexcept
		: JsonValue(static_cast<i64>(value))
	{
	}

	JsonValue::JsonValue(i64 value) noexcept
		: m_Type(JsonType::Number)
		, m_Number(static_cast<f64>(value))
	{
	}

	JsonValue::JsonValue(u64 value) noexcept
		: m_Type(JsonType::Number)
		, m_Number(static_cast<f64>(value))
	{
	}

	JsonValue::JsonValue(f32 value) noexcept
		: JsonValue(static_cast<f64>(value))
	{
	}

	JsonValue::JsonValue(f64 value) noexcept
		: m_Type(JsonType::Number)
		, m_Number(value)
	{
	}

	JsonValue::JsonValue(const char* value)
		: m_Type(JsonType::String)
		, m_String(value != nullptr ? value : "")
	{
	}

	JsonValue::JsonValue(std::string_view value)
		: m_Type(JsonType::String)
		, m_String(value)
	{
	}

	JsonValue::JsonValue(std::string value)
		: m_Type(JsonType::String)
		, m_String(std::move(value))
	{
	}

	JsonValue::JsonValue(Array value)
		: m_Type(JsonType::Array)
		, m_Array(std::make_unique<Array>(std::move(value)))
	{
	}

	JsonValue::JsonValue(Object value)
		: m_Type(JsonType::Object)
		, m_Object(std::make_unique<Object>(std::move(value)))
	{
	}

	JsonValue::JsonValue(const JsonValue& other)
		: m_Type(other.m_Type)
		, m_Bool(other.m_Bool)
		, m_Number(other.m_Number)
		, m_String(other.m_String)
	{
		if (other.m_Array)
		{
			m_Array = std::make_unique<Array>(*other.m_Array);
		}
		if (other.m_Object)
		{
			m_Object = std::make_unique<Object>(*other.m_Object);
		}
	}

	JsonValue::JsonValue(JsonValue&& other) noexcept
		: m_Type(other.m_Type)
		, m_Bool(other.m_Bool)
		, m_Number(other.m_Number)
		, m_String(std::move(other.m_String))
		, m_Array(std::move(other.m_Array))
		, m_Object(std::move(other.m_Object))
	{
		other.Reset();
	}

	JsonValue& JsonValue::operator=(const JsonValue& other)
	{
		if (this == &other)
		{
			return *this;
		}
		JsonValue copy(other);
		*this = std::move(copy);
		return *this;
	}

	JsonValue& JsonValue::operator=(JsonValue&& other) noexcept
	{
		if (this == &other)
		{
			return *this;
		}
		m_Type = other.m_Type;
		m_Bool = other.m_Bool;
		m_Number = other.m_Number;
		m_String = std::move(other.m_String);
		m_Array = std::move(other.m_Array);
		m_Object = std::move(other.m_Object);
		other.Reset();
		return *this;
	}

	JsonValue::~JsonValue() = default;

	void JsonValue::Reset()
	{
		m_Type = JsonType::Null;
		m_Bool = false;
		m_Number = 0.0;
		m_String.clear();
		m_Array.reset();
		m_Object.reset();
	}

	JsonValue JsonValue::MakeArray()
	{
		return JsonValue(Array{});
	}

	JsonValue JsonValue::MakeObject()
	{
		return JsonValue(Object{});
	}

	const JsonValue& JsonValue::Empty()
	{
		static const JsonValue s_Empty;
		return s_Empty;
	}

	bool JsonValue::AsBool(bool defaultValue) const
	{
		return m_Type == JsonType::Bool ? m_Bool : defaultValue;
	}

	f64 JsonValue::AsNumber(f64 defaultValue) const
	{
		return m_Type == JsonType::Number ? m_Number : defaultValue;
	}

	f32 JsonValue::AsFloat(f32 defaultValue) const
	{
		return m_Type == JsonType::Number ? static_cast<f32>(m_Number) : defaultValue;
	}

	i64 JsonValue::AsInt(i64 defaultValue) const
	{
		if (m_Type != JsonType::Number)
		{
			return defaultValue;
		}
		return static_cast<i64>(m_Number);
	}

	u64 JsonValue::AsUInt(u64 defaultValue) const
	{
		if (m_Type != JsonType::Number || m_Number < 0.0)
		{
			return defaultValue;
		}
		return static_cast<u64>(m_Number);
	}

	std::string_view JsonValue::AsString(std::string_view defaultValue) const
	{
		return m_Type == JsonType::String ? std::string_view(m_String) : defaultValue;
	}

	bool JsonValue::Contains(std::string_view key) const
	{
		return m_Type == JsonType::Object && m_Object != nullptr && m_Object->find(key) != m_Object->end();
	}

	const JsonValue* JsonValue::Find(std::string_view key) const
	{
		if (m_Type != JsonType::Object || m_Object == nullptr)
		{
			return nullptr;
		}
		const auto it = m_Object->find(key);
		return it == m_Object->end() ? nullptr : &it->second;
	}

	JsonValue& JsonValue::operator[](std::string_view key)
	{
		if (m_Type == JsonType::Null)
		{
			m_Type = JsonType::Object;
			m_Object = std::make_unique<Object>();
		}
		AE_ASSERT(m_Type == JsonType::Object, "JsonValue index by key requires an object, got {}",
			JsonTypeToString(m_Type));
		return (*m_Object)[std::string(key)];
	}

	const JsonValue& JsonValue::operator[](std::string_view key) const
	{
		const JsonValue* value = Find(key);
		return value != nullptr ? *value : Empty();
	}

	JsonValue& JsonValue::Set(std::string_view key, JsonValue value)
	{
		JsonValue& slot = (*this)[key];
		slot = std::move(value);
		return slot;
	}

	bool JsonValue::Remove(std::string_view key)
	{
		if (m_Type != JsonType::Object || m_Object == nullptr)
		{
			return false;
		}
		return m_Object->erase(std::string(key)) > 0;
	}

	std::string JsonValue::GetString(std::string_view key, std::string_view defaultValue) const
	{
		const JsonValue* value = Find(key);
		return value != nullptr ? std::string(value->AsString(defaultValue)) : std::string(defaultValue);
	}

	f64 JsonValue::GetNumber(std::string_view key, f64 defaultValue) const
	{
		const JsonValue* value = Find(key);
		return value != nullptr ? value->AsNumber(defaultValue) : defaultValue;
	}

	f32 JsonValue::GetFloat(std::string_view key, f32 defaultValue) const
	{
		const JsonValue* value = Find(key);
		return value != nullptr ? value->AsFloat(defaultValue) : defaultValue;
	}

	i64 JsonValue::GetInt(std::string_view key, i64 defaultValue) const
	{
		const JsonValue* value = Find(key);
		return value != nullptr ? value->AsInt(defaultValue) : defaultValue;
	}

	u64 JsonValue::GetUInt(std::string_view key, u64 defaultValue) const
	{
		const JsonValue* value = Find(key);
		return value != nullptr ? value->AsUInt(defaultValue) : defaultValue;
	}

	bool JsonValue::GetBool(std::string_view key, bool defaultValue) const
	{
		const JsonValue* value = Find(key);
		return value != nullptr ? value->AsBool(defaultValue) : defaultValue;
	}

	usize JsonValue::GetSize() const
	{
		switch (m_Type)
		{
			case JsonType::Array: return m_Array != nullptr ? m_Array->size() : 0;
			case JsonType::Object: return m_Object != nullptr ? m_Object->size() : 0;
			case JsonType::String: return m_String.size();
			default: return 0;
		}
	}

	JsonValue& JsonValue::operator[](usize index)
	{
		AE_ASSERT(m_Type == JsonType::Array, "JsonValue index by position requires an array");
		if (m_Array == nullptr)
		{
			m_Array = std::make_unique<Array>();
		}
		if (index >= m_Array->size())
		{
			m_Array->resize(index + 1);
		}
		return (*m_Array)[index];
	}

	const JsonValue& JsonValue::operator[](usize index) const
	{
		if (m_Type != JsonType::Array || m_Array == nullptr || index >= m_Array->size())
		{
			return Empty();
		}
		return (*m_Array)[index];
	}

	JsonValue& JsonValue::Push(JsonValue value)
	{
		if (m_Type == JsonType::Null)
		{
			m_Type = JsonType::Array;
			m_Array = std::make_unique<Array>();
		}
		AE_ASSERT(m_Type == JsonType::Array, "JsonValue::Push requires an array");
		m_Array->push_back(std::move(value));
		return m_Array->back();
	}

	const JsonValue::Array& JsonValue::GetArray() const
	{
		static const Array s_Empty;
		AE_ASSERT(m_Type == JsonType::Array || m_Type == JsonType::Null, "JsonValue is not an array");
		return m_Array != nullptr ? *m_Array : s_Empty;
	}

	const JsonValue::Object& JsonValue::GetObject() const
	{
		static const Object s_Empty;
		AE_ASSERT(m_Type == JsonType::Object || m_Type == JsonType::Null, "JsonValue is not an object");
		return m_Object != nullptr ? *m_Object : s_Empty;
	}

	namespace
	{
		void AppendIndent(std::string& out, int indent, int depth)
		{
			if (indent <= 0)
			{
				return;
			}
			out.push_back('\n');
			out.append(static_cast<usize>(indent * depth), ' ');
		}

		void AppendEscapedString(std::string& out, std::string_view text)
		{
			out.push_back('"');
			for (const char character : text)
			{
				const auto byte = static_cast<unsigned char>(character);
				switch (character)
				{
					case '"': out.append("\\\""); break;
					case '\\': out.append("\\\\"); break;
					case '\b': out.append("\\b"); break;
					case '\f': out.append("\\f"); break;
					case '\n': out.append("\\n"); break;
					case '\r': out.append("\\r"); break;
					case '\t': out.append("\\t"); break;
					default:
						if (byte < 0x20)
						{
							static constexpr char c_Hex[] = "0123456789abcdef";
							out.append("\\u00");
							out.push_back(c_Hex[(byte >> 4) & 0x0F]);
							out.push_back(c_Hex[byte & 0x0F]);
						}
						else
						{
							out.push_back(character);
						}
						break;
				}
			}
			out.push_back('"');
		}

		void AppendNumber(std::string& out, f64 value)
		{
			if (!std::isfinite(value))
			{
				out.append("null");
				return;
			}

			const f64 integral = std::floor(value);
			if (integral == value && std::fabs(value) < 1.0e15)
			{
				char buffer[32]{};
				const auto result = std::to_chars(buffer, buffer + sizeof(buffer), static_cast<i64>(value));
				out.append(buffer, static_cast<usize>(result.ptr - buffer));
				return;
			}

			char buffer[64]{};
			const auto result = std::to_chars(buffer, buffer + sizeof(buffer), value);
			out.append(buffer, static_cast<usize>(result.ptr - buffer));
		}

		class JsonParser
		{
		public:
			JsonParser(std::string_view text, JsonValue::ParseError* error)
				: m_Text(text)
				, m_Error(error)
			{
			}

			std::optional<JsonValue> ParseDocument()
			{
				SkipTrivia();
				JsonValue root;
				if (!ParseValue(root, 0))
				{
					return std::nullopt;
				}
				SkipTrivia();
				if (m_Position != m_Text.size())
				{
					Fail("unexpected trailing characters");
					return std::nullopt;
				}
				return root;
			}

		private:
			static constexpr int c_MaxDepth = 256;

			bool Fail(std::string_view message)
			{
				if (m_Error != nullptr && m_Error->Message.empty())
				{
					m_Error->Offset = m_Position;
					m_Error->Line = 1;
					m_Error->Column = 1;
					for (usize i = 0; i < m_Position && i < m_Text.size(); ++i)
					{
						if (m_Text[i] == '\n')
						{
							++m_Error->Line;
							m_Error->Column = 1;
						}
						else
						{
							++m_Error->Column;
						}
					}
					m_Error->Message = std::string(message);
				}
				return false;
			}

			bool AtEnd() const { return m_Position >= m_Text.size(); }
			char Peek() const { return AtEnd() ? '\0' : m_Text[m_Position]; }
			char Advance() { return AtEnd() ? '\0' : m_Text[m_Position++]; }

			void SkipTrivia()
			{
				while (!AtEnd())
				{
					const char c = Peek();
					if (c == ' ' || c == '\t' || c == '\r' || c == '\n')
					{
						++m_Position;
						continue;
					}
					if (c == '/' && m_Position + 1 < m_Text.size())
					{
						if (m_Text[m_Position + 1] == '/')
						{
							m_Position += 2;
							while (!AtEnd() && Peek() != '\n')
							{
								++m_Position;
							}
							continue;
						}
						if (m_Text[m_Position + 1] == '*')
						{
							m_Position += 2;
							while (m_Position + 1 < m_Text.size() &&
								!(m_Text[m_Position] == '*' && m_Text[m_Position + 1] == '/'))
							{
								++m_Position;
							}
							if (m_Position + 1 >= m_Text.size())
							{
								m_Position = m_Text.size();
								break;
							}
							m_Position += 2;
							continue;
						}
					}
					break;
				}
			}

			bool Expect(char expected)
			{
				SkipTrivia();
				if (Peek() != expected)
				{
					return Fail(std::string("expected '") + expected + "'");
				}
				++m_Position;
				return true;
			}

			bool ParseValue(JsonValue& out, int depth)
			{
				if (depth > c_MaxDepth)
				{
					return Fail("maximum nesting depth exceeded");
				}
				SkipTrivia();
				if (AtEnd())
				{
					return Fail("unexpected end of input, expected a value");
				}

				switch (Peek())
				{
					case '{': return ParseObject(out, depth);
					case '[': return ParseArray(out, depth);
					case '"':
					{
						std::string text;
						if (!ParseString(text))
						{
							return false;
						}
						out = JsonValue(std::move(text));
						return true;
					}
					case 't':
						if (MatchLiteral("true"))
						{
							out = JsonValue(true);
							return true;
						}
						return Fail("invalid literal, expected 'true'");
					case 'f':
						if (MatchLiteral("false"))
						{
							out = JsonValue(false);
							return true;
						}
						return Fail("invalid literal, expected 'false'");
					case 'n':
						if (MatchLiteral("null"))
						{
							out = JsonValue();
							return true;
						}
						return Fail("invalid literal, expected 'null'");
					default: return ParseNumber(out);
				}
			}

			bool MatchLiteral(std::string_view literal)
			{
				if (m_Text.substr(m_Position, literal.size()) != literal)
				{
					return false;
				}
				m_Position += literal.size();
				return true;
			}

			bool ParseObject(JsonValue& out, int depth)
			{
				++m_Position; // '{'
				JsonValue::Object object;
				SkipTrivia();
				if (Peek() == '}')
				{
					++m_Position;
					out = JsonValue(std::move(object));
					return true;
				}

				for (;;)
				{
					SkipTrivia();
					if (Peek() != '"')
					{
						return Fail("expected a string key");
					}
					std::string key;
					if (!ParseString(key))
					{
						return false;
					}
					if (!Expect(':'))
					{
						return false;
					}
					JsonValue value;
					if (!ParseValue(value, depth + 1))
					{
						return false;
					}
					if (!object.emplace(key, std::move(value)).second)
					{
						return Fail("duplicate object key '" + key + "'");
					}
					SkipTrivia();
					if (Peek() == ',')
					{
						++m_Position;
						continue;
					}
					if (Peek() == '}')
					{
						++m_Position;
						break;
					}
					return Fail("expected ',' or '}'");
				}

				out = JsonValue(std::move(object));
				return true;
			}

			bool ParseArray(JsonValue& out, int depth)
			{
				++m_Position; // '['
				JsonValue::Array array;
				SkipTrivia();
				if (Peek() == ']')
				{
					++m_Position;
					out = JsonValue(std::move(array));
					return true;
				}

				for (;;)
				{
					JsonValue value;
					if (!ParseValue(value, depth + 1))
					{
						return false;
					}
					array.push_back(std::move(value));
					SkipTrivia();
					if (Peek() == ',')
					{
						++m_Position;
						continue;
					}
					if (Peek() == ']')
					{
						++m_Position;
						break;
					}
					return Fail("expected ',' or ']'");
				}

				out = JsonValue(std::move(array));
				return true;
			}

			bool ParseString(std::string& out)
			{
				AE_ASSERT(Peek() == '"');
				++m_Position;
				out.clear();
				while (!AtEnd())
				{
					const auto byte = static_cast<unsigned char>(Peek());
					if (byte == '"')
					{
						++m_Position;
						return true;
					}
					if (byte < 0x20)
					{
						return Fail("unescaped control character in string");
					}
					if (byte != '\\')
					{
						out.push_back(Advance());
						continue;
					}

					++m_Position; // backslash
					if (AtEnd())
					{
						return Fail("unterminated escape sequence");
					}
					switch (Advance())
					{
						case '"': out.push_back('"'); break;
						case '\\': out.push_back('\\'); break;
						case '/': out.push_back('/'); break;
						case 'b': out.push_back('\b'); break;
						case 'f': out.push_back('\f'); break;
						case 'n': out.push_back('\n'); break;
						case 'r': out.push_back('\r'); break;
						case 't': out.push_back('\t'); break;
						case 'u':
						{
							u32 code = 0;
							if (!ParseHex4(code))
							{
								return false;
							}
							if (code >= 0xD800 && code <= 0xDBFF)
							{
								if (m_Position + 1 >= m_Text.size() || m_Text[m_Position] != '\\' ||
									m_Text[m_Position + 1] != 'u')
								{
									return Fail("high surrogate without a following low surrogate");
								}
								m_Position += 2;
								u32 low = 0;
								if (!ParseHex4(low))
								{
									return false;
								}
								if (low < 0xDC00 || low > 0xDFFF)
								{
									return Fail("invalid low surrogate");
								}
								code = 0x10000 + (((code - 0xD800) << 10) | (low - 0xDC00));
							}
							else if (code >= 0xDC00 && code <= 0xDFFF)
							{
								return Fail("unexpected low surrogate");
							}
							AppendUtf8(out, code);
							break;
						}
						default: return Fail("invalid escape character");
					}
				}
				return Fail("unterminated string");
			}

			bool ParseHex4(u32& out)
			{
				if (m_Position + 4 > m_Text.size())
				{
					return Fail("truncated \\u escape");
				}
				u32 value = 0;
				for (int i = 0; i < 4; ++i)
				{
					const char c = m_Text[m_Position++];
					value <<= 4;
					if (c >= '0' && c <= '9')
					{
						value |= static_cast<u32>(c - '0');
					}
					else if (c >= 'a' && c <= 'f')
					{
						value |= static_cast<u32>(c - 'a' + 10);
					}
					else if (c >= 'A' && c <= 'F')
					{
						value |= static_cast<u32>(c - 'A' + 10);
					}
					else
					{
						return Fail("invalid hexadecimal digit in \\u escape");
					}
				}
				out = value;
				return true;
			}

			static void AppendUtf8(std::string& out, u32 code)
			{
				if (code <= 0x7F)
				{
					out.push_back(static_cast<char>(code));
				}
				else if (code <= 0x7FF)
				{
					out.push_back(static_cast<char>(0xC0 | (code >> 6)));
					out.push_back(static_cast<char>(0x80 | (code & 0x3F)));
				}
				else if (code <= 0xFFFF)
				{
					out.push_back(static_cast<char>(0xE0 | (code >> 12)));
					out.push_back(static_cast<char>(0x80 | ((code >> 6) & 0x3F)));
					out.push_back(static_cast<char>(0x80 | (code & 0x3F)));
				}
				else
				{
					out.push_back(static_cast<char>(0xF0 | (code >> 18)));
					out.push_back(static_cast<char>(0x80 | ((code >> 12) & 0x3F)));
					out.push_back(static_cast<char>(0x80 | ((code >> 6) & 0x3F)));
					out.push_back(static_cast<char>(0x80 | (code & 0x3F)));
				}
			}

			bool ParseNumber(JsonValue& out)
			{
				const usize start = m_Position;
				if (Peek() == '-')
				{
					++m_Position;
				}

				if (Peek() == '0')
				{
					++m_Position;
				}
				else if (Peek() >= '1' && Peek() <= '9')
				{
					while (Peek() >= '0' && Peek() <= '9')
					{
						++m_Position;
					}
				}
				else
				{
					return Fail("unexpected character, expected a value");
				}

				if (Peek() == '.')
				{
					++m_Position;
					if (!(Peek() >= '0' && Peek() <= '9'))
					{
						return Fail("expected a digit after the decimal point");
					}
					while (Peek() >= '0' && Peek() <= '9')
					{
						++m_Position;
					}
				}

				if (Peek() == 'e' || Peek() == 'E')
				{
					++m_Position;
					if (Peek() == '+' || Peek() == '-')
					{
						++m_Position;
					}
					if (!(Peek() >= '0' && Peek() <= '9'))
					{
						return Fail("expected a digit in the exponent");
					}
					while (Peek() >= '0' && Peek() <= '9')
					{
						++m_Position;
					}
				}

				f64 value = 0.0;
				const char* begin = m_Text.data() + start;
				const char* end = m_Text.data() + m_Position;
				const auto result = std::from_chars(begin, end, value);
				if (result.ec != std::errc{} || result.ptr != end)
				{
					return Fail("invalid number");
				}
				out = JsonValue(value);
				return true;
			}

			std::string_view m_Text;
			usize m_Position = 0;
			JsonValue::ParseError* m_Error = nullptr;
		};
	}

	std::string JsonValue::Dump(int indent) const
	{
		std::string out;
		DumpTo(out, indent, 0);
		return out;
	}

	void JsonValue::DumpTo(std::string& out, int indent, int depth) const
	{
		switch (m_Type)
		{
			case JsonType::Null: out.append("null"); return;
			case JsonType::Bool: out.append(m_Bool ? "true" : "false"); return;
			case JsonType::Number: AppendNumber(out, m_Number); return;
			case JsonType::String: AppendEscapedString(out, m_String); return;
			case JsonType::Array:
			{
				out.push_back('[');
				const Array& array = *m_Array;
				if (array.empty())
				{
					out.push_back(']');
					return;
				}
				for (usize i = 0; i < array.size(); ++i)
				{
					if (i != 0)
					{
						out.push_back(',');
					}
					AppendIndent(out, indent, depth + 1);
					array[i].DumpTo(out, indent, depth + 1);
				}
				AppendIndent(out, indent, depth);
				out.push_back(']');
				return;
			}
			case JsonType::Object:
			{
				out.push_back('{');
				const Object& object = *m_Object;
				if (object.empty())
				{
					out.push_back('}');
					return;
				}
				bool first = true;
				for (const auto& [key, value] : object)
				{
					if (!first)
					{
						out.push_back(',');
					}
					first = false;
					AppendIndent(out, indent, depth + 1);
					AppendEscapedString(out, key);
					out.append(": ");
					value.DumpTo(out, indent, depth + 1);
				}
				AppendIndent(out, indent, depth);
				out.push_back('}');
				return;
			}
		}
	}

	std::optional<JsonValue> JsonValue::Parse(std::string_view text, ParseError* outError)
	{
		ParseError error;
		JsonParser parser(text, outError != nullptr ? outError : &error);
		std::optional<JsonValue> result = parser.ParseDocument();
		if (!result.has_value() && outError != nullptr && !outError->IsValid())
		{
			AE_CORE_DEBUG("JSON parse error at {}:{}: {}", outError->Line, outError->Column, outError->Message);
		}
		return result;
	}

	JsonValue JsonValue::ParseOrEmpty(std::string_view text, std::string_view context)
	{
		ParseError error;
		if (auto result = Parse(text, &error))
		{
			return *result;
		}
		AE_CORE_ERROR("Failed to parse JSON for '{}' at {}:{}: {}", context, error.Line, error.Column, error.Message);
		return JsonValue();
	}
}
