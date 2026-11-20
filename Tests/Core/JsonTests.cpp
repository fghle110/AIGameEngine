#include "TestSupport.h"

#include <doctest/doctest.h>

#include "Aegis/Core/Json.h"

using namespace aegis;

TEST_CASE("Json: primitives round trip")
{
	CHECK(JsonValue{}.Dump() == "null");
	CHECK(JsonValue{ nullptr }.Dump() == "null");
	CHECK(JsonValue{ true }.Dump() == "true");
	CHECK(JsonValue{ false }.Dump() == "false");
	CHECK(JsonValue{ 42 }.Dump() == "42");
	CHECK(JsonValue{ -7 }.Dump() == "-7");
	CHECK(JsonValue{ 3.5 }.Dump() == "3.5");
	CHECK(JsonValue{ "hello" }.Dump() == "\"hello\"");
	CHECK(JsonValue{ std::string_view("aegis") }.Dump() == "\"aegis\"");
}

TEST_CASE("Json: strings escape and unescape")
{
	const std::string source = "quote \" backslash \\ newline\n tab\t 中文";
	const JsonValue value(source);

	const std::string dumped = value.Dump();
	CHECK(dumped == "\"quote \\\" backslash \\\\ newline\\n tab\\t 中文\"");

	auto parsed = JsonValue::Parse(dumped);
	REQUIRE(parsed.has_value());
	CHECK(parsed->AsString() == source);
}

TEST_CASE("Json: unicode escapes decode to UTF-8")
{
	auto parsed = JsonValue::Parse("\"\\u4f60\\u597d\"");
	REQUIRE(parsed.has_value());
	CHECK(parsed->AsString() == "你好");

	auto emoji = JsonValue::Parse("\"\\ud83d\\ude00\"");
	REQUIRE(emoji.has_value());
	CHECK(emoji->AsString() == "😀");
}

TEST_CASE("Json: objects and arrays")
{
	auto parsed = JsonValue::Parse(R"({
		// Aegis JSON allows comments.
		"name": "Feature Test",
		"version": 3,
		"enabled": true,
		"entities": [
			{ "name": "Camera", "position": [0, 1.5, -10] },
			{ "name": "Light" }
		],
		/* block comment */
		"empty": {}
	})");
	REQUIRE(parsed.has_value());

	CHECK(parsed->GetString("name") == "Feature Test");
	CHECK(parsed->GetInt("version") == 3);
	CHECK(parsed->GetBool("enabled"));
	CHECK(parsed->GetSize() == 5);

	const JsonValue& entities = (*parsed)["entities"];
	REQUIRE(entities.IsArray());
	REQUIRE(entities.GetSize() == 2);
	CHECK(entities[0].GetString("name") == "Camera");
	CHECK(entities[0]["position"].GetSize() == 3);
	CHECK(entities[0]["position"][1].AsNumber() == doctest::Approx(1.5));
	CHECK(entities[1].GetString("name") == "Light");
}

TEST_CASE("Json: builder API produces deterministic output")
{
	JsonValue root = JsonValue::MakeObject();
	root.Set("a", 1);

	JsonValue array = JsonValue::MakeArray();
	array.Push("one");
	array.Push("two");
	root.Set("b", array);

	CHECK(root.Dump() == R"({"a": 1,"b": ["one","two"]})");
	CHECK(root.Dump(2) == "{\n  \"a\": 1,\n  \"b\": [\n    \"one\",\n    \"two\"\n  ]\n}");
}

TEST_CASE("Json: numbers keep precision and integral form")
{
	auto parsed = JsonValue::Parse("[0, -0.0, 1e3, 1.5e-2, 12345678901234, 1.2345678901234567]");
	REQUIRE(parsed.has_value());
	REQUIRE(parsed->GetSize() == 6);
	CHECK((*parsed)[2].AsNumber() == doctest::Approx(1000.0));
	CHECK((*parsed)[3].AsNumber() == doctest::Approx(0.015));
	CHECK((*parsed)[4].AsInt() == 12345678901234ll);
	CHECK((*parsed)[5].AsNumber() == doctest::Approx(1.2345678901234567));

	const std::string dumped = JsonValue{ 1.0e3 }.Dump();
	CHECK(dumped == "1000");
}

TEST_CASE("Json: invalid documents report line and column")
{
	JsonValue::ParseError error;
	CHECK_FALSE(JsonValue::Parse("{\n  \"a\": ,\n}", &error).has_value());
	CHECK(error.Line == 2);
	CHECK(error.Column >= 7);
	CHECK_FALSE(error.Message.empty());

	JsonValue::ParseError trailing;
	CHECK_FALSE(JsonValue::Parse("{\"a\":1} extra", &trailing).has_value());
	CHECK_FALSE(trailing.Message.empty());
}

TEST_CASE("Json: invalid constructs are rejected")
{
	CHECK_FALSE(JsonValue::Parse("").has_value());
	CHECK_FALSE(JsonValue::Parse("{").has_value());
	CHECK_FALSE(JsonValue::Parse("{'a': 1}").has_value());
	CHECK_FALSE(JsonValue::Parse("{\"a\": 1,}").has_value());
	CHECK_FALSE(JsonValue::Parse("[1 2]").has_value());
	CHECK_FALSE(JsonValue::Parse("01").has_value());
	CHECK_FALSE(JsonValue::Parse("1.").has_value());
	CHECK_FALSE(JsonValue::Parse("\"unterminated").has_value());
	CHECK_FALSE(JsonValue::Parse("{\"a\": 1, \"a\": 2}").has_value());
	CHECK_FALSE(JsonValue::Parse("\"\\x\"").has_value());
	CHECK_FALSE(JsonValue::Parse("nul").has_value());
}

TEST_CASE("Json: nesting depth is bounded")
{
	std::string deep(300, '[');
	deep.append(300, ']');
	CHECK_FALSE(JsonValue::Parse(deep).has_value());

	std::string shallow(50, '[');
	shallow.append(50, ']');
	CHECK(JsonValue::Parse(shallow).has_value());
}

TEST_CASE("Json: missing members return defaults")
{
	JsonValue object = JsonValue::ParseOrEmpty(R"({"present": 5})");
	CHECK(object.GetInt("missing", 12) == 12);
	CHECK(object.GetString("missing", "fallback") == "fallback");
	CHECK_FALSE(object.Contains("missing"));
	CHECK(object.Find("missing") == nullptr);
	CHECK_FALSE(object.Remove("missing"));

	// Non-const operator[] is the documented way to create missing members.
	CHECK(object["created"].IsNull());
	CHECK(object.Contains("created"));
	CHECK(object.Remove("created"));
	CHECK(object.Remove("present"));
	CHECK(object.IsEmpty());
}

TEST_CASE("Json: copy is deep")
{
	JsonValue original = JsonValue::MakeObject();
	original["nested"]["value"] = 1;

	JsonValue copy = original;
	copy["nested"]["value"] = 2;

	CHECK(original["nested"]["value"].AsInt() == 1);
	CHECK(copy["nested"]["value"].AsInt() == 2);
}
