#include "Aegis/Core/FileSystem.h"

#include <cctype>
#include <cstdlib>
#include <fstream>
#include <system_error>

#if AE_PLATFORM_WINDOWS
	#include <windows.h>
#elif AE_PLATFORM_MACOS
	#include <mach-o/dyld.h>
	#include <unistd.h>
#else
	#include <unistd.h>
#endif

namespace aegis
{
	FileSystem::Path FileSystem::GetExecutablePath()
	{
#if AE_PLATFORM_WINDOWS
		std::wstring buffer(MAX_PATH, L'\0');
		for (;;)
		{
			const DWORD length = ::GetModuleFileNameW(nullptr, buffer.data(), static_cast<DWORD>(buffer.size()));
			if (length == 0)
			{
				return {};
			}
			if (length < buffer.size())
			{
				buffer.resize(length);
				break;
			}
			buffer.resize(buffer.size() * 2);
		}
		return Path(buffer);
#elif AE_PLATFORM_MACOS
		u32 size = 0;
		::_NSGetExecutablePath(nullptr, &size);
		std::string buffer(size, '\0');
		if (::_NSGetExecutablePath(buffer.data(), &size) != 0)
		{
			return {};
		}
		return Path(FromUtf8(buffer.c_str()));
#else
		std::array<char, 4096> buffer{};
		const ssize_t length = ::readlink("/proc/self/exe", buffer.data(), buffer.size() - 1);
		if (length <= 0)
		{
			return {};
		}
		return Path(FromUtf8(std::string_view(buffer.data(), static_cast<usize>(length))));
#endif
	}

	FileSystem::Path FileSystem::GetExecutableDirectory()
	{
		const Path executable = GetExecutablePath();
		return executable.empty() ? GetCurrentWorkingDirectory() : executable.parent_path();
	}

	FileSystem::Path FileSystem::GetCurrentWorkingDirectory()
	{
		std::error_code error;
		const Path result = std::filesystem::current_path(error);
		return error ? Path{} : result;
	}

	bool FileSystem::SetCurrentWorkingDirectory(const Path& path)
	{
		std::error_code error;
		std::filesystem::current_path(path, error);
		return !error;
	}

	FileSystem::Path FileSystem::GetUserDataDirectory()
	{
#if AE_PLATFORM_WINDOWS
		if (const wchar_t* appData = ::_wgetenv(L"APPDATA"))
		{
			return Path(appData) / L"Aegis";
		}
		return GetCurrentWorkingDirectory() / "Saved";
#elif AE_PLATFORM_MACOS
		if (const char* home = std::getenv("HOME"))
		{
			return Path(home) / "Library" / "Application Support" / "Aegis";
		}
		return GetCurrentWorkingDirectory() / "Saved";
#else
		if (const char* xdg = std::getenv("XDG_CONFIG_HOME"))
		{
			return Path(xdg) / "Aegis";
		}
		if (const char* home = std::getenv("HOME"))
		{
			return Path(home) / ".config" / "Aegis";
		}
		return GetCurrentWorkingDirectory() / "Saved";
#endif
	}

	FileSystem::Path FileSystem::GetTempDirectory()
	{
		std::error_code error;
		const Path result = std::filesystem::temp_directory_path(error);
		return error ? GetCurrentWorkingDirectory() : result;
	}

	FileSystem::Path FileSystem::FromUtf8(std::string_view utf8)
	{
		const auto* begin = reinterpret_cast<const char8_t*>(utf8.data());
		return Path(std::u8string(begin, begin + utf8.size()));
	}

	std::string FileSystem::ToUtf8(const Path& path)
	{
		const std::u8string utf8 = path.u8string();
		return { reinterpret_cast<const char*>(utf8.data()), utf8.size() };
	}

	bool FileSystem::TryReadTextFile(const Path& path, std::string& outContents)
	{
		std::ifstream stream(path, std::ios::in | std::ios::binary);
		if (!stream.is_open())
		{
			return false;
		}
		std::string contents((std::istreambuf_iterator<char>(stream)), std::istreambuf_iterator<char>());
		if (stream.bad())
		{
			return false;
		}
		outContents = std::move(contents);
		return true;
	}

	std::string FileSystem::ReadTextFile(const Path& path)
	{
		std::string contents;
		return TryReadTextFile(path, contents) ? contents : std::string{};
	}

	bool FileSystem::WriteTextFile(const Path& path, std::string_view contents, bool createDirectories)
	{
		if (createDirectories && !EnsureParentDirectory(path))
		{
			return false;
		}
		std::ofstream stream(path, std::ios::out | std::ios::binary | std::ios::trunc);
		if (!stream.is_open())
		{
			return false;
		}
		stream.write(contents.data(), static_cast<std::streamsize>(contents.size()));
		stream.flush();
		return stream.good();
	}

	bool FileSystem::TryReadBinaryFile(const Path& path, std::vector<u8>& outContents)
	{
		std::ifstream stream(path, std::ios::in | std::ios::binary | std::ios::ate);
		if (!stream.is_open())
		{
			return false;
		}
		const std::streamsize size = stream.tellg();
		if (size < 0)
		{
			return false;
		}
		outContents.resize(static_cast<usize>(size));
		stream.seekg(0, std::ios::beg);
		if (size > 0)
		{
			stream.read(reinterpret_cast<char*>(outContents.data()), size);
		}
		return stream.good() && !stream.bad();
	}

