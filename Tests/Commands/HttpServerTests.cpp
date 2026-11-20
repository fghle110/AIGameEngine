#include "TestSupport.h"

#include <doctest/doctest.h>

#if defined(_WIN32)
#include <winsock2.h>
#include <ws2tcpip.h>
#else
#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>
#endif

#include "Aegis/Commands/CommandBus.h"
#include "Aegis/Commands/HttpServer.h"
#include "Aegis/Core/Config.h"
#include "Aegis/Core/Json.h"

#include <atomic>
#include <chrono>
#include <cstdlib>
#include <format>
#include <string>
#include <string_view>
#include <thread>
#include <utility>

using namespace aegis;

namespace
{
#if defined(_WIN32)
using TestSocket = SOCKET;
constexpr TestSocket c_InvalidTestSocket = INVALID_SOCKET;
#else
using TestSocket = int;
constexpr TestSocket c_InvalidTestSocket = -1;
#endif

void ensureNetworkInitialized()
{
#if defined(_WIN32)
	static const bool s_Initialized = []
	{
		WSADATA data{};
		return WSAStartup(MAKEWORD(2, 2), &data) == 0;
	}();
	(void)s_Initialized;
#endif
}

void closeTestSocket(TestSocket socket)
{
#if defined(_WIN32)
	::closesocket(socket);
#else
	::close(socket);
#endif
}

// Raw HTTP client used to prove the server works over a real socket from a
// non-main thread. It performs one request/response exchange and closes.
std::string sendRawRequest(u16 port, std::string_view request)
{
	ensureNetworkInitialized();
	const TestSocket socket = ::socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
	if (socket == c_InvalidTestSocket)
	{
		return {};
	}

	sockaddr_in address{};
	address.sin_family = AF_INET;
	address.sin_port = htons(port);
	address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
	if (::connect(socket, reinterpret_cast<sockaddr*>(&address), sizeof(address)) != 0)
	{
		closeTestSocket(socket);
		return {};
	}

	usize offset = 0;
	while (offset < request.size())
	{
		const int sent = ::send(socket, request.data() + offset, static_cast<int>(request.size() - offset), 0);
		if (sent <= 0)
		{
			closeTestSocket(socket);
			return {};
		}
		offset += static_cast<usize>(sent);
	}
#if defined(_WIN32)
	::shutdown(socket, SD_SEND);
#else
	::shutdown(socket, SHUT_WR);
#endif

	std::string response;
	char buffer[4096];
	for (;;)
	{
		const int received = ::recv(socket, buffer, sizeof(buffer), 0);
		if (received <= 0)
		{
			break;
		}
		response.append(buffer, static_cast<usize>(received));
	}
	closeTestSocket(socket);
	return response;
}

bool splitResponse(std::string_view response, int& outStatus, std::string& outBody)
{
	const usize headerEnd = response.find("\r\n\r\n");
	if (headerEnd == std::string_view::npos)
	{
		return false;
	}
	const usize lineEnd = response.find("\r\n");
	const std::string_view statusLine = response.substr(0, lineEnd);
	const usize firstSpace = statusLine.find(' ');
	if (firstSpace == std::string_view::npos || firstSpace + 4 > statusLine.size())
	{
		return false;
	}
	const std::string statusText(statusLine.substr(firstSpace + 1, 3));
	outStatus = std::atoi(statusText.c_str());
	outBody = std::string(response.substr(headerEnd + 4));
	return true;
}

struct ClientExchange
{
	std::atomic<bool> Done{ false };
	std::string Response;
};

// Runs the main-thread side of the contract: ProcessPending() in a loop until
// the network thread has its response. No sleeping on the command itself.
void pumpUntilDone(HttpServer& server, const ClientExchange& exchange, int timeoutMs)
{
	const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(timeoutMs);
	while (!exchange.Done.load() && std::chrono::steady_clock::now() < deadline)
	{
		server.ProcessPending();
		std::this_thread::sleep_for(std::chrono::milliseconds(1));
	}
	server.ProcessPending();
}

void exchange(HttpServer& server, std::string_view request, std::string& outResponse, int timeoutMs = 5000)
{
	ClientExchange clientExchange;
	const u16 port = server.GetPort();
	const std::string requestText(request);
	std::thread client(
	    [&clientExchange, port, requestText]()
	    {
		    clientExchange.Response = sendRawRequest(port, requestText);
		    clientExchange.Done.store(true);
	    });
	pumpUntilDone(server, clientExchange, timeoutMs);
	client.join();
	outResponse = clientExchange.Response;
}

CommandInfo makeTestInfo(std::string name, std::string description)
{
	CommandInfo info;
	info.Name = std::move(name);
	info.Category = "test";
	info.Description = std::move(description);
	return info;
}
} // namespace

