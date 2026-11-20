#include "Aegis/Commands/HttpServer.h"

#if defined(_WIN32)
#include <winsock2.h>
#include <ws2tcpip.h>
#else
#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <unistd.h>
#endif

#include "Aegis/Commands/CommandBus.h"
#include "Aegis/Core/Config.h"
#include "Aegis/Core/Json.h"
#include "Aegis/Core/Log.h"
#include "Aegis/Core/StringUtils.h"

#include <algorithm>
#include <atomic>
#include <cerrno>
#include <charconv>
#include <chrono>
#include <condition_variable>
#include <cstring>
#include <deque>
#include <exception>
#include <mutex>
#include <optional>
#include <thread>
#include <vector>

namespace aegis
{
namespace
{
#if defined(_WIN32)
using SocketHandle = SOCKET;
constexpr SocketHandle c_InvalidSocket = INVALID_SOCKET;
#else
using SocketHandle = int;
constexpr SocketHandle c_InvalidSocket = -1;
#endif

constexpr int c_AcceptPollMs = 100;
constexpr int c_SocketTimeoutMs = 5000;
constexpr int c_RequestReadTimeoutMs = 10000;
constexpr int c_ResponseTimeoutMs = 30000;
#if defined(MSG_NOSIGNAL)
constexpr int c_SendFlags = MSG_NOSIGNAL;
#else
constexpr int c_SendFlags = 0;
#endif

#if defined(_WIN32)
std::mutex s_WinsockMutex;
u32 s_WinsockReferences = 0;

bool AcquireWinsock()
{
	std::lock_guard lock(s_WinsockMutex);
	if (s_WinsockReferences == 0)
	{
		WSADATA data{};
		if (WSAStartup(MAKEWORD(2, 2), &data) != 0)
		{
			return false;
		}
	}
	++s_WinsockReferences;
	return true;
}

void ReleaseWinsock()
{
	std::lock_guard lock(s_WinsockMutex);
	if (s_WinsockReferences == 0)
	{
		return;
	}
	--s_WinsockReferences;
	if (s_WinsockReferences == 0)
	{
		WSACleanup();
	}
}
#else
bool AcquireWinsock()
{
	return true;
}

void ReleaseWinsock() {}
#endif

std::string LastSocketError()
{
#if defined(_WIN32)
	return std::format("Winsock error {}", static_cast<int>(WSAGetLastError()));
#else
	return std::format("socket error {} ({})", errno, std::strerror(errno));
#endif
}

void CloseSocket(SocketHandle socket)
{
	if (socket == c_InvalidSocket)
	{
		return;
	}
#if defined(_WIN32)
	::closesocket(socket);
#else
	::close(socket);
#endif
}

// Wakes a blocking recv() without closing the send direction: a request that
// was already read still needs its 503 response to reach the client.
void ShutdownSocketReceive(SocketHandle socket)
{
	if (socket == c_InvalidSocket)
	{
		return;
	}
#if defined(_WIN32)
	::shutdown(socket, SD_RECEIVE);
#else
	::shutdown(socket, SHUT_RD);
#endif
}

void SetSocketTimeout(SocketHandle socket, int timeoutMs)
{
#if defined(_WIN32)
	const DWORD timeout = static_cast<DWORD>(timeoutMs);
	const char* value = reinterpret_cast<const char*>(&timeout);
	const int size = sizeof(timeout);
	::setsockopt(socket, SOL_SOCKET, SO_RCVTIMEO, value, size);
	::setsockopt(socket, SOL_SOCKET, SO_SNDTIMEO, value, size);
#else
	timeval timeout{};
	timeout.tv_sec = timeoutMs / 1000;
	timeout.tv_usec = (timeoutMs % 1000) * 1000;
	::setsockopt(socket, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout));
	::setsockopt(socket, SOL_SOCKET, SO_SNDTIMEO, &timeout, sizeof(timeout));
#endif
}

bool SendAll(SocketHandle socket, std::string_view data)
{
	usize offset = 0;
	while (offset < data.size())
	{
		const int sent = ::send(socket, data.data() + offset, static_cast<int>(data.size() - offset), c_SendFlags);
		if (sent <= 0)
		{
			return false;
		}
		offset += static_cast<usize>(sent);
	}
	return true;
}

const char* ReasonPhrase(int status)
{
	switch (status)
	{
	case 200:
		return "OK";
	case 400:
		return "Bad Request";
	case 404:
		return "Not Found";
	case 408:
		return "Request Timeout";
	case 413:
		return "Content Too Large";
	case 431:
		return "Request Header Fields Too Large";
	case 500:
		return "Internal Server Error";
	case 503:
		return "Service Unavailable";
	default:
		break;
	}
	return "Unknown";
}

std::string
BuildHttpResponse(int status, std::string_view contentType, std::string_view body, std::string_view extraHeaders = {})
{
	std::string response;
	response.reserve(body.size() + 256);
	response += std::format("HTTP/1.1 {} {}\r\n", status, ReasonPhrase(status));
	response += "Connection: close\r\n";
	response += std::format("Content-Type: {}\r\n", contentType);
	response += std::format("Content-Length: {}\r\n", body.size());
	response += "Cache-Control: no-store\r\n";
	response += extraHeaders;
	response += "\r\n";
	response += body;
	return response;
}

std::string BuildErrorBody(std::string_view message)
{
	JsonValue error = JsonValue::MakeObject();
	error.Set("ok", false);
	error.Set("error", std::string(message));
	return error.Dump();
}

std::string BuildErrorResponse(int status, std::string_view message)
{
	return BuildHttpResponse(status, "application/json", BuildErrorBody(message));
}
} // namespace

