#include "pch.h"
#include "Portal.hpp"
#include "Store.hpp"
#include <winhttp.h>
#include <wincrypt.h>
#include <regex>
#include <thread>
#include <cwctype>

using namespace winrt::Windows::Data::Json;
namespace campus {
namespace {
struct InternetHandle {
    HINTERNET value{};
    explicit InternetHandle(HINTERNET handle) : value(handle) {}
    ~InternetHandle() { if (value) WinHttpCloseHandle(value); }
    operator HINTERNET() const { return value; }
};
[[noreturn]] void NetworkError(Gateway gateway, Operation operation, wchar_t const* stage,
    DWORD systemCode = GetLastError(), DWORD httpStatus = 0) {
    throw NetworkFailure(gateway, operation, stage, systemCode, httpStatus);
}
std::string Utf8(std::wstring const& value) {
    if (value.empty()) return {};
    int size = WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, value.data(), static_cast<int>(value.size()), nullptr, 0, nullptr, nullptr);
    if (!size) throw std::runtime_error("encoding");
    std::string output(size, '\0');
    WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, value.data(), static_cast<int>(value.size()), output.data(), size, nullptr, nullptr);
    return output;
}
std::wstring Decode(std::string const& body, UINT codePage) {
    if (body.empty()) return {};
    int length = MultiByteToWideChar(codePage, 0, body.data(), static_cast<int>(body.size()), nullptr, 0);
    std::wstring output(length, L'\0');
    MultiByteToWideChar(codePage, 0, body.data(), static_cast<int>(body.size()), output.data(), length);
    return output;
}
std::wstring Trim(std::wstring value) {
    auto first = value.find_first_not_of(L" \r\n\t\uFEFF");
    if (first == std::wstring::npos) return {};
    return value.substr(first, value.find_last_not_of(L" \r\n\t") - first + 1);
}
std::wstring Variable(std::wstring const& html, std::wstring const& name) {
    std::wregex pattern(L"(?:^|[^\\w])" + name + L"\\s*=\\s*['\"]([^'\"]{0,128})['\"]");
    std::wsmatch match;
    return std::regex_search(html, match, pattern) ? match[1].str() : L"";
}
bool IsIp(std::wstring const& value) {
    if (value.empty() || value == L"0.0.0.0") return false;
    int groups = 0, number = 0, digits = 0;
    for (wchar_t c : value + L".") {
        if (c == L'.') {
            if (!digits || number > 255 || ++groups > 4) return false;
            number = digits = 0;
        } else if (c >= L'0' && c <= L'9' && digits < 3) { number = number * 10 + c - L'0'; ++digits; }
        else return false;
    }
    return groups == 4;
}
std::wstring NormalizeMac(std::wstring value) {
    value.erase(std::remove_if(value.begin(), value.end(), [](wchar_t c) { return c == L':' || c == L'-'; }), value.end());
    if (!std::regex_match(value, std::wregex(L"[0-9a-fA-F]{12}")) || value == L"000000000000" || value == L"111111111111") return {};
    return value;
}
std::wstring Base64(std::wstring const& value) {
    std::string bytes = Utf8(value);
    DWORD size{};
    CryptBinaryToStringW(reinterpret_cast<BYTE const*>(bytes.data()), static_cast<DWORD>(bytes.size()), CRYPT_STRING_BASE64 | CRYPT_STRING_NOCRLF, nullptr, &size);
    std::wstring result(size, L'\0');
    if (!CryptBinaryToStringW(reinterpret_cast<BYTE const*>(bytes.data()), static_cast<DWORD>(bytes.size()), CRYPT_STRING_BASE64 | CRYPT_STRING_NOCRLF, result.data(), &size)) throw std::runtime_error("encoding");
    result.resize(size);
    if (!result.empty() && result.back() == L'\0') result.pop_back();
    return result;
}
std::wstring FailureMessage(JsonObject const& response, std::wstring const& password) {
    auto message = JsonText(response, L"msg");
    if (message.empty()) return L"认证未通过，请检查账号、密码或校园网账户状态。";
    message = std::regex_replace(message, std::wregex(L"<[^>]*>"), L"");
    if (!password.empty()) {
        size_t position{};
        while ((position = message.find(password, position)) != std::wstring::npos) {
            message.replace(position, password.size(), L"***"); position += 3;
        }
    }
    return message.substr(0, 200);
}
Query ConfigQuery(Context const& context) {
    return {
        { L"program_index", L"" }, { L"wlan_vlan_id", std::to_wstring(context.vlan) },
        { L"wlan_user_ip", Base64(context.ip) }, { L"wlan_user_ipv6", L"" },
        { L"wlan_user_ssid", L"" }, { L"wlan_user_areaid", L"" }, { L"wlan_ac_ip", L"" },
        { L"wlan_ap_mac", L"000000000000" }, { L"gw_id", L"000000000000" }
    };
}
void AddApiParameters(Query& query) {
    query[L"callback"] = L"campusLogin";
    query[L"jsVersion"] = L"4.2";
    query[L"lang"] = L"zh-cn";
    query[L"v"] = std::to_wstring(GetTickCount64());
}
}

