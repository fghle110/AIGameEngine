#include "HttpClient.h"

#include "Aegis/Core/Base.h"

#if defined(_WIN32)
#include <winsock2.h>
#include <ws2tcpip.h>
#else
#include <arpa/inet.h>
#include <cerrno>
#include <fcntl.h>
#include <netdb.h>
#include <netinet/in.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <unistd.h>
#endif

#include <charconv>
#include <cstring>
#include <format>
#include <string>
#include <string_view>
#include <system_error>

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

constexpr usize c_MaxResponseBytes = 32 * 1024 * 1024;
#if defined(MSG_NOSIGNAL)
constexpr int c_SendFlags = MSG_NOSIGNAL;
#else
constexpr int c_SendFlags = 0;
#endif

std::string lastSocketError()
{
#if defined(_WIN32)
	return std::format("Winsock error {}", static_cast<int>(WSAGetLastError()));
#else
	return std::format("socket error {} ({})", errno, std::strerror(errno));
#endif
}

void closeSocket(SocketHandle socket)
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

#if defined(_WIN32)
class WinsockScope
{
public:
	WinsockScope()
	{
		WSADATA data{};
		m_Initialized = WSAStartup(MAKEWORD(2, 2), &data) == 0;
	}

	~WinsockScope()
	{
		if (m_Initialized)
		{
			WSACleanup();
		}
	}

	WinsockScope(const WinsockScope&) = delete;
	WinsockScope& operator=(const WinsockScope&) = delete;

	bool IsInitialized() const
	{
		return m_Initialized;
	}

private:
	bool m_Initialized = false;
};
#else
class WinsockScope
{
public:
	bool IsInitialized() const
	{
		return true;
	}
};
#endif

bool setBlockingMode(SocketHandle socket, bool blocking)
{
#if defined(_WIN32)
	u_long mode = blocking ? 0 : 1;
	return ::ioctlsocket(socket, FIONBIO, &mode) == 0;
#else
	const int flags = ::fcntl(socket, F_GETFL, 0);
	if (flags < 0)
	{
		return false;
	}
	const int updated = blocking ? (flags & ~O_NONBLOCK) : (flags | O_NONBLOCK);
	return ::fcntl(socket, F_SETFL, updated) == 0;
#endif
}

bool isConnectInProgress()
{
#if defined(_WIN32)
	const int code = WSAGetLastError();
	return code == WSAEWOULDBLOCK || code == WSAEINPROGRESS;
#else
	return errno == EINPROGRESS || errno == EWOULDBLOCK || errno == EINTR;
#endif
}

void setSocketTimeout(SocketHandle socket, int timeoutMs)
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

bool connectWithTimeout(SocketHandle socket,
                        const sockaddr* address,
                        socklen_t addressLength,
                        int timeoutMs,
                        std::string& outError)
{
	if (!setBlockingMode(socket, false))
	{
		outError = std::format("failed to set the socket to non-blocking mode: {}", lastSocketError());
		return false;
	}

	int result = ::connect(socket, address, addressLength);
	if (result == 0)
	{
		setBlockingMode(socket, true);
		return true;
	}
	if (!isConnectInProgress())
	{
		outError = std::format("connect() failed: {}", lastSocketError());
		return false;
	}

	fd_set writeSet;
	FD_ZERO(&writeSet);
	FD_SET(socket, &writeSet);
	timeval timeout{};
	timeout.tv_sec = timeoutMs / 1000;
	timeout.tv_usec = (timeoutMs % 1000) * 1000;
	result = ::select(static_cast<int>(socket) + 1, nullptr, &writeSet, nullptr, &timeout);
	if (result <= 0)
	{
		outError = result == 0 ? std::format("connect() timed out after {} ms", timeoutMs)
		                       : std::format("select() failed while connecting: {}", lastSocketError());
		return false;
	}

	int socketError = 0;
#if defined(_WIN32)
	int socketErrorLength = sizeof(socketError);
#else
	socklen_t socketErrorLength = sizeof(socketError);
#endif
	if (::getsockopt(socket, SOL_SOCKET, SO_ERROR, reinterpret_cast<char*>(&socketError), &socketErrorLength) != 0)
	{
		outError = std::format("getsockopt(SO_ERROR) failed: {}", lastSocketError());
		return false;
	}
	if (socketError != 0)
	{
		outError = std::format("connect() failed with socket error {}", socketError);
		return false;
	}

	setBlockingMode(socket, true);
	return true;
}

bool sendAll(SocketHandle socket, std::string_view data, std::string& outError)
{
	usize offset = 0;
	while (offset < data.size())
	{
		const int sent = ::send(socket, data.data() + offset, static_cast<int>(data.size() - offset), c_SendFlags);
		if (sent <= 0)
		{
			outError = std::format("failed to send the HTTP request: {}", lastSocketError());
			return false;
		}
		offset += static_cast<usize>(sent);
	}
	return true;
}

