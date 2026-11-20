#pragma once

#include "Aegis/Core/Base.h"

#include <string>

namespace aegis
{
// Dependency-free HTTP/1.1 server that exposes the editor command bus to
// automation. One background thread accepts connections and reads requests; it
// never executes a command. Each request is enqueued and the network thread
// blocks until the editor main thread has produced the response in
// ProcessPending(), so command handlers always run on the thread that owns the
// scene and render state.
//
// The listener binds to 127.0.0.1 only: the API is a local developer tool and
// must not be reachable from the network. Stop() aborts pending requests,
// closes the listener and joins the thread.
class HttpServer
{
public:
	// Upper bound on requests executed per ProcessPending() call so one busy
	// automation client cannot stall the editor frame.
	static constexpr usize c_MaxRequestsPerTick = 8;
	// Request bodies larger than this are rejected with 413 (Content Too Large).
	static constexpr usize c_MaxBodyBytes = 1024 * 1024;
	// Request line plus headers must fit in this many bytes.
	static constexpr usize c_MaxHeaderBytes = 16 * 1024;

	HttpServer();
	~HttpServer();

	HttpServer(const HttpServer&) = delete;
	HttpServer& operator=(const HttpServer&) = delete;

	// Binds to 127.0.0.1 and starts the network thread. Port 0 asks the OS for
	// an ephemeral port; the bound port is reported by GetPort(). Returns false
	// (and logs the reason) when the socket cannot be created or bound.
	bool Start(u16 port);
	void Stop();

	bool IsRunning() const;
	u16 GetPort() const;

	// Executes at most c_MaxRequestsPerTick queued requests through the command
	// bus and hands their responses to the waiting network thread. Must be called
	// from the editor main thread, once per frame.
	void ProcessPending();

	// Human-readable state for logs, /health and the editor UI.
	std::string GetStatusText();

private:
	struct Impl;
	Scope<Impl> m_Impl;
};
} // namespace aegis
