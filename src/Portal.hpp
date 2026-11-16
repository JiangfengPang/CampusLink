#pragma once
#include <functional>
#include <cstdint>
#include <map>
#include <optional>
#include <stdexcept>
#include <string>
#include <winrt/Windows.Data.Json.h>

namespace campus {
struct Credentials {
    std::wstring account;
    std::wstring password;
    Credentials() = default;
    Credentials(std::wstring accountValue, std::wstring passwordValue)
        : account(std::move(accountValue)), password(std::move(passwordValue)) {}
    Credentials(Credentials const&) = delete;
    Credentials& operator=(Credentials const&) = delete;
    Credentials(Credentials&&) noexcept = default;
    Credentials& operator=(Credentials&&) noexcept = default;
    ~Credentials();
};

enum class Operation { Status, Page, Config, Login, Logout, LegacyLogout, MacFind, MacUnbind };
enum class Gateway { Campus, Wired };
using Query = std::map<std::wstring, std::wstring>;
using Transport = std::function<std::wstring(Operation, Query const&)>;
using GatewayTransport = std::function<std::wstring(Gateway, Operation, Query const&)>;
struct NetworkFailure : std::runtime_error {
    Gateway gateway;
    Operation operation;
    std::wstring stage;
    unsigned long systemCode;
    unsigned long httpStatus;
    NetworkFailure(Gateway gatewayValue, Operation operationValue, std::wstring stageValue,
        unsigned long systemCodeValue, unsigned long httpStatusValue = 0)
        : std::runtime_error("network"), gateway(gatewayValue), operation(operationValue),
          stage(std::move(stageValue)), systemCode(systemCodeValue), httpStatus(httpStatusValue) {}
};
struct Status { bool online{}; winrt::Windows::Data::Json::JsonObject data; };
struct Context { std::wstring ip; std::wstring mac; int vlan{1}; };
enum class ConnectionResult { AlreadyOnline, Connected };
enum class DisconnectionResult { AlreadyOffline, Disconnected };

std::wstring Encode(std::wstring const& value);
std::wstring BuildPath(Operation operation, Query const& query);
std::uint16_t RequestPort(Operation operation);
std::wstring HttpRequest(Operation operation, Query const& query);
std::wstring HttpRequestAt(Gateway gateway, Operation operation, Query const& query);
std::wstring GatewayHost(Gateway gateway);
std::wstring GatewayUrl(Gateway gateway);
std::wstring DiagnoseNetwork(GatewayTransport const& transport = HttpRequestAt);
winrt::Windows::Data::Json::JsonObject ParseJsonp(std::wstring const& body);
std::wstring JsonText(winrt::Windows::Data::Json::JsonObject const& object, std::wstring const& key);
Context ParseContext(std::wstring const& html, winrt::Windows::Data::Json::JsonObject const& status);
std::wstring DecimalIp(std::wstring const& ip);

class Portal {
public:
    Portal() : gatewayRequest_(HttpRequestAt) {}
    explicit Portal(Gateway preferred) : gatewayRequest_(HttpRequestAt), preferredGateway_(preferred) {}
    explicit Portal(Transport transport) : request_(std::move(transport)) {}
    explicit Portal(GatewayTransport transport, Gateway preferred = Gateway::Campus)
        : gatewayRequest_(std::move(transport)), preferredGateway_(preferred) {}
    Status Check();
    std::optional<Gateway> SelectedGateway() const { return gateway_; }
    ConnectionResult Connect(Credentials const& credentials, bool waitForNetwork = false);
    DisconnectionResult Disconnect();
private:
    winrt::Windows::Data::Json::JsonObject Api(Operation operation, Query query = {});
    winrt::Windows::Data::Json::JsonObject LoadConfig(Context const& context);
    std::wstring FindCurrentMac(Context const& context, std::wstring const& account, winrt::Windows::Data::Json::JsonObject const& config);
    std::wstring Request(Operation operation, Query const& query);
    Status CheckSelected();
    Transport request_;
    GatewayTransport gatewayRequest_;
    std::optional<Gateway> gateway_;
    Gateway preferredGateway_{Gateway::Campus};
    unsigned failedProbeMask_{};
};
}
