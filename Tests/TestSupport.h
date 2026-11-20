#pragma once

// Shared test prelude. MSVC declares the std::string_view stream operators only
// when <ostream> has been included already, and doctest stringifies
// string_view values in failed assertions, so every test translation unit
// includes this header before <doctest/doctest.h>.

#include <cstdint>
#include <ostream>
#include <sstream>
#include <string>
#include <string_view>
#include <vector>
