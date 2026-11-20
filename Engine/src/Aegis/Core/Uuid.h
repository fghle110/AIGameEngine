#pragma once

#include "Aegis/Core/Base.h"

#include <array>
#include <string>
#include <string_view>

namespace aegis
{
	// 128-bit identifier used for every persistent engine object (entities,
	// assets, projects). Formatted as the familiar 8-4-4-4-12 hex string.
	class Uuid
	{
	public:
		Uuid() = default;
		explicit Uuid(const std::array<u8, 16>& bytes);

		static Uuid Generate();
		static Uuid FromString(std::string_view text);
		static const Uuid& Null();

		bool IsNull() const;
		explicit operator bool() const { return !IsNull(); }

		std::string ToString() const;

		const std::array<u8, 16>& GetBytes() const { return m_Data; }

		bool operator==(const Uuid& other) const { return m_Data == other.m_Data; }
		bool operator!=(const Uuid& other) const { return !(*this == other); }
		bool operator<(const Uuid& other) const { return m_Data < other.m_Data; }

	private:
		std::array<u8, 16> m_Data{};
	};

	struct UuidHash
	{
		usize operator()(const Uuid& uuid) const noexcept
		{
			// FNV-1a over the 16 bytes.
			usize hash = static_cast<usize>(1469598103934665603ull);
			for (const u8 byte : uuid.GetBytes())
			{
				hash ^= byte;
				hash *= static_cast<usize>(1099511628211ull);
			}
			return hash;
		}
	};
}