struct HttpServer::Impl
{
	// One in-flight request. The network thread owns the socket, fills the
	// request fields, then waits on the condition variable until the main thread
	// has executed the command and stored the response here.
	struct PendingRequest
	{
		std::string Method;
		std::string Target;
		std::string Body;

		std::mutex Mutex;
		std::condition_variable Cv;
		bool Completed = false;
		bool Aborted = false;
		std::string Response;
	};

	std::thread NetworkThread;
	std::atomic<bool> Running{ false };
	std::atomic<bool> StopRequested{ false };
	u16 Port = 0;
	bool WinsockAcquired = false;
	SocketHandle Listener = c_InvalidSocket;

	// The single active client socket, shutdown by Stop() so a thread blocked in
	// recv() cannot delay shutdown.
	std::mutex ClientMutex;
	SocketHandle ActiveClient = c_InvalidSocket;

	std::mutex QueueMutex;
	std::condition_variable QueueCv;
	std::deque<Ref<PendingRequest>> Queue;

	void Run();
	void HandleConnection(SocketHandle client);
	bool ReadRequest(SocketHandle client, PendingRequest& request, int& outStatus, std::string& outError);
	std::string HandleRequest(const PendingRequest& request);
	usize GetPendingCount();
};

void HttpServer::Impl::Run()
{
	while (!StopRequested.load(std::memory_order_relaxed))
	{
		fd_set readSet;
		FD_ZERO(&readSet);
		FD_SET(Listener, &readSet);

		timeval timeout{};
		timeout.tv_sec = 0;
		timeout.tv_usec = c_AcceptPollMs * 1000;
		const int ready = ::select(static_cast<int>(Listener) + 1, &readSet, nullptr, nullptr, &timeout);

		if (StopRequested.load(std::memory_order_relaxed))
		{
			break;
		}
		if (ready < 0)
		{
			AE_CORE_ERROR("HTTP command server: select() failed: {}", LastSocketError());
			break;
		}
		if (ready == 0)
		{
			continue;
		}

		sockaddr_in address{};
#if defined(_WIN32)
		int addressLength = sizeof(address);
#else
		socklen_t addressLength = sizeof(address);
#endif
		const SocketHandle client = ::accept(Listener, reinterpret_cast<sockaddr*>(&address), &addressLength);
		if (client == c_InvalidSocket)
		{
			if (!StopRequested.load(std::memory_order_relaxed))
			{
				AE_CORE_ERROR("HTTP command server: accept() failed: {}", LastSocketError());
			}
			continue;
		}

		SetSocketTimeout(client, c_SocketTimeoutMs);
#if defined(SO_NOSIGPIPE)
		const int noSigPipe = 1;
		::setsockopt(client, SOL_SOCKET, SO_NOSIGPIPE, &noSigPipe, sizeof(noSigPipe));
#endif
		{
			std::lock_guard lock(ClientMutex);
			ActiveClient = client;
		}
		HandleConnection(client);
		{
			std::lock_guard lock(ClientMutex);
			ActiveClient = c_InvalidSocket;
		}
		CloseSocket(client);
	}
}