// --- URL handling ------------------------------------------------------

struct ParsedUrl
{
	std::string Host;
	std::string Port = "80";
	std::string Authority;
	std::string Target = "/";
};

bool parseUrl(std::string_view url, ParsedUrl& out, std::string& outError)
{
	constexpr std::string_view c_Scheme = "http://";
	if (url.size() < c_Scheme.size() || url.substr(0, c_Scheme.size()) != c_Scheme)
	{
		outError = std::format("unsupported URL '{}'; only http:// URLs are supported", url);
		return false;
	}

	const std::string_view rest = url.substr(c_Scheme.size());
	const usize pathStart = rest.find('/');
	const usize queryStart = rest.find('?');
	usize split = std::string_view::npos;
	if (pathStart != std::string_view::npos && queryStart != std::string_view::npos)
	{
		split = pathStart < queryStart ? pathStart : queryStart;
	}
	else if (pathStart != std::string_view::npos)
	{
		split = pathStart;
	}
	else
	{
		split = queryStart;
	}

	const std::string_view authority = split == std::string_view::npos ? rest : rest.substr(0, split);
	std::string target = split == std::string_view::npos ? std::string("/") : std::string(rest.substr(split));
	if (!target.empty() && target.front() == '?')
	{
		target.insert(target.begin(), '/');
	}
	if (target.empty())
	{
		target = "/";
	}

	if (authority.empty())
	{
		outError = std::format("URL '{}' has no host", url);
		return false;
	}

	std::string_view host;
	std::string_view port = "80";
	if (authority.front() == '[')
	{
		const usize closingBracket = authority.find(']');
		if (closingBracket == std::string_view::npos)
		{
			outError = std::format("URL '{}' has an unterminated IPv6 host", url);
			return false;
		}
		host = authority.substr(1, closingBracket - 1);
		if (closingBracket + 1 < authority.size())
		{
			if (authority[closingBracket + 1] != ':')
			{
				outError = std::format("URL '{}' has an invalid authority", url);
				return false;
			}
			port = authority.substr(closingBracket + 2);
		}
	}
	else
	{
		const usize colon = authority.rfind(':');
		if (colon != std::string_view::npos)
		{
			host = authority.substr(0, colon);
			port = authority.substr(colon + 1);
		}
		else
		{
			host = authority;
		}
	}

	if (host.empty())
	{
		outError = std::format("URL '{}' has no host", url);
		return false;
	}

	u32 portNumber = 0;
	const auto [portEnd, portError] = std::from_chars(port.data(), port.data() + port.size(), portNumber);
	if (portError != std::errc{} || portEnd != port.data() + port.size() || portNumber == 0 || portNumber > 65535)
	{
		outError = std::format("URL '{}' has an invalid port '{}'", url, port);
		return false;
	}

	out.Host = std::string(host);
	out.Port = std::string(port);
	out.Authority = std::string(authority);
	out.Target = std::move(target);
	return true;
}

// --- Response parsing --------------------------------------------------

bool parseResponse(const std::string& raw, HttpResult& out, std::string& outError)
{
	const usize headerEnd = raw.find("\r\n\r\n");
	if (headerEnd == std::string::npos)
	{
		outError = "malformed HTTP response: missing the header terminator";
		return false;
	}

	const std::string_view headers(raw.data(), headerEnd);
	const usize statusLineEnd = headers.find("\r\n");
	const std::string_view statusLine =
	    headers.substr(0, statusLineEnd == std::string_view::npos ? headers.size() : statusLineEnd);
	const usize firstSpace = statusLine.find(' ');
	if (firstSpace == std::string_view::npos)
	{
		outError = "malformed HTTP response: missing the status line";
		return false;
	}
	const std::string_view afterVersion = statusLine.substr(firstSpace + 1);
	u32 statusCode = 0;
	const auto [statusEnd, statusError] =
	    std::from_chars(afterVersion.data(), afterVersion.data() + afterVersion.size(), statusCode);
	if (statusError != std::errc{} || statusEnd == afterVersion.data())
	{
		outError = "malformed HTTP response: invalid status code";
		return false;
	}

	usize contentLength = 0;
	bool hasContentLength = false;
	usize cursor = statusLineEnd == std::string_view::npos ? headers.size() : statusLineEnd + 2;
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
			continue;
		}
		std::string_view name = line.substr(0, colon);
		std::string nameLower(name);
		for (char& character : nameLower)
		{
			if (character >= 'A' && character <= 'Z')
			{
				character = static_cast<char>(character - 'A' + 'a');
			}
		}
		if (nameLower != "content-length")
		{
			continue;
		}
		std::string_view value = line.substr(colon + 1);
		while (!value.empty() && (value.front() == ' ' || value.front() == '\t'))
		{
			value.remove_prefix(1);
		}
		const auto [lengthEnd, lengthError] = std::from_chars(value.data(), value.data() + value.size(), contentLength);
		if (lengthError != std::errc{} || lengthEnd != value.data() + value.size())
		{
			outError = "malformed HTTP response: invalid Content-Length";
			return false;
		}
		hasContentLength = true;
	}

	const usize bodyStart = headerEnd + 4;
	const usize available = raw.size() - bodyStart;
	if (hasContentLength && available < contentLength)
	{
		outError =
		    std::format("truncated HTTP response: expected {} body bytes, received {}", contentLength, available);
		return false;
	}
	out.StatusCode = static_cast<int>(statusCode);
	out.Body = raw.substr(bodyStart, hasContentLength ? contentLength : std::string::npos);
	return true;
}