TEST_CASE("HTTP: server binds an ephemeral port and stops cleanly")
{
	HttpServer server;
	CHECK_FALSE(server.IsRunning());
	CHECK(server.GetPort() == 0);
	CHECK(server.GetStatusText().find("stopped") != std::string::npos);

	REQUIRE(server.Start(0));
	CHECK(server.IsRunning());
	CHECK(server.GetPort() != 0);
	CHECK(server.GetStatusText().find("127.0.0.1") != std::string::npos);

	server.Stop();
	CHECK_FALSE(server.IsRunning());
	CHECK(server.GetPort() == 0);
	CHECK(server.GetStatusText().find("stopped") != std::string::npos);

	// A stopped server can be started again on a fresh ephemeral port.
	REQUIRE(server.Start(0));
	CHECK(server.IsRunning());
	server.Stop();
	CHECK_FALSE(server.IsRunning());
}

TEST_CASE("HTTP: GET /health reports the server state")
{
	HttpServer server;
	REQUIRE(server.Start(0));

	std::string response;
	exchange(server, "GET /health HTTP/1.1\r\nHost: 127.0.0.1\r\nConnection: close\r\n\r\n", response);
	int status = 0;
	std::string body;
	REQUIRE(splitResponse(response, status, body));
	CHECK(status == 200);
	const JsonValue json = JsonValue::ParseOrEmpty(body);
	CHECK(json.GetBool("ok"));
	CHECK(json.GetString("version") == std::string(c_EngineVersion));
	CHECK(json.GetUInt("port") == server.GetPort());

	exchange(server, "GET /unknown HTTP/1.1\r\nHost: 127.0.0.1\r\nConnection: close\r\n\r\n", response);
	REQUIRE(splitResponse(response, status, body));
	CHECK(status == 404);
	const JsonValue notFound = JsonValue::ParseOrEmpty(body);
	CHECK_FALSE(notFound.GetBool("ok", true));
	CHECK(notFound.GetString("error").find("unknown route") != std::string::npos);

	server.Stop();
	CHECK_FALSE(server.IsRunning());
}

TEST_CASE("HTTP: GET /commands returns the command registry")
{
	ensureNetworkInitialized();

	CommandInfo info = makeTestInfo("test.http.registry", "Registered for the HTTP /commands test");
	info.Parameters.push_back({ "value", "number", false, "Optional value" });
	CommandBus& bus = CommandBus::Get();
	bus.Unregister(info.Name);
	const std::string commandName = info.Name;
	REQUIRE(bus.Register(std::move(info), [](const JsonValue&) { return CommandResult::Ok(); }));

	HttpServer server;
	REQUIRE(server.Start(0));
	std::string response;
	exchange(server, "GET /commands HTTP/1.1\r\nHost: 127.0.0.1\r\nConnection: close\r\n\r\n", response);
	int status = 0;
	std::string body;
	REQUIRE(splitResponse(response, status, body));
	CHECK(status == 200);

	const JsonValue commands = JsonValue::ParseOrEmpty(body);
	REQUIRE(commands.IsArray());
	bool found = false;
	for (usize index = 0; index < commands.GetSize(); ++index)
	{
		const JsonValue& entry = commands[index];
		if (entry.GetString("name") != commandName)
		{
			continue;
		}
		found = true;
		CHECK(entry.GetString("category") == "test");
		CHECK(entry.GetString("description") == "Registered for the HTTP /commands test");
		REQUIRE(entry["parameters"].IsArray());
		REQUIRE(entry["parameters"].GetSize() == 1);
		CHECK(entry["parameters"][0].GetString("name") == "value");
	}
	CHECK(found);

	server.Stop();
	bus.Unregister(commandName);
}

