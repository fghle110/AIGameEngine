#pragma once

// Platform file and path utilities. All text paths are UTF-8; on Windows the
// wide-character native API is used underneath so non-ASCII paths work.

#include "Aegis/Core/Base.h"

#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace aegis
{
	class FileSystem
	{
	public:
		using Path = std::filesystem::path;

		static Path GetExecutablePath();
		static Path GetExecutableDirectory();
		static Path GetCurrentWorkingDirectory();
		static bool SetCurrentWorkingDirectory(const Path& path);
		static Path GetUserDataDirectory();
		static Path GetTempDirectory();

		static Path FromUtf8(std::string_view utf8);
		static std::string ToUtf8(const Path& path);

		static bool TryReadTextFile(const Path& path, std::string& outContents);
		static std::string ReadTextFile(const Path& path);
		static bool WriteTextFile(const Path& path, std::string_view contents, bool createDirectories = true);
		static bool TryReadBinaryFile(const Path& path, std::vector<u8>& outContents);
		static bool WriteBinaryFile(const Path& path, const void* data, usize size, bool createDirectories = true);

		static bool Exists(const Path& path);
		static bool IsFile(const Path& path);
		static bool IsDirectory(const Path& path);
		static bool CreateDirectories(const Path& path);
		static bool EnsureParentDirectory(const Path& path);
		static bool RemoveFile(const Path& path);
		static bool DeleteDirectory(const Path& path);
		static bool CopyFileTo(const Path& source, const Path& destination, bool overwrite = true);
		static bool Rename(const Path& source, const Path& destination);
		// Lists directory entries. Directories are only returned when requested,
		// which keeps asset enumeration free of folder noise.
		static std::vector<Path> ListDirectory(const Path& path, bool recursive = false,
			bool includeDirectories = false);

		static i64 GetFileSize(const Path& path);
		static std::optional<std::filesystem::file_time_type> GetLastWriteTime(const Path& path);

		static Path Absolute(const Path& path);
		static Path Normalize(const Path& path);
		static std::string GetExtension(const Path& path);
		static std::string GetFileName(const Path& path);
		static std::string GetStem(const Path& path);
		static Path WithExtension(const Path& path, std::string_view extension);
	};
}
