#pragma once

namespace aegis
{
	// Installs a process-wide fatal-error handler that writes a symbolized call
	// stack to stderr and to logs/crash-<timestamp>.log before the process dies.
	// No-op on platforms without SEH (the POSIX build relies on the platform
	// crash reporter).
	void InstallCrashHandler();
	void ShutdownCrashHandler();
}