TEST_CASE("HTTP: POST /command executes on the main thread")
{
	ensureNetworkInitialized();

	const std::thread::id mainThreadId = std::this_thread::get_id();
	std::atomic<bool> ranOnMainThread{ false };

	CommandInfo info = makeTestInfo("test.http.sum", "Adds two numbers");
	info.Parameters.push_back({ "a", "number", true, "First addend" });
	info.Parameters.push_back({ "b", "number", true, "Second addend" });
	CommandBus& bus = CommandBus::Get();
	bus.Unregister(info.Name);
	const std::string commandName = info.Name;
	REQUIRE(bus.Register(std::move(info),
	                     [&ranOnMainThread, mainThreadId](const JsonValue& params)
	                     {
		                     ranOnMainThread.store(std::this_thread::get_id() == mainThreadId);
		                     JsonValue result = JsonValue::MakeObject();
		                     result.Set("sum", params.GetInt("a") + params.GetInt("b"));
		                     return CommandResult::Ok(result);
	                     }));

	HttpServer server;
	REQUIRE(server.Start(0));

	const std::string body = "{\"command\":\"test.http.sum\",\"params\":{\"a\":3,\"b\":4}}";
	const std::string request =
	    std::format("POST /command HTTP/1.1\r\nHost: 127.0.0.1\r\nContent-Type: application/json\r\n"
	                "Content-Length: {}\r\nConnection: close\r\n\r\n{}",
	                body.size(),
	                body);
	std::string response;
	exchange(server, request, response);

	int status = 0;
	std::string responseBody;
	REQUIRE(splitResponse(response, status, responseBody));
	CHECK(status == 200);
	const JsonValue json = JsonValue::ParseOrEmpty(responseBody);
	CHECK(json.GetBool("ok"));
	CHECK(json.GetString("command") == commandName);
	CHECK(json["result"].GetInt("sum") == 7);
	CHECK(ranOnMainThread.load());

	server.Stop();
	bus.Unregister(commandName);
}

TEST_CASE("HTTP: malformed and oversized requests are rejected")
{
	HttpServer server;
	REQUIRE(server.Start(0));

	std::string response;
	const std::string body = "not-json";
	const std::string request =
	    std::format("POST /command HTTP/1.1\r\nHost: 127.0.0.1\r\nContent-Length: {}\r\nConnection: close\r\n\r\n{}",
	                body.size(),
	                body);
	exchange(server, request, response);
	int status = 0;
	std::string responseBody;
	REQUIRE(splitResponse(response, status, responseBody));
	CHECK(status == 400);
	CHECK(JsonValue::ParseOrEmpty(responseBody).GetString("error").find("invalid JSON") != std::string::npos);

	exchange(server,
	         "POST /command HTTP/1.1\r\nHost: 127.0.0.1\r\nContent-Length: 2000000\r\nConnection: close\r\n\r\n",
	         response);
	REQUIRE(splitResponse(response, status, responseBody));
	CHECK(status == 413);
	CHECK(JsonValue::ParseOrEmpty(responseBody).GetString("error").find("exceeds") != std::string::npos);

	exchange(server, "DELETE /health HTTP/1.1\r\nHost: 127.0.0.1\r\nConnection: close\r\n\r\n", response);
	REQUIRE(splitResponse(response, status, responseBody));
	CHECK(status == 400);

	server.Stop();
}

TEST_CASE("HTTP: Stop releases a request that is waiting for the main thread")
{
	ensureNetworkInitialized();

	CommandInfo info = makeTestInfo("test.http.pending", "Never processed by the main thread");
	CommandBus& bus = CommandBus::Get();
	bus.Unregister(info.Name);
	const std::string commandName = info.Name;
	REQUIRE(bus.Register(std::move(info), [](const JsonValue&) { return CommandResult::Ok(); }));

	HttpServer server;
	REQUIRE(server.Start(0));

	ClientExchange clientExchange;
	const u16 port = server.GetPort();
	const std::string body = "{\"command\":\"test.http.pending\"}";
	const std::string request =
	    std::format("POST /command HTTP/1.1\r\nHost: 127.0.0.1\r\nContent-Type: application/json\r\n"
	                "Content-Length: {}\r\nConnection: close\r\n\r\n{}",
	                body.size(),
	                body);
	std::thread client(
	    [&clientExchange, port, request]()
	    {
		    clientExchange.Response = sendRawRequest(port, request);
		    clientExchange.Done.store(true);
	    });

	// Wait until the network thread has queued the request without processing it.
	const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
	bool queued = false;
	while (std::chrono::steady_clock::now() < deadline)
	{
		if (server.GetStatusText().find("1 pending request") != std::string::npos)
		{
			queued = true;
			break;
		}
		std::this_thread::sleep_for(std::chrono::milliseconds(1));
	}
	CHECK(queued);

	server.Stop();
	client.join();
	CHECK_FALSE(server.IsRunning());

	int status = 0;
	std::string responseBody;
	REQUIRE(splitResponse(clientExchange.Response, status, responseBody));
	CHECK(status == 503);
	CHECK(JsonValue::ParseOrEmpty(responseBody).GetString("error").find("stopping") != std::string::npos);

	bus.Unregister(commandName);
}