bool HttpServer::Impl::ReadRequest(SocketHandle client, PendingRequest& request, int& outStatus, std::string& outError)
{
	outStatus = 200;
	std::string buffer;
	buffer.reserve(4096);

	const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(c_RequestReadTimeoutMs);
	constexpr usize c_HeaderTerminatorLength = 4;
	const std::string_view c_HeaderTerminator = "\r\n\r\n";
	usize headerEnd = std::string::npos;

	char chunk[4096];
	while (headerEnd == std::string::npos)
	{
		if (std::chrono::steady_clock::now() > deadline)
		{
			outStatus = 408;
			outError = "timed out while reading the request headers";
			return false;
		}

		const int received = ::recv(client, chunk, sizeof(chunk), 0);
		if (received == 0)
		{
			outStatus = StopRequested.load() ? 503 : 400;
			outError = StopRequested.load() ? "the command server is stopping"
			                                : "the connection closed before the request was complete";
			return false;
		}
		if (received < 0)
		{
			outStatus = StopRequested.load() ? 503 : 408;
			outError = StopRequested.load() ? "the command server is stopping"
			                                : std::format("failed to read the request: {}", LastSocketError());
			return false;
		}

		buffer.append(chunk, static_cast<usize>(received));
		headerEnd = buffer.find(c_HeaderTerminator);
		if (headerEnd == std::string::npos && buffer.size() > c_MaxHeaderBytes)
		{
			outStatus = 431;
			outError = std::format("request headers exceed {} bytes", c_MaxHeaderBytes);
			return false;
		}
		if (headerEnd != std::string::npos && headerEnd + c_HeaderTerminatorLength > c_MaxHeaderBytes)
		{
			outStatus = 431;
			outError = std::format("request headers exceed {} bytes", c_MaxHeaderBytes);
			return false;
		}
	}

	const std::string_view headers(buffer.data(), headerEnd);
	const usize requestLineEnd = headers.find("\r\n");
	const std::string_view requestLine =
	    headers.substr(0, requestLineEnd == std::string_view::npos ? headers.size() : requestLineEnd);
	const usize firstSpace = requestLine.find(' ');
	const usize secondSpace =
	    firstSpace == std::string_view::npos ? std::string_view::npos : requestLine.find(' ', firstSpace + 1);
	if (firstSpace == std::string_view::npos || secondSpace == std::string_view::npos)
	{
		outStatus = 400;
		outError = "malformed request line (expected: METHOD /path HTTP/1.1)";
		return false;
	}

	const std::string_view method = requestLine.substr(0, firstSpace);
	const std::string_view target = requestLine.substr(firstSpace + 1, secondSpace - firstSpace - 1);
	const std::string_view version = requestLine.substr(secondSpace + 1);
	if (version != "HTTP/1.1" && version != "HTTP/1.0")
	{
		outStatus = 400;
		outError = std::format("unsupported HTTP version '{}' (this server speaks HTTP/1.1)", version);
		return false;
	}
	if (target.empty() || target.front() != '/')
	{
		outStatus = 400;
		outError = std::format("malformed request target '{}' (expected an origin-form path such as /health)", target);
		return false;
	}
	if (method != "GET" && method != "POST")
	{
		outStatus = 400;
		outError = std::format("unsupported method '{}' (supported: GET, POST)", method);
		return false;
	}

	usize contentLength = 0;
	bool hasContentLength = false;
	bool usesChunkedEncoding = false;
	usize cursor = requestLineEnd == std::string_view::npos ? headers.size() : requestLineEnd + 2;
	while (cursor < headers.size())
	{
		const usize lineEnd = headers.find("\r\n", cursor);
		const std::string_view line =
		    headers.substr(cursor, (lineEnd == std::string_view::npos ? headers.size() : lineEnd) - cursor);
		cursor = lineEnd == std::string_view::npos ? headers.size() : lineEnd + 2;
		if (line.empty())
		{
			continue;
		}

		const usize colon = line.find(':');
		if (colon == std::string_view::npos)
		{
			outStatus = 400;
			outError = std::format("malformed header line '{}' (expected 'Name: value')", line);
			return false;
		}

		const std::string_view name = line.substr(0, colon);
		const std::string_view value = StringUtils::Trim(line.substr(colon + 1));
		if (StringUtils::EqualsIgnoreCase(name, "content-length"))
		{
			u64 parsedLength = 0;
			const auto [end, error] = std::from_chars(value.data(), value.data() + value.size(), parsedLength);
			if (error != std::errc{} || end != value.data() + value.size())
			{
				outStatus = 400;
				outError = std::format("invalid Content-Length '{}'", value);
				return false;
			}
			if (parsedLength > c_MaxBodyBytes)
			{
				outStatus = 413;
				outError =
				    std::format("request body of {} bytes exceeds the {} byte limit", parsedLength, c_MaxBodyBytes);
				return false;
			}
			contentLength = static_cast<usize>(parsedLength);
			hasContentLength = true;
		}
		else if (StringUtils::EqualsIgnoreCase(name, "transfer-encoding") &&
		         StringUtils::Contains(StringUtils::ToLower(value), "chunked"))
		{
			usesChunkedEncoding = true;
		}
	}

	if (usesChunkedEncoding)
	{
		outStatus = 400;
		outError = "chunked request bodies are not supported; send Content-Length";
		return false;
	}
	if (!hasContentLength)
	{
		contentLength = 0;
	}

	request.Method = std::string(method);
	request.Target = std::string(target);
	request.Body.assign(buffer.data() + headerEnd + c_HeaderTerminatorLength,
	                    buffer.size() - headerEnd - c_HeaderTerminatorLength);

	while (request.Body.size() < contentLength)
	{
		if (std::chrono::steady_clock::now() > deadline)
		{
			outStatus = 408;
			outError = "timed out while reading the request body";
			return false;
		}
		const int received = ::recv(client, chunk, sizeof(chunk), 0);
		if (received <= 0)
		{
			outStatus = StopRequested.load() ? 503 : 408;
			outError = StopRequested.load() ? "the command server is stopping"
			                                : "the connection closed before the request body was complete";
			return false;
		}
		request.Body.append(chunk, static_cast<usize>(received));
	}
	// Extra bytes are ignored: this server answers one request per connection.
	request.Body.resize(contentLength);
	return true;
}

