#include "Aegis/Core/StringUtils.h"

#include <cctype>
#include <cstdio>

namespace aegis::StringUtils
{
	std::string ToLower(std::string_view text)
	{
		std::string result(text);
		for (char& c : result)
		{
			c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
		}
		return result;
	}

	std::string ToUpper(std::string_view text)
	{
		std::string result(text);
		for (char& c : result)
		{
			c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
		}
		return result;
	}

	std::string_view Trim(std::string_view text)
	{
		while (!text.empty() && std::isspace(static_cast<unsigned char>(text.front())))
		{
			text.remove_prefix(1);
		}
		while (!text.empty() && std::isspace(static_cast<unsigned char>(text.back())))
		{
			text.remove_suffix(1);
		}
		return text;
	}

	std::string TrimCopy(std::string_view text)
	{
		return std::string(Trim(text));
	}

	bool StartsWith(std::string_view text, std::string_view prefix)
	{
		return text.size() >= prefix.size() && text.substr(0, prefix.size()) == prefix;
	}

	bool EndsWith(std::string_view text, std::string_view suffix)
	{
		return text.size() >= suffix.size() && text.substr(text.size() - suffix.size()) == suffix;
	}

	bool Contains(std::string_view text, std::string_view needle)
	{
		return text.find(needle) != std::string_view::npos;
	}

	bool EqualsIgnoreCase(std::string_view a, std::string_view b)
	{
		if (a.size() != b.size())
		{
			return false;
		}
		for (usize i = 0; i < a.size(); ++i)
		{
			if (std::tolower(static_cast<unsigned char>(a[i])) != std::tolower(static_cast<unsigned char>(b[i])))
			{
				return false;
			}
		}
		return true;
	}

	std::vector<std::string> Split(std::string_view text, char delimiter, bool keepEmpty)
	{
		std::vector<std::string> parts;
		usize start = 0;
		while (start <= text.size())
		{
			const usize end = text.find(delimiter, start);
			const std::string_view part = text.substr(start, end == std::string_view::npos ? std::string_view::npos : end - start);
			if (keepEmpty || !part.empty())
			{
				parts.emplace_back(part);
			}
			if (end == std::string_view::npos)
			{
				break;
			}
			start = end + 1;
		}
		return parts;
	}

	std::vector<std::string> SplitLines(std::string_view text)
	{
		std::vector<std::string> lines;
		usize start = 0;
		while (start <= text.size())
		{
			usize end = text.find('\n', start);
			if (end == std::string_view::npos)
			{
				end = text.size();
			}
			std::string_view line = text.substr(start, end - start);
			if (!line.empty() && line.back() == '\r')
			{
				line.remove_suffix(1);
			}
			lines.emplace_back(line);
			if (end == text.size())
			{
				break;
			}
			start = end + 1;
		}
		return lines;
	}

	std::string Join(const std::vector<std::string>& parts, std::string_view separator)
	{
		std::string result;
		for (usize i = 0; i < parts.size(); ++i)
		{
			if (i != 0)
			{
				result.append(separator);
			}
			result.append(parts[i]);
		}
		return result;
	}

	std::string ReplaceAll(std::string_view text, std::string_view from, std::string_view to)
	{
		if (from.empty())
		{
			return std::string(text);
		}
		std::string result;
		result.reserve(text.size());
		usize start = 0;
		for (;;)
		{
			const usize position = text.find(from, start);
			if (position == std::string_view::npos)
			{
				result.append(text.substr(start));
				break;
			}
			result.append(text.substr(start, position - start));
			result.append(to);
			start = position + from.size();
		}
		return result;
	}

	std::string ToHex(const void* data, usize size)
	{
		static constexpr char c_HexDigits[] = "0123456789abcdef";
		const auto* bytes = static_cast<const u8*>(data);
		std::string result;
		result.reserve(size * 2);
		for (usize i = 0; i < size; ++i)
		{
			result.push_back(c_HexDigits[bytes[i] >> 4]);
			result.push_back(c_HexDigits[bytes[i] & 0x0F]);
		}
		return result;
	}

	std::vector<std::string> SplitPath(std::string_view path)
	{
		std::vector<std::string> segments;
		usize start = 0;
		for (usize i = 0; i <= path.size(); ++i)
		{
			if (i == path.size() || path[i] == '.')
			{
				if (i > start)
				{
					segments.emplace_back(path.substr(start, i - start));
				}
				start = i + 1;
			}
		}
		return segments;
	}
}
