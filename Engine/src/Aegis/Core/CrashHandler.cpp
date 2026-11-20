#include "Aegis/Core/CrashHandler.h"

#include "Aegis/Core/Base.h"
#include "Aegis/Core/FileSystem.h"

#include <cstdio>
#include <ctime>
#include <exception>
#include <mutex>
#include <string>

#if AE_PLATFORM_WINDOWS
	#include <windows.h>
	#include <dbghelp.h>
	#include <process.h>
#endif

namespace aegis
{
#if AE_PLATFORM_WINDOWS
	namespace
	{
		std::mutex s_CrashMutex;
		bool s_SymbolsInitialized = false;
		std::string s_ReportPath;

		std::string Timestamp()
		{
			const std::time_t now = std::time(nullptr);
			std::tm tm{};
			localtime_s(&tm, &now);
			char buffer[64]{};
			std::strftime(buffer, sizeof(buffer), "%Y-%m-%d-%H-%M-%S", &tm);
			return buffer;
		}

		void WriteStack(std::FILE* stream, CONTEXT* context, int framesToSkip)
		{
			HANDLE process = GetCurrentProcess();
			HANDLE thread = GetCurrentThread();

			STACKFRAME64 frame{};
			const DWORD machine = IMAGE_FILE_MACHINE_AMD64;
			frame.AddrPC.Offset = context->Rip;
			frame.AddrPC.Mode = AddrModeFlat;
			frame.AddrFrame.Offset = context->Rbp;
			frame.AddrFrame.Mode = AddrModeFlat;
			frame.AddrStack.Offset = context->Rsp;
			frame.AddrStack.Mode = AddrModeFlat;

			char symbolBuffer[sizeof(SYMBOL_INFO) + 512]{};
			auto* symbol = reinterpret_cast<SYMBOL_INFO*>(symbolBuffer);
			symbol->SizeOfStruct = sizeof(SYMBOL_INFO);
			symbol->MaxNameLen = 511;

			IMAGEHLP_LINE64 line{};
			line.SizeOfStruct = sizeof(IMAGEHLP_LINE64);

			constexpr int c_MaxFrames = 64;
			for (int i = 0; i < c_MaxFrames; ++i)
			{
				if (StackWalk64(machine, process, thread, &frame, context, nullptr, SymFunctionTableAccess64,
						SymGetModuleBase64, nullptr) == FALSE)
				{
					break;
				}
				if (frame.AddrPC.Offset == 0)
				{
					break;
				}
				if (i < framesToSkip)
				{
					continue;
				}

				DWORD64 displacement = 0;
				const DWORD64 address = frame.AddrPC.Offset;
				std::string lineText;
				const bool haveSymbol = SymFromAddr(process, address, &displacement, symbol) != FALSE;
				DWORD lineDisplacement = 0;
				const bool haveLine = SymGetLineFromAddr64(process, address, &lineDisplacement, &line) != FALSE;

				if (haveLine)
				{
					lineText = std::format("  #{} {} ({}:{})", i, haveSymbol ? symbol->Name : "<unknown>",
						line.FileName, static_cast<unsigned>(line.LineNumber));
				}
				else if (haveSymbol)
				{
					lineText = std::format("  #{} {} + 0x{:x}", i, symbol->Name, static_cast<unsigned long long>(displacement));
				}
				else
				{
					lineText = std::format("  #{} 0x{:016x}", i, static_cast<unsigned long long>(address));
				}

				std::fprintf(stream, "%s\n", lineText.c_str());
				std::fflush(stream);
			}
		}

		LONG WINAPI CrashFilter(EXCEPTION_POINTERS* exceptionInfo)
		{
			std::lock_guard lock(s_CrashMutex);

			if (!s_SymbolsInitialized)
			{
				SymSetOptions(SYMOPT_UNDNAME | SYMOPT_DEFERRED_LOADS | SYMOPT_LOAD_LINES);
				SymInitialize(GetCurrentProcess(), nullptr, TRUE);
				s_SymbolsInitialized = true;
			}

			const DWORD code = exceptionInfo != nullptr && exceptionInfo->ExceptionRecord != nullptr
								   ? exceptionInfo->ExceptionRecord->ExceptionCode
								   : 0;
			const void* address = exceptionInfo != nullptr && exceptionInfo->ExceptionRecord != nullptr
									  ? exceptionInfo->ExceptionRecord->ExceptionAddress
									  : nullptr;
			std::fprintf(stderr, "\nAegis crash: exception 0x%08lx at %p\n", static_cast<unsigned long>(code), address);

			std::FILE* file = s_ReportPath.empty() ? nullptr : std::fopen(s_ReportPath.c_str(), "w");
			const auto report = [&](std::FILE* stream)
			{
				std::fprintf(stream, "Aegis crash report\n");
				std::fprintf(stream, "Exception: 0x%08lx at %p\n", static_cast<unsigned long>(code), address);
				if (exceptionInfo != nullptr)
				{
					WriteStack(stream, exceptionInfo->ContextRecord, 0);
				}
			};
			report(stderr);
			if (file != nullptr)
			{
				report(file);
				std::fclose(file);
			}
			return EXCEPTION_EXECUTE_HANDLER;
		}

		void OnTerminate()
		{
			std::fprintf(stderr, "Aegis: std::terminate called\n");
			if (auto exception = std::current_exception())
			{
				try
				{
					std::rethrow_exception(exception);
				}
				catch (const std::exception& error)
				{
					std::fprintf(stderr, "  what(): %s\n", error.what());
				}
				catch (...)
				{
					std::fprintf(stderr, "  unknown exception\n");
				}
			}
			std::fflush(stderr);
			std::abort();
		}
	}
#endif

	void InstallCrashHandler()
	{
#if AE_PLATFORM_WINDOWS
		const auto logDirectory = FileSystem::GetCurrentWorkingDirectory() / "logs";
		FileSystem::CreateDirectories(logDirectory);
		s_ReportPath = FileSystem::ToUtf8(logDirectory / ("crash-" + Timestamp() + ".log"));

		SymSetOptions(SYMOPT_UNDNAME | SYMOPT_DEFERRED_LOADS | SYMOPT_LOAD_LINES);
		s_SymbolsInitialized = SymInitialize(GetCurrentProcess(), nullptr, TRUE) != FALSE;

		SetUnhandledExceptionFilter(CrashFilter);
		std::set_terminate(OnTerminate);
#endif
	}

	void ShutdownCrashHandler()
	{
#if AE_PLATFORM_WINDOWS
		if (s_SymbolsInitialized)
		{
			SymCleanup(GetCurrentProcess());
			s_SymbolsInitialized = false;
		}
#endif
	}
}