HttpResult performRequest(std::string_view method,
                          std::string_view url,
                          std::string_view body,
                          std::string_view contentType,
                          int timeoutMs)
{
	HttpResult result;
	ParsedUrl parsed;
	std::string error;
	if (!parseUrl(url, parsed, error))
	{
		result.Error = std::move(error);
		return result;
	}
	if (timeoutMs <= 0)
	{
		timeoutMs = HttpClient::c_DefaultTimeoutMs;
	}

	WinsockScope winsock;
	if (!winsock.IsInitialized())
	{
		result.Error = "failed to initialize Winsock";
		return result;
	}

	addrinfo hints{};
	hints.ai_family = AF_UNSPEC;
	hints.ai_socktype = SOCK_STREAM;
	hints.ai_protocol = IPPROTO_TCP;
	addrinfo* addresses = nullptr;
	const int resolveResult = ::getaddrinfo(parsed.Host.c_str(), parsed.Port.c_str(), &hints, &addresses);
	if (resolveResult != 0 || addresses == nullptr)
	{
		result.Error = std::format("failed to resolve '{}':{}", parsed.Host, parsed.Port);
		return result;
	}

	SocketHandle socket = c_InvalidSocket;
	std::string connectError;
	for (addrinfo* address = addresses; address != nullptr; address = address->ai_next)
	{
		socket = ::socket(address->ai_family, address->ai_socktype, address->ai_protocol);
		if (socket == c_InvalidSocket)
		{
			connectError = std::format("failed to create a socket: {}", lastSocketError());
			continue;
		}
		setSocketTimeout(socket, timeoutMs);
		if (connectWithTimeout(socket,
		                       address->ai_addr,
		                       static_cast<socklen_t>(address->ai_addrlen),
		                       timeoutMs,
		                       connectError))
		{
			break;
		}
		closeSocket(socket);
		socket = c_InvalidSocket;
	}
	::freeaddrinfo(addresses);

	if (socket == c_InvalidSocket)
	{
		result.Error = connectError.empty() ? std::format("could not connect to {}:{}", parsed.Host, parsed.Port)
		                                    : std::move(connectError);
		return result;
	}

	std::string request;
	request.reserve(512 + body.size());
	request += std::format("{} {} HTTP/1.1\r\n", method, parsed.Target);
	request += std::format("Host: {}\r\n", parsed.Authority);
	request += "Accept: application/json\r\n";
	request += "User-Agent: AegisCli\r\n";
	request += "Connection: close\r\n";
	if (!body.empty() || method == "POST")
	{
		request += std::format("Content-Type: {}\r\n", contentType);
		request += std::format("Content-Length: {}\r\n", body.size());
	}
	request += "\r\n";
	request += body;

	if (!sendAll(socket, request, error))
	{
		closeSocket(socket);
		result.Error = std::move(error);
		return result;
	}
#if defined(_WIN32)
	::shutdown(socket, SD_SEND);
#else
	::shutdown(socket, SHUT_WR);
#endif

	std::string rawResponse;
	char buffer[8192];
	for (;;)
	{
		const int received = ::recv(socket, buffer, sizeof(buffer), 0);
		if (received == 0)
		{
			break;
		}
		if (received < 0)
		{
#if !defined(_WIN32)
			if (errno == EINTR)
			{
				continue;
			}
#endif
			closeSocket(socket);
			result.Error = std::format("failed to read the HTTP response: {}", lastSocketError());
			return result;
		}
		rawResponse.append(buffer, static_cast<usize>(received));
		if (rawResponse.size() > c_MaxResponseBytes)
		{
			closeSocket(socket);
			result.Error = std::format("HTTP response exceeds {} bytes", c_MaxResponseBytes);
			return result;
		}
	}
	closeSocket(socket);

	if (!parseResponse(rawResponse, result, error))
	{
		result.StatusCode = 0;
		result.Body.clear();
		result.Error = std::move(error);
	}
	return result;
}
} // namespace

HttpResult HttpClient::Get(std::string_view url, int timeoutMs)
{
	return performRequest("GET", url, {}, {}, timeoutMs);
}

HttpResult HttpClient::Post(std::string_view url, std::string_view body, std::string_view contentType, int timeoutMs)
{
	return performRequest("POST", url, body, contentType, timeoutMs);
}
} // namespace aegis