Credentials::~Credentials() {
    if (!password.empty()) SecureZeroMemory(password.data(), password.size() * sizeof(wchar_t));
}

std::wstring Encode(std::wstring const& value) {
    auto bytes = Utf8(value);
    constexpr wchar_t hex[] = L"0123456789ABCDEF";
    std::wstring output;
    for (unsigned char c : bytes) {
        if ((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '-' || c == '_' || c == '.' || c == '~') output += c;
        else { output += L'%'; output += hex[c >> 4]; output += hex[c & 15]; }
    }
    if (!bytes.empty()) SecureZeroMemory(bytes.data(), bytes.size());
    return output;
}

std::wstring BuildPath(Operation operation, Query const& query) {
    std::wstring path;
    switch (operation) {
    case Operation::Status: path = L"/drcom/chkstatus"; break;
    case Operation::Page: path = L"/"; break;
    case Operation::Config: path = L"/eportal/portal/page/loadConfig"; break;
    case Operation::Login: path = L"/eportal/portal/login"; break;
    case Operation::Logout: path = L"/eportal/portal/logout"; break;
    case Operation::LegacyLogout: path = L"/drcom/logout"; break;
    case Operation::MacFind: path = L"/eportal/portal/mac/find"; break;
    case Operation::MacUnbind: path = L"/eportal/portal/mac/unbind"; break;
    default: throw std::runtime_error("operation");
    }
    bool first = true;
    for (auto const& [key, value] : query) {
        path += first ? L'?' : L'&'; first = false;
        path += Encode(key) + L"=" + Encode(value);
    }
    return path;
}

std::uint16_t RequestPort(Operation operation) {
    return (operation == Operation::Status || operation == Operation::Page || operation == Operation::LegacyLogout) ? 80 : 801;
}
std::wstring GatewayHost(Gateway gateway) {
    // Only the two school addresses confirmed by users. Never take a password
    // destination from an untrusted redirect or a downloaded page.
    switch (gateway) {
    case Gateway::Campus: return L"172.17.1.90";
    case Gateway::Wired: return L"172.31.0.10";
    default: throw std::runtime_error("protocol");
    }
}
std::wstring GatewayUrl(Gateway gateway) {
    return L"http://" + GatewayHost(gateway) + (gateway == Gateway::Wired ? L"/a79.htm" : L"/");
}
std::wstring HttpRequest(Operation operation, Query const& query) {
    return HttpRequestAt(Gateway::Campus, operation, query);
}
std::wstring HttpRequestAt(Gateway gateway, Operation operation, Query const& query) {
    auto host = GatewayHost(gateway);
    auto referer = GatewayUrl(gateway);
    InternetHandle session(WinHttpOpen(L"CampusLink/1.1.4", WINHTTP_ACCESS_TYPE_NO_PROXY, WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0));
    if (!session) NetworkError(gateway, operation, L"创建网络会话");
    if (!WinHttpSetTimeouts(session, 2000, 3000, 4000, 6000)) NetworkError(gateway, operation, L"设置超时");
    InternetHandle connection(WinHttpConnect(session, host.c_str(), RequestPort(operation), 0));
    if (!connection) NetworkError(gateway, operation, L"准备连接");
    auto path = BuildPath(operation, query);
    if (operation == Operation::Page && gateway == Gateway::Wired) path = L"/a79.htm";
    InternetHandle request(WinHttpOpenRequest(connection, L"GET", path.c_str(), nullptr, referer.c_str(), WINHTTP_DEFAULT_ACCEPT_TYPES, 0));
    if (!request) NetworkError(gateway, operation, L"创建请求");
    DWORD disabled = WINHTTP_DISABLE_REDIRECTS | WINHTTP_DISABLE_COOKIES;
    if (!WinHttpSetOption(request, WINHTTP_OPTION_DISABLE_FEATURE, &disabled, sizeof(disabled))) NetworkError(gateway, operation, L"设置请求");
    BOOL sent = WinHttpSendRequest(request, WINHTTP_NO_ADDITIONAL_HEADERS, 0, WINHTTP_NO_REQUEST_DATA, 0, 0, 0);
    DWORD sendError = sent ? ERROR_SUCCESS : GetLastError();
    if (!path.empty()) SecureZeroMemory(path.data(), path.size() * sizeof(wchar_t));
    if (!sent) NetworkError(gateway, operation, L"发送请求", sendError);
    if (!WinHttpReceiveResponse(request, nullptr)) NetworkError(gateway, operation, L"接收响应");
    DWORD status{}, statusSize = sizeof(status);
    if (!WinHttpQueryHeaders(request, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER, WINHTTP_HEADER_NAME_BY_INDEX, &status, &statusSize, WINHTTP_NO_HEADER_INDEX)) NetworkError(gateway, operation, L"读取状态码");
    if (status != 200) NetworkError(gateway, operation, L"HTTP 状态", ERROR_SUCCESS, status);
    wchar_t contentType[256]{}; DWORD typeSize = sizeof(contentType);
    WinHttpQueryHeaders(request, WINHTTP_QUERY_CONTENT_TYPE, WINHTTP_HEADER_NAME_BY_INDEX, contentType, &typeSize, WINHTTP_NO_HEADER_INDEX);
    std::wstring type(contentType); std::transform(type.begin(), type.end(), type.begin(), towlower);
    UINT codePage = type.find(L"gb") != std::wstring::npos ? 936 : CP_UTF8;
    std::string body;
    char buffer[8192]; DWORD read{};
    do {
        if (!WinHttpReadData(request, buffer, sizeof(buffer), &read)) NetworkError(gateway, operation, L"读取响应");
        if (body.size() + read > 2 * 1024 * 1024) throw std::runtime_error("response");
        body.append(buffer, read);
    } while (read);
    return Decode(body, codePage);
}

JsonObject ParseJsonp(std::wstring const& body) {
    auto text = Trim(body);
    constexpr wchar_t prefix[] = L"campusLogin";
    if (text.rfind(prefix, 0) != 0) throw std::runtime_error("response");
    text = Trim(text.substr(11));
    if (!text.empty() && text.back() == L';') { text.pop_back(); text = Trim(text); }
    if (text.size() < 4 || text.front() != L'(' || text.back() != L')') throw std::runtime_error("response");
    JsonObject result;
    if (!JsonObject::TryParse(Trim(text.substr(1, text.size() - 2)), result)) throw std::runtime_error("response");
    return result;
}
std::wstring JsonText(JsonObject const& object, std::wstring const& key) {
    if (!object.HasKey(key)) return {};
    auto value = object.GetNamedValue(key);
    if (value.ValueType() == JsonValueType::String) return std::wstring(value.GetString());
    if (value.ValueType() == JsonValueType::Number) return std::to_wstring(static_cast<long long>(value.GetNumber()));
    return {};
}
Context ParseContext(std::wstring const& html, JsonObject const& status) {
    Context result;
    for (auto name : { L"v46ip", L"ss5", L"v4ip" }) {
        auto candidate = Variable(html, name);
        if (IsIp(candidate)) { result.ip = candidate; break; }
    }
    if (result.ip.empty()) for (auto name : { L"v46ip", L"ss5", L"v4ip" }) {
        auto candidate = JsonText(status, name);
        if (IsIp(candidate)) { result.ip = candidate; break; }
    }
    if (result.ip.empty()) throw std::runtime_error("ip");
    for (auto name : {L"ss4", L"olmac"}) {
        result.mac = NormalizeMac(Variable(html, name));
        if (!result.mac.empty()) break;
    }
    if (result.mac.empty()) for (auto name : {L"ss4", L"olmac"}) {
        result.mac = NormalizeMac(JsonText(status, name));
        if (!result.mac.empty()) break;
    }
    if (result.mac.empty()) result.mac = L"000000000000";
    std::wsmatch match;
    if (std::regex_search(html, match, std::wregex(L"(?:^|[^\\w])vlanid\\s*=\\s*['\"]?([0-9]{1,4})"))) result.vlan = std::stoi(match[1]);
    return result;
}
std::wstring DecimalIp(std::wstring const& ip) {
    if (!IsIp(ip)) throw std::runtime_error("ip");
    std::uint32_t address = 0, octet = 0;
    for (wchar_t c : ip + L".") {
        if (c == L'.') { address = (address << 8) | octet; octet = 0; }
        else octet = octet * 10 + static_cast<unsigned>(c - L'0');
    }
    return std::to_wstring(address);
}

JsonObject Portal::Api(Operation operation, Query query) {
    AddApiParameters(query);
    try {
        auto result = ParseJsonp(Request(operation, query));
        auto secret = query.find(L"user_password");
        if (secret != query.end() && !secret->second.empty()) SecureZeroMemory(secret->second.data(), secret->second.size() * sizeof(wchar_t));
        return result;
    } catch (...) {
        auto secret = query.find(L"user_password");
        if (secret != query.end() && !secret->second.empty()) SecureZeroMemory(secret->second.data(), secret->second.size() * sizeof(wchar_t));
        throw;
    }
}
std::wstring Portal::Request(Operation operation, Query const& query) {
    if (request_) return request_(operation, query);
    if (!gateway_) throw std::runtime_error("network");
    return gatewayRequest_(*gateway_, operation, query);
}
Status Portal::CheckSelected() {
    auto data = Api(Operation::Status);
    auto result = JsonText(data, L"result");
    if (result != L"0" && result != L"1") throw std::runtime_error("response");
    return { result == L"1", data };
}
Status Portal::Check() {
    // Discover a candidate using read-only status requests. Connect may refine
    // the candidate during read-only preparation, before sending a password.
    // Never retry a login/logout mutation on another host.
    if (request_ || gateway_) return CheckSelected();
    failedProbeMask_ = 0;
    std::exception_ptr failure;
    auto alternate = preferredGateway_ == Gateway::Campus ? Gateway::Wired : Gateway::Campus;
    for (auto candidate : {preferredGateway_, alternate}) {
        gateway_ = candidate;
        try { return CheckSelected(); }
        catch (std::runtime_error const& error) {
            std::string code = error.what();
            if (code != "network" && code != "response") { gateway_.reset(); throw; }
            failure = std::current_exception();
            failedProbeMask_ |= 1u << static_cast<unsigned>(candidate);
            gateway_.reset();
        } catch (...) { gateway_.reset(); throw; }
    }
    std::rethrow_exception(failure);
}
JsonObject Portal::LoadConfig(Context const& context) {
    auto configResponse = Api(Operation::Config, ConfigQuery(context));
    if (JsonText(configResponse, L"code") != L"1" || !configResponse.HasKey(L"data")
        || configResponse.GetNamedValue(L"data").ValueType() != JsonValueType::Object) throw std::runtime_error("config");
    return configResponse.GetNamedObject(L"data");
}
std::wstring Portal::FindCurrentMac(Context const& context, std::wstring const& account, JsonObject const& config) {
    auto findMode = JsonText(config, L"find_mac");
    auto response = Api(Operation::MacFind, {
        {L"user_account", account}, {L"login_method", JsonText(config, L"login_method")},
        {L"find_mac", findMode.empty() ? L"0" : findMode},
        {L"wlan_user_ip", context.ip}, {L"wlan_user_mac", context.mac}
    });
    auto result = JsonText(response, L"result");
    if ((result != L"1" && result != L"ok") || !response.HasKey(L"list")) throw std::runtime_error("logout-device");
    auto list = response.GetNamedArray(L"list");
    std::wstring matchingMac;
    for (auto const& item : list) {
        if (item.ValueType() != JsonValueType::Object) continue;
        auto device = item.GetObject();
        if (JsonText(device, L"online_ip") != context.ip) continue;
        auto mac = NormalizeMac(JsonText(device, L"online_mac"));
        if (mac.empty()) continue;
        std::transform(mac.begin(), mac.end(), mac.begin(), towupper);
        if (!matchingMac.empty() && matchingMac != mac) throw std::runtime_error("logout-device");
        matchingMac = std::move(mac);
    }
    // Never choose the first entry or a different IP from the account's device list.
    if (matchingMac.empty()) throw std::runtime_error("logout-device");
    return matchingMac;
}
ConnectionResult Portal::Connect(Credentials const& credentials, bool waitForNetwork) {
    Status state;
    for (int attempt = 0; ; ++attempt) {
        try { state = Check(); break; }
        catch (std::runtime_error const& error) {
            if (!waitForNetwork || attempt >= 3 || std::string(error.what()) != "network") throw;
            std::this_thread::sleep_for(std::chrono::seconds(2));
        }
    }
    if (state.online) return ConnectionResult::AlreadyOnline;
    if (credentials.account.empty() || credentials.password.empty()) throw std::runtime_error("credentials");
    Context context;
    JsonObject config{nullptr};
    std::wstring method;
    auto prepare = [&] {
        context = ParseContext(Request(Operation::Page, {}), state.data);
        config = LoadConfig(context);
        method = JsonText(config, L"login_method");
        if (method != L"1" && method != L"9") throw std::runtime_error("protocol");
    };
    try { prepare(); }
    catch (std::runtime_error const& error) {
        auto code = std::string(error.what());
        if (request_ || !gateway_ || (code != "network" && code != "response" && code != "ip" && code != "config" && code != "protocol")) throw;
        auto firstGateway = *gateway_;
        auto alternate = firstGateway == Gateway::Campus ? Gateway::Wired : Gateway::Campus;
        if (failedProbeMask_ & (1u << static_cast<unsigned>(alternate))) throw;
        auto firstFailure = std::current_exception();
        gateway_ = alternate;
        // Still read-only: no password has been sent. A gateway answering
        // chkstatus alone is insufficient if it cannot describe this device.
        try {
            state = CheckSelected();
            if (state.online) return ConnectionResult::AlreadyOnline;
            prepare();
        } catch (std::runtime_error const& alternateError) {
            if (code != "network" && std::string(alternateError.what()) == "network") {
                gateway_ = firstGateway;
                std::rethrow_exception(firstFailure);
            }
            throw;
        }
    }
    std::wstring prefix;
    if (JsonText(config, L"account_prefix") == L"1") prefix = JsonText(config, L"custom_perceive") == L"1" ? L",b," : L",0,";
    auto reply = Api(Operation::Login, {
        { L"login_method", method }, { L"user_account", prefix + credentials.account + JsonText(config, L"account_suffix") },
        { L"user_password", credentials.password }, { L"wlan_user_ip", context.ip },
        { L"wlan_user_ipv6", L"" }, { L"wlan_user_mac", context.mac },
        { L"wlan_ac_ip", L"" }, { L"wlan_ac_name", L"" }, { L"terminal_type", L"1" }
    });
    auto result = JsonText(reply, L"result");
    if (result != L"1" && result != L"ok") throw winrt::hresult_error(E_ACCESSDENIED, FailureMessage(reply, credentials.password));
    for (int attempt = 0; attempt < 3; ++attempt) {
        if (Check().online) return ConnectionResult::Connected;
        if (attempt < 2) std::this_thread::sleep_for(std::chrono::milliseconds(650));
    }
    throw std::runtime_error("unconfirmed");
}
DisconnectionResult Portal::Disconnect() {
    auto state = Check();
    if (!state.online) return DisconnectionResult::AlreadyOffline;
    auto context = ParseContext(Request(Operation::Page, {}), state.data);
    auto config = LoadConfig(context);
    auto method = JsonText(config, L"login_method");
    auto acLogout = JsonText(config, L"ac_logout");
    auto registerMode = JsonText(config, L"register_mode");
    auto unbindMac = JsonText(config, L"un_bind_mac");
    if (acLogout.empty()) acLogout = L"0";
    if (registerMode.empty()) registerMode = L"0";
    if (acLogout != L"0" && acLogout != L"1" && acLogout != L"2") throw std::runtime_error("protocol");
    if (!unbindMac.empty() && unbindMac != L"0" && unbindMac != L"1") throw std::runtime_error("protocol");
    JsonObject reply;
    bool unbound = false;
    if (unbindMac == L"1" && (registerMode == L"1" || registerMode == L"3" || registerMode == L"4")) {
        auto account = Trim(JsonText(state.data, L"uid"));
        if (account.empty()) throw std::runtime_error("logout-account");
        auto mac = NormalizeMac(context.mac);
        if (mac.empty()) mac = FindCurrentMac(context, account, config);
        std::transform(mac.begin(), mac.end(), mac.begin(), towupper);
        reply = Api(Operation::MacUnbind, {
            {L"user_account", account}, {L"wlan_user_mac", mac}, {L"wlan_user_ip", DecimalIp(context.ip)}
        });
        auto result = JsonText(reply, L"result");
        unbound = result == L"1" || result == L"ok";
        // The page falls back to ordinary logout only after an explicit rejection.
        if (!unbound && result != L"0") throw std::runtime_error("response");
    }
    if (!unbound && method == L"0" && acLogout == L"0") reply = Api(Operation::LegacyLogout);
    else if (!unbound && (method == L"1" || method == L"9")) {
        // a40.js overrides the generic local logout with this portal request.
        // These two fixed compatibility values are not the user's credentials.
        reply = Api(Operation::Logout, {
            {L"login_method", method}, {L"user_account", L"drcom"}, {L"user_password", L"123"},
            {L"ac_logout", acLogout}, {L"register_mode", registerMode},
            {L"wlan_user_ip", context.ip}, {L"wlan_user_ipv6", L""},
            {L"wlan_vlan_id", std::to_wstring(context.vlan)}, {L"wlan_user_mac", context.mac},
            {L"wlan_ac_ip", L""}, {L"wlan_ac_name", L""}
        });
    } else if (!unbound) throw std::runtime_error("protocol");
    auto result = JsonText(reply, L"result");
    if (result != L"1" && result != L"ok") throw std::runtime_error("logout-rejected");
    bool previouslyOffline = false;
    for (int attempt = 0; attempt < 12; ++attempt) {
        bool offline = !Check().online;
        if (offline && previouslyOffline) return DisconnectionResult::Disconnected;
        previouslyOffline = offline;
        if (attempt < 11) std::this_thread::sleep_for(std::chrono::milliseconds(750));
    }
    throw std::runtime_error("logout-unconfirmed");
}
std::wstring DiagnoseNetwork(GatewayTransport const& transport) {
    std::wstring report = L"CampusLink 1.1.4 网络诊断\n仅检查学校已知入口，不登录或注销。\n\n";
    for (auto gateway : {Gateway::Campus, Gateway::Wired}) {
        report += L"入口：" + GatewayHost(gateway) + L"\n";
        try {
            Query statusQuery; AddApiParameters(statusQuery);
            auto status = ParseJsonp(transport(gateway, Operation::Status, statusQuery));
            auto result = JsonText(status, L"result");
            if (result != L"0" && result != L"1") throw std::runtime_error("response");
            report += L"状态接口（80）：可达，" + std::wstring(result == L"1" ? L"在线。\n" : L"待认证。\n");
            auto context = ParseContext(transport(gateway, Operation::Page, {}), status);
            report += L"当前设备信息：已获取（具体内容不记录）。\n";
            auto configQuery = ConfigQuery(context); AddApiParameters(configQuery);
            auto response = ParseJsonp(transport(gateway, Operation::Config, configQuery));
            if (JsonText(response, L"code") != L"1" || !response.HasKey(L"data")) throw std::runtime_error("config");
            auto method = JsonText(response.GetNamedObject(L"data"), L"login_method");
            report += L"配置接口（801）：可达，" + std::wstring(method == L"1" || method == L"9" ? L"认证类型受支持。\n" : L"认证类型暂不支持，请使用学校登录页。\n");
        } catch (std::exception const& error) { report += FriendlyError(error) + L"\n"; }
        catch (...) { report += L"响应无法识别，请使用学校登录页。\n"; }
        report += L"\n";
    }
    return report + L"报告不包含账号、密码、设备 IP 或 MAC。";
}
}
