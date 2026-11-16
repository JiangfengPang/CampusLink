#pragma once
#include "Portal.hpp"
namespace campus {
struct Preferences { bool autoLogin{true}; bool autoClose{true}; };
std::optional<Credentials> ReadCredentials(std::wstring const& target = L"CampusLink.KashiUniversity.v1");
void WriteCredentials(Credentials const& credentials, std::wstring const& target = L"CampusLink.KashiUniversity.v1");
void ForgetCredentials(std::wstring const& target = L"CampusLink.KashiUniversity.v1");
Preferences ReadPreferences();
void WritePreferences(Preferences preferences);
std::wstring StartupCommand(std::wstring const& executable);
bool StartupEnabled(std::wstring const& keyPath = L"Software\\Microsoft\\Windows\\CurrentVersion\\Run");
void SetStartupEnabled(bool enabled, std::wstring const& keyPath = L"Software\\Microsoft\\Windows\\CurrentVersion\\Run");
void LogLogoutStage(std::string const& stage, bool reset = false) noexcept;
std::wstring FriendlyError(std::exception const& error);
}
