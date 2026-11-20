#pragma once

#include <string>
#include <string_view>

namespace aegis
{
// Minimal blocking HTTP/1.1 client used by AegisCli to talk to the editor AI
// command API. Dependency-free: Winsock on Windows, BSD sockets elsewhere.
// Connections are one-shot (Connection: close) and use Content-Length framing,
// which is what the editor server always sends.
struct HttpResult
{
	int StatusCode = 0;
	std::string Body;
	std::string Error;

	bool IsSuccess() const
	{
		return Error.empty() && StatusCode >= 200 && StatusCode < 300;
	}
};

class HttpClient
{
public:
	static constexpr int c_DefaultTimeoutMs = 5000;

	// Performs GET url. Returns a result with Error set (and StatusCode 0) when
	// the transport fails; HTTP error statuses are reported through StatusCode.
	static HttpResult Get(std::string_view url, int timeoutMs = c_DefaultTimeoutMs);

	static HttpResult Post(std::string_view url,
	                       std::string_view body,
	                       std::string_view contentType = "application/json",
	                       int timeoutMs = c_DefaultTimeoutMs);

	HttpClient() = delete;
};
} // namespace aegis
