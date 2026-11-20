#include "TestSupport.h"

#include <doctest/doctest.h>

#include "Aegis/Core/StringUtils.h"

#include <ostream>
#include <sstream>

using namespace aegis;

TEST_CASE("StringUtils: case conversion")
{
	CHECK(StringUtils::ToLower("Aegis Engine") == "aegis engine");
	CHECK(StringUtils::ToUpper("Aegis Engine") == "AEGIS ENGINE");
	CHECK(StringUtils::EqualsIgnoreCase("Mesh", "MESH"));
	CHECK_FALSE(StringUtils::EqualsIgnoreCase("Mesh", "Meshes"));
}

TEST_CASE("StringUtils: trim")
{
	CHECK(StringUtils::Trim("  hello \t\n") == "hello");
	CHECK(StringUtils::Trim("").empty());
	CHECK(StringUtils::TrimCopy("  x  ") == "x");
}

TEST_CASE("StringUtils: split and join")
{
	const auto parts = StringUtils::Split("a,b,,c", ',', false);
	REQUIRE(parts.size() == 3);
	CHECK(parts[0] == "a");
	CHECK(parts[2] == "c");

	const auto withEmpty = StringUtils::Split("a,b,,c", ',', true);
	CHECK(withEmpty.size() == 4);

	CHECK(StringUtils::Join(parts, "/") == "a/b/c");
}

TEST_CASE("StringUtils: replace")
{
	CHECK(StringUtils::ReplaceAll("entities/entity/entityid", "entity", "node") == "entities/node/nodeid");
	CHECK(StringUtils::ReplaceAll("aaa", "", "x") == "aaa");
	CHECK(StringUtils::ReplaceAll("abc", "d", "x") == "abc");
}

TEST_CASE("StringUtils: split path")
{
	const auto segments = StringUtils::SplitPath("transform.position");
	REQUIRE(segments.size() == 2);
	CHECK(segments[0] == "transform");
	CHECK(segments[1] == "position");
}