void HttpServer::Impl::HandleConnection(SocketHandle client)
{
	PendingRequest request;
	int status = 200;
	std::string error;
	if (!ReadRequest(client, request, status, error))
	{
		SendAll(client, BuildErrorResponse(status == 200 ? 400 : status, error));
		return;
	}
	if (StopRequested.load())
	{
		SendAll(client, BuildErrorResponse(503, "the command server is stopping"));
		return;
	}

	Ref<PendingRequest> pending = CreateRef<PendingRequest>();
	pending->Method = std::move(request.Method);
	pending->Target = std::move(request.Target);
	pending->Body = std::move(request.Body);
	{
		std::lock_guard lock(QueueMutex);
		Queue.push_back(pending);
	}
	QueueCv.notify_one();

	std::string response;
	bool aborted = false;
	{
		std::unique_lock lock(pending->Mutex);
		pending->Cv.wait_for(lock,
		                     std::chrono::milliseconds(c_ResponseTimeoutMs),
		                     [&pending] { return pending->Completed || pending->Aborted; });
		if (pending->Completed)
		{
			response = std::move(pending->Response);
		}
		else
		{
			pending->Aborted = true;
			aborted = true;
		}
	}

	if (aborted)
	{
		response =
		    BuildErrorResponse(503,
		                       StopRequested.load() ? "the command server is stopping"
		                                            : "the editor main thread did not process the command in time");
	}
	SendAll(client, response);
}

