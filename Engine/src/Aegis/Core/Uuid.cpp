#include "Aegis/Core/Uuid.h"

#include <chrono>
#include <cstdio>
#include <random>

namespace aegis
{
	namespace
	{
		u64 SplitMix64(u64& state)
		{
			u64 z = (state += 0x9E3779B97F4A7C15ull);
			z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ull;
			z = (z ^ (z >> 27)) * 0x94D049BB133111EBull;
			return z ^ (z >> 31);
		}

		u64 MakeSeed()
		{
			std::random_device device;
			const u64 randomPart = (static_cast<u64>(device()) << 32) ^ static_cast<u64>(device());
			const u64 timePart = static_cast<u64>(
				std::chrono::high_resolution_clock::now().time_since_epoch().count());
			const u64 addressPart = reinterpret_cast<u64>(&device);
			return randomPart ^ timePart ^ (addressPart * 0x9E3779B97F4A7C15ull);
		}

		thread_local u64 t_State = MakeSeed();
	}

	Uuid::Uuid(const std::array<u8, 16>& bytes)
		: m_Data(bytes)
	{
	}

	Uuid Uuid::Generate()
	{
		std::array<u8, 16> bytes{};
		const u64 high = SplitMix64(t_State);
		const u64 low = SplitMix64(t_State);
		for (usize i = 0; i < 8; ++i)
		{
			bytes[i] = static_cast<u8>((high >> (56 - i * 8)) & 0xFF);
			bytes[i + 8] = static_cast<u8>((low >> (56 - i * 8)) & 0xFF);
		}
		// Mark as RFC 4122 version 4 (random) for interoperability with tools.
		bytes[6] = static_cast<u8>((bytes[6] & 0x0F) | 0x40);
		bytes[8] = static_cast<u8>((bytes[8] & 0x3F) | 0x80);
		return Uuid(bytes);
	}

	Uuid Uuid::FromString(std::string_view text)
	{
		std::array<u8, 16> bytes{};
		usize nibbleIndex = 0;
		int highNibble = -1;

		const auto hexValue = [](char c) -> int
		{
			if (c >= '0' && c <= '9')
			{
				return c - '0';
			}
			if (c >= 'a' && c <= 'f')
			{
				return c - 'a' + 10;
			}
			if (c >= 'A' && c <= 'F')
			{
				return c - 'A' + 10;
			}
			return -1;
		};

		for (const char c : text)
		{
			if (c == '-')
			{
				continue;
			}
			const int value = hexValue(c);
			if (value < 0)
			{
				return Null();
			}
			if (highNibble < 0)
			{
				highNibble = value;
			}
			else
			{
				if (nibbleIndex >= 16)
				{
					return Null(); // more than 32 hexadecimal digits
				}
				bytes[nibbleIndex] = static_cast<u8>((highNibble << 4) | value);
				highNibble = -1;
				++nibbleIndex;
			}
		}

		if (nibbleIndex != 16 || highNibble >= 0)
		{
			return Null();
		}
		return Uuid(bytes);
	}

	const Uuid& Uuid::Null()
	{
		static const Uuid s_Null;
		return s_Null;
	}

	bool Uuid::IsNull() const
	{
		for (const u8 byte : m_Data)
		{
			if (byte != 0)
			{
				return false;
			}
		}
		return true;
	}

	std::string Uuid::ToString() const
	{
		char buffer[37]{};
		std::snprintf(buffer, sizeof(buffer),
			"%02x%02x%02x%02x-%02x%02x-%02x%02x-%02x%02x-%02x%02x%02x%02x%02x%02x", m_Data[0], m_Data[1], m_Data[2],
			m_Data[3], m_Data[4], m_Data[5], m_Data[6], m_Data[7], m_Data[8], m_Data[9], m_Data[10], m_Data[11],
			m_Data[12], m_Data[13], m_Data[14], m_Data[15]);
		return buffer;
	}
}
