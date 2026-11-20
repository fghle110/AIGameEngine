#pragma once

// A compact, dependency-free JSON DOM used by every human-readable engine
// format (scenes, prefabs, materials, projects) and by the AI command API.
//
// The parser accepts the JSON superset that is convenient for authored data:
// "//" line comments and "/* */" block comments are ignored. Output is always
// strict JSON with deterministic member ordering.

#include "Aegis/Core/Base.h"

#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace aegis
{
	enum class JsonType : u8
	{
		Null = 0,
		Bool,
		Number,
		String,
		Array,
		Object
	};

	std::string_view JsonTypeToString(JsonType type);

	class JsonValue
	{
	public:
		using Array = std::vector<JsonValue>;
		using Object = std::map<std::string, JsonValue, std::less<>>;

		JsonValue() noexcept = default;
		JsonValue(std::nullptr_t) noexcept {}
		JsonValue(bool value) noexcept;
		JsonValue(i8 value) noexcept;
		JsonValue(u8 value) noexcept;
		JsonValue(i16 value) noexcept;
		JsonValue(u16 value) noexcept;
		JsonValue(i32 value) noexcept;
		JsonValue(u32 value) noexcept;
		JsonValue(i64 value) noexcept;
		JsonValue(u64 value) noexcept;
		JsonValue(f32 value) noexcept;
		JsonValue(f64 value) noexcept;
		JsonValue(const char* value);
		JsonValue(std::string_view value);
		JsonValue(std::string value);
		JsonValue(Array value);
		JsonValue(Object value);

		JsonValue(const JsonValue& other);
		JsonValue(JsonValue&& other) noexcept;
		JsonValue& operator=(const JsonValue& other);
		JsonValue& operator=(JsonValue&& other) noexcept;
		~JsonValue();

		static JsonValue MakeArray();
		static JsonValue MakeObject();
		static const JsonValue& Empty();

		JsonType GetType() const { return m_Type; }
		bool IsNull() const { return m_Type == JsonType::Null; }
		bool IsBool() const { return m_Type == JsonType::Bool; }
		bool IsNumber() const { return m_Type == JsonType::Number; }
		bool IsString() const { return m_Type == JsonType::String; }
		bool IsArray() const { return m_Type == JsonType::Array; }
		bool IsObject() const { return m_Type == JsonType::Object; }

		// Value accessors are forgiving: a mismatched type yields the default
		// value instead of undefined behaviour. This keeps optional authoring data
		// robust without hiding format errors (see TryGet/Find).
		bool AsBool(bool defaultValue = false) const;
		f64 AsNumber(f64 defaultValue = 0.0) const;
		f32 AsFloat(f32 defaultValue = 0.0f) const;
		i64 AsInt(i64 defaultValue = 0) const;
		u64 AsUInt(u64 defaultValue = 0) const;
		std::string_view AsString(std::string_view defaultValue = std::string_view{}) const;

		// Object helpers.
		bool Contains(std::string_view key) const;
		const JsonValue* Find(std::string_view key) const;
		JsonValue& operator[](std::string_view key);
		const JsonValue& operator[](std::string_view key) const;
		JsonValue& Set(std::string_view key, JsonValue value);
		bool Remove(std::string_view key);

		std::string GetString(std::string_view key, std::string_view defaultValue = std::string_view{}) const;
		f64 GetNumber(std::string_view key, f64 defaultValue = 0.0) const;
		f32 GetFloat(std::string_view key, f32 defaultValue = 0.0f) const;
		i64 GetInt(std::string_view key, i64 defaultValue = 0) const;
		u64 GetUInt(std::string_view key, u64 defaultValue = 0) const;
		bool GetBool(std::string_view key, bool defaultValue = false) const;

		// Array helpers.
		usize GetSize() const;
		bool IsEmpty() const { return GetSize() == 0; }
		JsonValue& operator[](usize index);
		const JsonValue& operator[](usize index) const;
		JsonValue& Push(JsonValue value);
		const Array& GetArray() const;
		const Object& GetObject() const;

		// Serialisation. indent < 0 produces compact output.
		std::string Dump(int indent = -1) const;

		struct ParseError
		{
			usize Line = 0;
			usize Column = 0;
			usize Offset = 0;
			std::string Message;

			bool IsValid() const { return Message.empty(); }
		};

		static std::optional<JsonValue> Parse(std::string_view text, ParseError* outError = nullptr);

		// Parses and reports a readable error through the log if the text is invalid.
		static JsonValue ParseOrEmpty(std::string_view text, std::string_view context = {});

	private:
		void DumpTo(std::string& out, int indent, int depth) const;
		void Reset();

		JsonType m_Type = JsonType::Null;
		bool m_Bool = false;
		f64 m_Number = 0.0;
		std::string m_String;
		Scope<Array> m_Array;
		Scope<Object> m_Object;
	};
}