usize HttpServer::Impl::GetPendingCount()
{
	std::lock_guard lock(QueueMutex);
	return Queue.size();
}

std::string HttpServer::Impl::HandleRequest(const PendingRequest& request)
{
	std::string target = request.Target;
	const usize queryStart = target.find('?');
	if (queryStart != std::string::npos)
	{
		target.resize(queryStart);
	}

	if (request.Method == "GET" && target == "/health")
	{
		JsonValue health = JsonValue::MakeObject();
		health.Set("ok", true);
		health.Set("status", "running");
		health.Set("engine", std::string(c_EngineName));
		health.Set("version", std::string(c_EngineVersion));
		health.Set("address", "127.0.0.1");
		health.Set("port", Port);
		health.Set("pendingRequests", static_cast<u64>(GetPendingCount()));
		return BuildHttpResponse(200, "application/json", health.Dump());
	}

	if (request.Method == "GET" && target == "/commands")
	{
		const JsonValue commands = CommandBus::Get().DescribeCommands();
		return BuildHttpResponse(200, "application/json", commands.Dump());
	}

	if (request.Method == "POST" && target == "/command")
	{
		JsonValue::ParseError parseError;
		const std::optional<JsonValue> document = JsonValue::Parse(request.Body, &parseError);
		if (!document.has_value())
		{
			return BuildErrorResponse(
			    400,
			    std::format("invalid JSON body at {}:{}: {}", parseError.Line, parseError.Column, parseError.Message));
		}
		if (!document->IsObject())
		{
			return BuildErrorResponse(
			    400,
			    "the request body must be a JSON object like {\"command\":\"project.info\",\"params\":{}}");
		}

		const JsonValue response = CommandBus::Get().ExecuteJson(*document);
		return BuildHttpResponse(200, "application/json", response.Dump());
	}

	return BuildErrorResponse(
	    404,
	    std::format("unknown route '{} {}'; valid routes: GET /health, GET /commands, POST /command",
	                request.Method,
	                target));
}

HttpServer::HttpServer() : m_Impl(CreateScope<Impl>()) {}

HttpServer::~HttpServer()
{
	Stop();
}

bool HttpServer::Start(u16 port)
{
	if (m_Impl->Running.load())
	{
		AE_CORE_WARN("HTTP command server is already listening on port {}", m_Impl->Port);
		return false;
	}
	if (!AcquireWinsock())
	{
		AE_CORE_ERROR("HTTP command server: failed to initialize Winsock");
		return false;
	}

	const SocketHandle listener = ::socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
	if (listener == c_InvalidSocket)
	{
		AE_CORE_ERROR("HTTP command server: failed to create a socket: {}", LastSocketError());
		ReleaseWinsock();
		return false;
	}

	const int reuseAddress = 1;
	::setsockopt(listener,
	             SOL_SOCKET,
	             SO_REUSEADDR,
	             reinterpret_cast<const char*>(&reuseAddress),
	             sizeof(reuseAddress));

	sockaddr_in address{};
	address.sin_family = AF_INET;
	address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
	address.sin_port = htons(port);
	if (::bind(listener, reinterpret_cast<const sockaddr*>(&address), sizeof(address)) != 0)
	{
		AE_CORE_ERROR("HTTP command server: failed to bind 127.0.0.1:{}: {}", port, LastSocketError());
		CloseSocket(listener);
		ReleaseWinsock();
		return false;
	}
	if (::listen(listener, SOMAXCONN) != 0)
	{
		AE_CORE_ERROR("HTTP command server: failed to listen: {}", LastSocketError());
		CloseSocket(listener);
		ReleaseWinsock();
		return false;
	}

	sockaddr_in boundAddress{};
#if defined(_WIN32)
	int boundAddressLength = sizeof(boundAddress);
#else
	socklen_t boundAddressLength = sizeof(boundAddress);
#endif
	if (::getsockname(listener, reinterpret_cast<sockaddr*>(&boundAddress), &boundAddressLength) != 0)
	{
		AE_CORE_ERROR("HTTP command server: failed to query the bound port: {}", LastSocketError());
		CloseSocket(listener);
		ReleaseWinsock();
		return false;
	}

	m_Impl->Listener = listener;
	m_Impl->Port = ntohs(boundAddress.sin_port);
	m_Impl->WinsockAcquired = true;
	m_Impl->StopRequested.store(false);
	m_Impl->Running.store(true);

	try
	{
		Impl* impl = m_Impl.get();
		m_Impl->NetworkThread = std::thread([impl] { impl->Run(); });
	}
	catch (const std::exception& exception)
	{
		AE_CORE_ERROR("HTTP command server: failed to start the network thread: {}", exception.what());
		CloseSocket(m_Impl->Listener);
		m_Impl->Listener = c_InvalidSocket;
		m_Impl->Port = 0;
		m_Impl->Running.store(false);
		m_Impl->WinsockAcquired = false;
		ReleaseWinsock();
		return false;
	}

	AE_CORE_INFO("HTTP command server listening on http://127.0.0.1:{}", m_Impl->Port);
	return true;
}

