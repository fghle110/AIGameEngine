#include "TestSupport.h"

#include <doctest/doctest.h>

#include "Aegis/Core/Uuid.h"

using namespace aegis;

TEST_CASE("Uuid: generated identifiers are unique and non-null")
{
	Uuid first = Uuid::Generate();
	Uuid second = Uuid::Generate();

	CHECK_FALSE(first.IsNull());
	CHECK_FALSE(second.IsNull());
	CHECK(first != second);
	CHECK(static_cast<bool>(first));
}

TEST_CASE("Uuid: string round trip")
{
	const Uuid uuid = Uuid::Generate();
	const std::string text = uuid.ToString();

	// 8-4-4-4-12 => 36 characters including dashes.
	CHECK(text.size() == 36);
	CHECK(text[8] == '-');
	CHECK(text[13] == '-');
	CHECK(text[18] == '-');
	CHECK(text[23] == '-');

	CHECK(Uuid::FromString(text) == uuid);
}

TEST_CASE("Uuid: malformed strings parse as null")
{
	CHECK(Uuid::FromString("").IsNull());
	CHECK(Uuid::FromString("not-a-uuid").IsNull());
	CHECK(Uuid::FromString("00112233445566778899aabbccddeef").IsNull());   // too short
	// Dashes are ignored anywhere, so a misplaced separator still parses.
	CHECK_FALSE(Uuid::FromString("00112233445566778-899aabbccddeeff").IsNull());
	CHECK(Uuid::FromString("00112233445566778899aabbccddeefff").IsNull()); // 33 digits
	CHECK(Uuid::FromString("00112233445566778899aabbccddeeff").IsNull() == false);
}

TEST_CASE("Uuid: null string form")
{
	CHECK(Uuid::Null().IsNull());
	CHECK(Uuid::Null().ToString() == "00000000-0000-0000-0000-000000000000");
}

TEST_CASE("Uuid: hash is stable for equal values")
{
	const Uuid a = Uuid::FromString("00112233-4455-6677-8899-aabbccddeeff");
	const Uuid b = Uuid::FromString("00112233-4455-6677-8899-aabbccddeeff");

	CHECK(UuidHash{}(a) == UuidHash{}(b));
}