	bool FileSystem::WriteBinaryFile(const Path& path, const void* data, usize size, bool createDirectories)
	{
		if (createDirectories && !EnsureParentDirectory(path))
		{
			return false;
		}
		std::ofstream stream(path, std::ios::out | std::ios::binary | std::ios::trunc);
		if (!stream.is_open())
		{
			return false;
		}
		if (size > 0)
		{
			stream.write(static_cast<const char*>(data), static_cast<std::streamsize>(size));
		}
		stream.flush();
		return stream.good();
	}

	bool FileSystem::Exists(const Path& path)
	{
		std::error_code error;
		return std::filesystem::exists(path, error) && !error;
	}

	bool FileSystem::IsFile(const Path& path)
	{
		std::error_code error;
		return std::filesystem::is_regular_file(path, error) && !error;
	}

	bool FileSystem::IsDirectory(const Path& path)
	{
		std::error_code error;
		return std::filesystem::is_directory(path, error) && !error;
	}

	bool FileSystem::CreateDirectories(const Path& path)
	{
		std::error_code error;
		std::filesystem::create_directories(path, error);
		return !error || std::filesystem::is_directory(path, error);
	}

	bool FileSystem::EnsureParentDirectory(const Path& path)
	{
		const Path parent = path.parent_path();
		return parent.empty() || CreateDirectories(parent);
	}

	bool FileSystem::RemoveFile(const Path& path)
	{
		std::error_code error;
		return std::filesystem::remove(path, error) && !error;
	}

	bool FileSystem::DeleteDirectory(const Path& path)
	{
		std::error_code error;
		std::filesystem::remove_all(path, error);
		return !error;
	}

	bool FileSystem::CopyFileTo(const Path& source, const Path& destination, bool overwrite)
	{
		std::error_code error;
		std::filesystem::copy_file(source, destination,
			overwrite ? std::filesystem::copy_options::overwrite_existing : std::filesystem::copy_options::none, error);
		return !error;
	}

	bool FileSystem::Rename(const Path& source, const Path& destination)
	{
		std::error_code error;
		std::filesystem::rename(source, destination, error);
		return !error;
	}

	std::vector<FileSystem::Path> FileSystem::ListDirectory(const Path& path, bool recursive, bool includeDirectories)
	{
		std::vector<Path> entries;
		std::error_code error;
		if (recursive)
		{
			std::filesystem::recursive_directory_iterator it(path, error);
			if (error)
			{
				return entries;
			}
			for (; it != std::filesystem::recursive_directory_iterator(); it.increment(error))
			{
				if (error)
				{
					break;
				}
				entries.push_back(it->path());
			}
		}
		else
		{
			std::filesystem::directory_iterator it(path, error);
			if (error)
			{
				return entries;
			}
			for (; it != std::filesystem::directory_iterator(); it.increment(error))
			{
				if (error)
				{
					break;
				}
				entries.push_back(it->path());
			}
		}
		if (!includeDirectories)
		{
			entries.erase(std::remove_if(entries.begin(), entries.end(), [](const Path& entry)
			{
				std::error_code typeError;
				return std::filesystem::is_directory(entry, typeError) && !typeError;
			}), entries.end());
		}

		std::ranges::sort(entries, [](const Path& a, const Path& b)
		{
			return ToUtf8(a) < ToUtf8(b);
		});
		return entries;
	}

	i64 FileSystem::GetFileSize(const Path& path)
	{
		std::error_code error;
		const auto size = std::filesystem::file_size(path, error);
		if (error)
		{
			return -1;
		}
		return static_cast<i64>(size);
	}

	std::optional<std::filesystem::file_time_type> FileSystem::GetLastWriteTime(const Path& path)
	{
		std::error_code error;
		const auto time = std::filesystem::last_write_time(path, error);
		if (error)
		{
			return std::nullopt;
		}
		return time;
	}

	FileSystem::Path FileSystem::Absolute(const Path& path)
	{
		std::error_code error;
		const Path result = std::filesystem::absolute(path, error);
		return error ? path : result;
	}

	FileSystem::Path FileSystem::Normalize(const Path& path)
	{
		Path result = path.lexically_normal();
		if (!result.empty() && result.filename().empty())
		{
			result = result.parent_path();
		}
		return result;
	}

	std::string FileSystem::GetExtension(const Path& path)
	{
		std::string extension = ToUtf8(path.extension());
		if (!extension.empty() && extension.front() == '.')
		{
			extension.erase(extension.begin());
		}
		for (char& c : extension)
		{
			c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
		}
		return extension;
	}

	std::string FileSystem::GetFileName(const Path& path)
	{
		return ToUtf8(path.filename());
	}

	std::string FileSystem::GetStem(const Path& path)
	{
		return ToUtf8(path.stem());
	}

	FileSystem::Path FileSystem::WithExtension(const Path& path, std::string_view extension)
	{
		Path result = path;
		result.replace_extension(FromUtf8(extension));
		return result;
	}
}
