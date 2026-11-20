#include "TestSupport.h"

#include <doctest/doctest.h>

#include "Aegis/Core/FileSystem.h"
#include "Aegis/Core/StringUtils.h"

#include <fstream>

using namespace aegis;

namespace
{
	FileSystem::Path TestDirectory()
	{
		const FileSystem::Path directory = FileSystem::FromUtf8("TestAssets/FileSystem");
		FileSystem::DeleteDirectory(directory);
		FileSystem::CreateDirectories(directory);
		return directory;
	}
}

TEST_CASE("FileSystem: text file round trip")
{
	const auto directory = TestDirectory();
	const auto path = directory / "hello.txt";

	REQUIRE(FileSystem::WriteTextFile(path, "Aegis engine\nsecond line", true));
	CHECK(FileSystem::Exists(path));
	CHECK(FileSystem::IsFile(path));
	CHECK(FileSystem::ReadTextFile(path) == "Aegis engine\nsecond line");
}

TEST_CASE("FileSystem: binary file round trip")
{
	const auto directory = TestDirectory();
	const auto path = directory / "bytes.bin";

	const std::vector<u8> data{ 0, 1, 2, 3, 255, 128, 64 };
	REQUIRE(FileSystem::WriteBinaryFile(path, data.data(), data.size()));
	CHECK(FileSystem::GetFileSize(path) == static_cast<i64>(data.size()));

	std::vector<u8> loaded;
	REQUIRE(FileSystem::TryReadBinaryFile(path, loaded));
	CHECK(loaded == data);
}

TEST_CASE("FileSystem: missing files report failure instead of throwing")
{
	std::vector<u8> data;
	CHECK_FALSE(FileSystem::TryReadBinaryFile(FileSystem::FromUtf8("TestAssets/does-not-exist.bin"), data));
	CHECK_FALSE(FileSystem::Exists(FileSystem::FromUtf8("TestAssets/does-not-exist.bin")));
	CHECK(FileSystem::GetFileSize(FileSystem::FromUtf8("TestAssets/does-not-exist.bin")) == -1);
}

TEST_CASE("FileSystem: extensions are lower-cased")
{
	CHECK(FileSystem::GetExtension(FileSystem::FromUtf8("Model.GLTF")) == "gltf");
	CHECK(FileSystem::GetExtension(FileSystem::FromUtf8("no-extension")) == "");
	CHECK(FileSystem::GetStem(FileSystem::FromUtf8("dir/Model.v2.gltf")) == "Model.v2");
}

TEST_CASE("FileSystem: UTF-8 paths round trip")
{
	const auto directory = TestDirectory();
	const auto path = directory / FileSystem::FromUtf8("引擎-测试.txt");
	REQUIRE(FileSystem::WriteTextFile(path, "你好，Aegis"));
	CHECK(FileSystem::ReadTextFile(path) == "你好，Aegis");
	CHECK(FileSystem::ToUtf8(path).find("引擎-测试.txt") != std::string::npos);
}

TEST_CASE("FileSystem: recursive listing finds nested entries")
{
	const auto directory = TestDirectory();
	FileSystem::CreateDirectories(directory / "a" / "b");
	REQUIRE(FileSystem::WriteTextFile(directory / "a" / "one.txt", "1"));
	REQUIRE(FileSystem::WriteTextFile(directory / "a" / "b" / "two.txt", "2"));

	const auto entries = FileSystem::ListDirectory(directory, true);
	CHECK(entries.size() == 2);
}