void HttpServer::Stop()
{
	if (m_Impl == nullptr)
	{
		return;
	}
	if (!m_Impl->Running.load() && !m_Impl->NetworkThread.joinable())
	{
		return;
	}

	m_Impl->StopRequested.store(true);
	{
		std::vector<Ref<Impl::PendingRequest>> pendingRequests;
		{
			std::lock_guard lock(m_Impl->QueueMutex);
			pendingRequests.assign(m_Impl->Queue.begin(), m_Impl->Queue.end());
		}
		for (const Ref<Impl::PendingRequest>& request : pendingRequests)
		{
			{
				std::lock_guard lock(request->Mutex);
				request->Aborted = true;
			}
			request->Cv.notify_all();
		}
	}
	{
		std::lock_guard lock(m_Impl->ClientMutex);
		ShutdownSocketReceive(m_Impl->ActiveClient);
	}

	if (m_Impl->NetworkThread.joinable())
	{
		m_Impl->NetworkThread.join();
	}

	CloseSocket(m_Impl->Listener);
	m_Impl->Listener = c_InvalidSocket;
	m_Impl->Port = 0;
	m_Impl->Running.store(false);
	if (m_Impl->WinsockAcquired)
	{
		m_Impl->WinsockAcquired = false;
		ReleaseWinsock();
	}
	AE_CORE_INFO("HTTP command server stopped");
}

bool HttpServer::IsRunning() const
{
	return m_Impl != nullptr && m_Impl->Running.load();
}

u16 HttpServer::GetPort() const
{
	return m_Impl == nullptr ? 0 : m_Impl->Port;
}

void HttpServer::ProcessPending()
{
	if (m_Impl == nullptr)
	{
		return;
	}

	std::vector<Ref<Impl::PendingRequest>> batch;
	{
		std::lock_guard lock(m_Impl->QueueMutex);
		while (!m_Impl->Queue.empty() && batch.size() < c_MaxRequestsPerTick)
		{
			batch.push_back(std::move(m_Impl->Queue.front()));
			m_Impl->Queue.pop_front();
		}
	}

	for (const Ref<Impl::PendingRequest>& request : batch)
	{
		{
			std::lock_guard lock(request->Mutex);
			if (!request->Aborted && !m_Impl->StopRequested.load())
			{
				request->Response = m_Impl->HandleRequest(*request);
				request->Completed = true;
			}
			else if (!request->Completed)
			{
				// The client already timed out or the server is stopping: do not
				// execute a command nobody is waiting for.
				request->Aborted = true;
				request->Completed = true;
			}
		}
		request->Cv.notify_all();
	}
}

std::string HttpServer::GetStatusText()
{
	if (m_Impl == nullptr)
	{
		return "HTTP command server is not initialized";
	}
	if (!m_Impl->Running.load())
	{
		return "HTTP command server is stopped";
	}
	return std::format("HTTP command server listening on http://127.0.0.1:{} ({} pending request(s))",
	                   m_Impl->Port,
	                   m_Impl->GetPendingCount());
}
} // namespace aegis
