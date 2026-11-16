#include "pch.h"
#include "Store.hpp"
#include <wincred.h>
#include <shlobj.h>
#include <filesystem>
#include <fstream>
#include <cstdio>

namespace campus {
namespace {
std::filesystem::path SettingsPath() {
    PWSTR raw{};
    winrt::check_hresult(SHGetKnownFolderPath(FOLDERID_LocalAppData, 0, nullptr, &raw));
    std::filesystem::path directory = std::filesystem::path(raw) / L"CampusLink";
    CoTaskMemFree(raw);
    std::filesystem::create_directories(directory);
    return directory / L"settings.json";
}
std::wstring CurrentExecutable() {
    std::wstring path(32768, L'\0');
    DWORD size = GetModuleFileNameW(nullptr, path.data(), static_cast<DWORD>(path.size()));
    if (!size || size >= path.size()) throw std::runtime_error("startup");
    path.resize(size);
    return path;
}
struct RegistryKey {
    HKEY value{};
    ~RegistryKey() { if (value) RegCloseKey(value); }
};
}
std::optional<Credentials> ReadCredentials(std::wstring const& target) {
    PCREDENTIALW record{};
    if (!CredReadW(target.c_str(), CRED_TYPE_GENERIC, 0, &record)) {
        if (GetLastError() == ERROR_NOT_FOUND) return {};
        throw std::runtime_error("vault");
    }
    Credentials value;
    if (record->UserName) value.account = record->UserName;
    if (record->CredentialBlobSize % sizeof(wchar_t) != 0 || record->CredentialBlobSize > CRED_MAX_CREDENTIAL_BLOB_SIZE) {
        CredFree(record); throw std::runtime_error("vault");
    }
    if (record->CredentialBlobSize) value.password.assign(reinterpret_cast<wchar_t const*>(record->CredentialBlob), record->CredentialBlobSize / sizeof(wchar_t));
    if (record->CredentialBlobSize) SecureZeroMemory(record->CredentialBlob, record->CredentialBlobSize);
    CredFree(record);
    return value;
}
void WriteCredentials(Credentials const& credentials, std::wstring const& target) {
    if (credentials.password.size() * sizeof(wchar_t) > CRED_MAX_CREDENTIAL_BLOB_SIZE) throw std::runtime_error("credentials");
    CREDENTIALW record{};
    record.Type = CRED_TYPE_GENERIC;
    record.TargetName = const_cast<wchar_t*>(target.c_str());
    record.UserName = const_cast<wchar_t*>(credentials.account.c_str());
    record.CredentialBlob = reinterpret_cast<BYTE*>(const_cast<wchar_t*>(credentials.password.data()));
    record.CredentialBlobSize = static_cast<DWORD>(credentials.password.size() * sizeof(wchar_t));
    record.Persist = CRED_PERSIST_LOCAL_MACHINE;
    record.Comment = const_cast<wchar_t*>(L"校园网 · 喀什大学。本机当前 Windows 用户的校园网登录凭据。");
    if (!CredWriteW(&record, 0)) throw std::runtime_error("vault");
}
void ForgetCredentials(std::wstring const& target) {
    if (!CredDeleteW(target.c_str(), CRED_TYPE_GENERIC, 0) && GetLastError() != ERROR_NOT_FOUND) throw std::runtime_error("vault");
}
Preferences ReadPreferences() {
    try {
        std::ifstream input(SettingsPath(), std::ios::binary);
        std::string text((std::istreambuf_iterator<char>(input)), {});
        if (text.size() > 1024) return {};
        winrt::Windows::Data::Json::JsonObject object;
        if (!winrt::Windows::Data::Json::JsonObject::TryParse(winrt::to_hstring(text), object)) return {};
        return { object.GetNamedBoolean(L"autoLogin", true), object.GetNamedBoolean(L"autoClose", true) };
    } catch (...) { return {}; }
}
void WritePreferences(Preferences preferences) {
    auto path = SettingsPath(); auto temporary = path; temporary += L".tmp";
    {
        std::ofstream output(temporary, std::ios::binary | std::ios::trunc);
        output << "{\"autoLogin\":" << (preferences.autoLogin ? "true" : "false") << ",\"autoClose\":" << (preferences.autoClose ? "true" : "false") << "}";
        output.close(); if (!output) throw std::runtime_error("settings");
    }
    if (!MoveFileExW(temporary.c_str(), path.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) throw std::runtime_error("settings");
}
std::wstring StartupCommand(std::wstring const& executable) {
    if (executable.empty() || executable.find_first_of(L"\"\r\n") != std::wstring::npos) throw std::runtime_error("startup");
    return L"\"" + executable + L"\" --startup";
}
bool StartupEnabled(std::wstring const& keyPath) {
    RegistryKey key;
    auto result = RegOpenKeyExW(HKEY_CURRENT_USER, keyPath.c_str(), 0, KEY_QUERY_VALUE, &key.value);
    if (result == ERROR_FILE_NOT_FOUND) return false;
    if (result != ERROR_SUCCESS) throw std::runtime_error("startup");
    DWORD type{}, bytes{};
    result = RegQueryValueExW(key.value, L"CampusLink", nullptr, &type, nullptr, &bytes);
    if (result == ERROR_FILE_NOT_FOUND) return false;
    if (result != ERROR_SUCCESS || type != REG_SZ || bytes > 65536 || bytes < sizeof(wchar_t) || bytes % sizeof(wchar_t)) throw std::runtime_error("startup");
    std::wstring command(bytes / sizeof(wchar_t), L'\0');
    if (RegQueryValueExW(key.value, L"CampusLink", nullptr, &type, reinterpret_cast<BYTE*>(command.data()), &bytes) != ERROR_SUCCESS) throw std::runtime_error("startup");
    while (!command.empty() && command.back() == L'\0') command.pop_back();
    return command == StartupCommand(CurrentExecutable());
}
void SetStartupEnabled(bool enabled, std::wstring const& keyPath) {
    RegistryKey key;
    if (!enabled) {
        auto result = RegOpenKeyExW(HKEY_CURRENT_USER, keyPath.c_str(), 0, KEY_SET_VALUE, &key.value);
        if (result == ERROR_FILE_NOT_FOUND) return;
        if (result != ERROR_SUCCESS) throw std::runtime_error("startup");
        result = RegDeleteValueW(key.value, L"CampusLink");
        if (result != ERROR_SUCCESS && result != ERROR_FILE_NOT_FOUND) throw std::runtime_error("startup");
        return;
    }
    if (RegCreateKeyExW(HKEY_CURRENT_USER, keyPath.c_str(), 0, nullptr, 0, KEY_SET_VALUE, nullptr, &key.value, nullptr) != ERROR_SUCCESS) throw std::runtime_error("startup");
    auto command = StartupCommand(CurrentExecutable());
    if (RegSetValueExW(key.value, L"CampusLink", 0, REG_SZ, reinterpret_cast<BYTE const*>(command.c_str()), static_cast<DWORD>((command.size() + 1) * sizeof(wchar_t))) != ERROR_SUCCESS) throw std::runtime_error("startup");
}
void LogLogoutStage(std::string const& stage, bool reset) noexcept {
    try {
        std::ofstream output(SettingsPath().parent_path() / L"last-logout.log", std::ios::binary | (reset ? std::ios::trunc : std::ios::app));
        SYSTEMTIME now{}; GetSystemTime(&now);
        char timestamp[40]{};
        sprintf_s(timestamp, "%04u-%02u-%02uT%02u:%02u:%02u.%03uZ ",
            static_cast<unsigned>(now.wYear), static_cast<unsigned>(now.wMonth), static_cast<unsigned>(now.wDay),
            static_cast<unsigned>(now.wHour), static_cast<unsigned>(now.wMinute), static_cast<unsigned>(now.wSecond), static_cast<unsigned>(now.wMilliseconds));
        output << timestamp << stage << '\n';
    } catch (...) {}
}
std::wstring FriendlyError(std::exception const& error) {
    std::string code = error.what();
    if (auto failure = dynamic_cast<NetworkFailure const*>(&error)) {
        auto address = GatewayHost(failure->gateway) + L":" + std::to_wstring(RequestPort(failure->operation));
        std::wstring detail = failure->httpStatus ? L"HTTP " + std::to_wstring(failure->httpStatus) : L"Windows 错误 " + std::to_wstring(failure->systemCode);
        std::wstring message = L"无法完成对学校入口 " + address + L" 的请求（" + failure->stage + L"，" + detail + L"）。";
        if (failure->operation == Operation::Login || failure->operation == Operation::Logout || failure->operation == Operation::LegacyLogout || failure->operation == Operation::MacUnbind)
            return message + L"请求结果尚未确认，请先刷新状态或使用学校登录页确认。";
        if (RequestPort(failure->operation) == 801)
            return message + L"网页登录页可访问，也可能是认证接口未连通。请运行“网络诊断”，或使用学校登录页。";
        return message + L"请确认校园网连接；如网页可以打开，请运行“网络诊断”。";
    }
    if (code == "network") return L"暂时无法连接已知学校认证入口。请确认校园网连接；如网页可以打开，请运行“网络诊断”。";
    if (code == "ip") return L"未获取到校园网 IP。请重新连接校园网后重试。";
    if (code == "protocol" || code == "config") return L"学校的认证配置发生了变化。请先使用原登录页，工具需要更新。";
    if (code == "credentials") return L"请完整填写校园网账号和密码。";
    if (code == "vault") return L"无法访问本机保存的账号。请重新填写并保存账号密码。";
    if (code == "settings") return L"无法保存偏好设置，请确认本机用户目录可写。";
    if (code == "startup") return L"无法更改开机启动设置。请检查当前 Windows 用户是否允许设置启动应用。";
    if (code == "logout-rejected") return L"网关未接受注销请求。请刷新状态，或打开学校登录页手动注销。";
    if (code == "logout-unconfirmed") return L"注销请求已提交，但网关尚未确认离线。请刷新状态或查看学校登录页。";
    if (code == "logout-account") return L"网关未提供当前在线账号，无法安全注销。请刷新状态或使用学校登录页。";
    if (code == "logout-device") return L"未找到与本机当前 IP 匹配的认证绑定，已停止操作，避免影响其他设备。请使用学校登录页注销。";
    if (code == "unconfirmed") return L"请求已提交，但网关尚未确认在线。请稍后重试，或查看原登录页。";
    return L"认证服务器返回的数据暂时无法识别，请重试或打开原登录页。";
}
}
