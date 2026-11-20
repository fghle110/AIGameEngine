#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <doctest/doctest.h>

#include "Aegis/Core/Log.h"

// One-time process setup shared by every test executable.
static struct TestEnvironment
{
	TestEnvironment() { aegis::Log::Init(""); }
} g_TestEnvironment;
