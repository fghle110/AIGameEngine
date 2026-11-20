#pragma once

// Core types, smart pointer aliases and assertion helpers shared by the whole
// engine. This header is intentionally light: everything else depends on it.

#include <algorithm>
#include <array>
#include <cassert>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <format>
#include <functional>
#include <limits>
#include <memory>
#include <string>
#include <string_view>
#include <unordered_map>
#include <utility>
#include <vector>

#define AE_EXPAND_MACRO(x) x
#define AE_STRINGIFY_MACRO(x) #x

// Single bit of a 64-bit mask. Matches the Hazel developer guide.
#define BIT(x) (1ull << (x))

// Binds a member function as an event callback.
#define AE_BIND_EVENT_FN(fn) [this](auto&&... args) -> decltype(auto) { return this->fn(std::forward<decltype(args)>(args)...); }

#if defined(_MSC_VER)
	#define AE_DEBUGBREAK() __debugbreak()
#elif defined(__clang__) || defined(__GNUC__)
	#define AE_DEBUGBREAK() __builtin_trap()
#else
	#include <csignal>
	#define AE_DEBUGBREAK() std::raise(SIGTRAP)
#endif

// Assertions are compiled in for every non-Dist build and can also be forced on
// for shipping builds while diagnosing a problem.
#if defined(AE_DEBUG) || defined(AE_ENABLE_ASSERTS)
	#define AE_ASSERT_ENABLED 1
#else
	#define AE_ASSERT_ENABLED 0
#endif

namespace aegis
{
	using i8 = std::int8_t;
	using i16 = std::int16_t;
	using i32 = std::int32_t;
	using i64 = std::int64_t;
	using u8 = std::uint8_t;
	using u16 = std::uint16_t;
	using u32 = std::uint32_t;
	using u64 = std::uint64_t;
	using f32 = float;
	using f64 = double;
	using usize = std::size_t;
	using isize = std::ptrdiff_t;

	using Byte = std::byte;

	// Unique ownership by default, shared ownership only when it is genuinely
	// needed. Both aliases mirror Hazel's Scope/Ref convention.
	template<typename T>
	using Scope = std::unique_ptr<T>;

	template<typename T, typename... Args>
	constexpr Scope<T> CreateScope(Args&&... args)
	{
		return std::make_unique<T>(std::forward<Args>(args)...);
	}

	template<typename T>
	using Ref = std::shared_ptr<T>;

	template<typename T, typename... Args>
	constexpr Ref<T> CreateRef(Args&&... args)
	{
		return std::make_shared<T>(std::forward<Args>(args)...);
	}

	template<typename T>
	using WeakRef = std::weak_ptr<T>;

	constexpr std::string_view c_InvalidString = "<invalid>";

	template<typename T, usize N>
	constexpr u32 ArrayCount(const T (&)[N]) noexcept
	{
		return static_cast<u32>(N);
	}

	// Clamps a value into [low, high] without relying on non-standard helpers.
	template<typename T>
	constexpr T Clamp(T value, T low, T high) noexcept
	{
		return value < low ? low : (value > high ? high : value);
	}

	template<typename T>
	constexpr T Min(T a, T b) noexcept
	{
		return a < b ? a : b;
	}

	template<typename T>
	constexpr T Max(T a, T b) noexcept
	{
		return a > b ? a : b;
	}

	constexpr u32 RoundUpToPowerOfTwo(u32 value) noexcept
	{
		if (value <= 1)
		{
			return 1;
		}
		--value;
		value |= value >> 1;
		value |= value >> 2;
		value |= value >> 4;
		value |= value >> 8;
		value |= value >> 16;
		return value + 1;
	}
}

namespace aegis::detail
{
	[[noreturn]] void AssertFailed(const char* expression, std::string_view message, const char* file, int line);

	inline std::string FormatAssertMessage()
	{
		return {};
	}

	template<typename... Args>
	std::string FormatAssertMessage(std::format_string<Args...> format, Args&&... args)
	{
		return std::format(format, std::forward<Args>(args)...);
	}
}

#if AE_ASSERT_ENABLED
	#define AE_ASSERT(condition, ...)                                                                                    \
		do                                                                                                               \
		{                                                                                                                \
			if (!(static_cast<bool>(condition)))                                                                         \
			{                                                                                                            \
				::aegis::detail::AssertFailed(#condition, ::aegis::detail::FormatAssertMessage(__VA_ARGS__), __FILE__,     \
					__LINE__);                                                                                           \
			}                                                                                                            \
		} while (false)
#else
	#define AE_ASSERT(condition, ...)                                                                                    \
		do                                                                                                               \
		{                                                                                                                \
			(void)sizeof(condition);                                                                                     \
		} while (false)
#endif
