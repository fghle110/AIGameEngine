#pragma once

#include "Aegis/Core/Base.h"

#include <string>
#include <string_view>
#include <vector>

namespace aegis::StringUtils
{
	std::string ToLower(std::string_view text);
	std::string ToUpper(std::string_view text);

	std::string_view Trim(std::string_view text);
	std::string TrimCopy(std::string_view text);

	bool StartsWith(std::string_view text, std::string_view prefix);
	bool EndsWith(std::string_view text, std::string_view suffix);
	bool Contains(std::string_view text, std::string_view needle);
	bool EqualsIgnoreCase(std::string_view a, std::string_view b);

	std::vector<std::string> Split(std::string_view text, char delimiter, bool keepEmpty = false);
	std::vector<std::string> SplitLines(std::string_view text);
	std::string Join(const std::vector<std::string>& parts, std::string_view separator);

	std::string ReplaceAll(std::string_view text, std::string_view from, std::string_view to);
	std::string ToHex(const void* data, usize size);

	// Splits "a.b.c[0]" style paths used by the AI command API.
	std::vector<std::string> SplitPath(std::string_view path);
}
