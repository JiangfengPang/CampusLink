#include "pch.h"
#include "Portal.hpp"
#include "Store.hpp"
#include <iostream>

using namespace campus;
using namespace winrt::Windows::Data::Json;
void Require(bool value, char const* message) { if (!value) throw std::runtime_error(message); }
template<class F> void Reject(F&& action, char const* message) { bool failed = false; try { action(); } catch (...) { failed = true; } Require(failed, message); }

int wmain(int argc, wchar_t** argv) {
    winrt::init_apartment();
    if (argc > 1 && std::wstring(argv[1]) == L"--live") {
        try { auto state = Portal().Check(); std::cout << "{\"reachable\":true,\"online\":" << (state.online ? "true" : "false") << "}\n"; return 0; }
        catch (...) { std::cout << "{\"reachable\":false,\"online\":false}\n"; return 2; }
    }
    if (argc > 1 && std::wstring(argv[1]) == L"--check-logout-route") {
        struct Captured {};
        bool portalRoute = false, unbindRoute = false, completeContext = false;
        try {
            Portal inspect([&](Gateway gateway, Operation operation, Query const& query) -> std::wstring {
                if (operation == Operation::Logout || operation == Operation::LegacyLogout || operation == Operation::MacUnbind) {
                    portalRoute = operation == Operation::Logout && RequestPort(operation) == 801;
                    unbindRoute = operation == Operation::MacUnbind && RequestPort(operation) == 801;
                    completeContext = unbindRoute ? (!query.at(L"user_account").empty() && query.at(L"wlan_user_mac").size() == 12
                        && query.at(L"wlan_user_mac") != L"000000000000" && query.at(L"wlan_user_ip").find(L'.') == std::wstring::npos)
                        : !portalRoute || (!query.at(L"wlan_user_ip").empty() && !query.at(L"wlan_user_mac").empty()
                        && query.at(L"login_method") == L"1" && query.at(L"ac_logout") == L"0" && query.at(L"register_mode") == L"1");
                    // Stop here: never forward a mutation to the real school gateway.
                    throw Captured{};
                }
                if (operation != Operation::Status && operation != Operation::Page && operation != Operation::Config && operation != Operation::MacFind) throw std::runtime_error("read-only guard");
                return HttpRequestAt(gateway, operation, query);
            });
            inspect.Disconnect();
            std::cout << "{\"requestPrepared\":false,\"alreadyOffline\":true,\"logoutSent\":false}\n";
            return 0;
        } catch (Captured const&) {
            std::cout << "{\"requestPrepared\":true,\"portal801\":" << (portalRoute ? "true" : "false")
                << ",\"macUnbind801\":" << (unbindRoute ? "true" : "false")
                << ",\"contextComplete\":" << (completeContext ? "true" : "false") << ",\"logoutSent\":false}\n";
            return completeContext ? 0 : 2;
        } catch (...) { std::cout << "{\"requestPrepared\":false,\"logoutSent\":false,\"error\":true}\n"; return 2; }
    }
    try {
        Require(Encode(L"a&+?=#% 中文") == L"a%26%2B%3F%3D%23%25%20%E4%B8%AD%E6%96%87", "password URL encoding");
        Require(BuildPath(Operation::Login, {{L"user_password",L"p&a"}}) == L"/eportal/portal/login?user_password=p%26a", "login endpoint");
        Require(BuildPath(Operation::Logout, {}) == L"/eportal/portal/logout" && RequestPort(Operation::Logout) == 801, "configured Portal logout uses port 801");
        Require(BuildPath(Operation::LegacyLogout, {}) == L"/drcom/logout" && RequestPort(Operation::LegacyLogout) == 80, "local protocol retains port 80 logout");
        Require(BuildPath(Operation::MacUnbind, {}) == L"/eportal/portal/mac/unbind" && RequestPort(Operation::MacUnbind) == 801, "red school button uses MAC-unbind endpoint");
        Require(DecimalIp(L"10.20.30.40") == L"169090600" && DecimalIp(L"172.17.1.90") == L"2886795610", "MAC-unbind uses unsigned decimal IPv4 like school JavaScript");
        Require(JsonText(ParseJsonp(L" \r\ncampusLogin ( {\"result\":1} ); "), L"result") == L"1", "strict JSONP accepts valid response");
        Reject([] { ParseJsonp(L"campusLogin({\"result\":1});alert(1)"); }, "JSONP trailing code");
        Reject([] { ParseJsonp(L"other({\"result\":1})"); }, "JSONP wrong callback");
        Reject([] { ParseJsonp(L"campusLogin({\"result\":1}garbage)"); }, "JSONP invalid JSON");
        auto context = ParseContext(L"<script>v4ip='10.20.30.40';ss4='12:34:56:ab:cd:ef';vlanid=7;</script>", JsonObject());
        Require(context.ip == L"10.20.30.40" && context.mac == L"123456abcdef" && context.vlan == 7, "dynamic network context");
        Reject([] { ParseContext(L"v4ip='999.1.2.3'", JsonObject()); }, "invalid IP rejected");
        auto fallback = JsonObject::Parse(L"{\"v46ip\":\"10.1.2.3\"}");
        Require(ParseContext(L"", fallback).ip == L"10.1.2.3", "gateway status IP fallback");
        auto macFallback = JsonObject::Parse(L"{\"v46ip\":\"10.1.2.3\",\"ss4\":\"12:34:56:ab:cd:ef\"}");
        Require(ParseContext(L"ss4='000000000000'", macFallback).mac == L"123456abcdef", "placeholder page MAC falls back to current status");

        int submissions = 0, statusQueries = 0; bool online = false, rejectLogin = false, unsupported = false, confirmOnline = true;
        Transport server = [&](Operation operation, Query const& query) -> std::wstring {
            switch (operation) {
            case Operation::Status: ++statusQueries; return online ? L"campusLogin({\"result\":1});" : L"campusLogin({\"result\":0,\"v46ip\":\"10.20.30.40\"});";
            case Operation::Page: return L"v4ip='10.20.30.40';ss4='123456abcdef';vlanid=7;";
            case Operation::Config:
                if (unsupported) return L"campusLogin({\"code\":1,\"data\":{\"login_method\":\"7\"}});";
                return L"campusLogin({\"code\":1,\"data\":{\"login_method\":\"1\",\"account_prefix\":\"1\",\"account_suffix\":\"\"}});";
            case Operation::Login:
                ++submissions;
                Require(query.at(L"user_account") == L",0,test-only", "desktop account prefix");
                Require(query.at(L"user_password") == L"test &+ 中文", "password preserved");
                Require(query.at(L"wlan_user_ip") == L"10.20.30.40", "current IP submitted");
                if (rejectLogin) return L"campusLogin({\"result\":0,\"msg\":\"test &+ 中文 invalid\"});";
                online = confirmOnline; return L"campusLogin({\"result\":1});";
            }
            throw std::runtime_error("unexpected operation");
        };
        Portal portal(server);
        Credentials credentials(L"test-only", L"test &+ 中文");
        Require(portal.Connect(credentials) == ConnectionResult::Connected, "offline authentication completes");
        Require(submissions == 1, "one authentication per action");
        Require(statusQueries >= 2, "gateway must confirm success");
        Require(portal.Connect(credentials) == ConnectionResult::AlreadyOnline && submissions == 1, "already online skips authentication");
        online = false; rejectLogin = true;
        bool failed = false;
        try { portal.Connect(credentials); }
        catch (winrt::hresult_error const& error) {
            failed = true; Require(std::wstring(error.message()).find(credentials.password) == std::wstring::npos, "password excluded from error");
        }
        Require(failed && submissions == 2, "authentication rejection is not retried");
        rejectLogin = false; unsupported = true;
        Reject([&] { portal.Connect(credentials); }, "unsupported protocol must fail before password submission");
        Require(submissions == 2, "unsupported protocol leaked authentication");
        unsupported = false; confirmOnline = false;
        Reject([&] { portal.Connect(credentials); }, "unconfirmed gateway state must not count as success");
        Require(submissions == 3, "unconfirmed state triggered another password submission");

        int logoutSubmissions = 0, logoutChecks = 0;
        bool logoutOnline = true, rejectLogout = false, confirmLogout = true;
        bool legacyLogout = false, unsupportedLogout = false, missingLogoutContext = false, transientOffline = false;
        int offlineRepliesRemaining = 0;
        Portal logoutPortal([&](Operation operation, Query const& query) -> std::wstring {
            if (operation == Operation::Status) {
                ++logoutChecks;
                if (offlineRepliesRemaining > 0) { --offlineRepliesRemaining; return L"campusLogin({\"result\":0});"; }
                return logoutOnline ? L"campusLogin({\"result\":1});" : L"campusLogin({\"result\":0});";
            }
            if (operation == Operation::Page) return missingLogoutContext ? L"" : L"v4ip='10.20.30.40';ss4='123456abcdef';vlanid=7;";
            if (operation == Operation::Config) {
                if (unsupportedLogout) return L"campusLogin({\"code\":1,\"data\":{\"login_method\":\"7\"}});";
                if (legacyLogout) return L"campusLogin({\"code\":1,\"data\":{\"login_method\":\"0\",\"ac_logout\":\"0\"}});";
                // Current school loadConfig: login_method=1, ac_logout=0, register_mode=1.
                return L"campusLogin({\"code\":1,\"data\":{\"login_method\":\"1\",\"ac_logout\":\"0\",\"register_mode\":\"1\"}});";
            }
            Require(operation == (legacyLogout ? Operation::LegacyLogout : Operation::Logout), "logout chooses the configured school protocol");
            if (!legacyLogout) {
                Require(query.at(L"login_method") == L"1" && query.at(L"ac_logout") == L"0" && query.at(L"register_mode") == L"1", "Portal logout includes current school settings");
                Require(query.at(L"wlan_user_ip") == L"10.20.30.40" && query.at(L"wlan_user_mac") == L"123456abcdef" && query.at(L"wlan_vlan_id") == L"7", "Portal logout targets current network session");
                Require(query.at(L"user_account") == L"drcom" && query.at(L"user_password") == L"123", "logout uses school frontend compatibility constants rather than saved credentials");
            } else Require(query.count(L"user_password") == 0 && query.count(L"wlan_user_ip") == 0, "local logout does not use Portal parameters");
            ++logoutSubmissions;
            if (rejectLogout) return L"campusLogin({\"result\":0});";
            logoutOnline = !confirmLogout;
            if (transientOffline) offlineRepliesRemaining = 1;
            return L"campusLogin({\"result\":\"ok\"});";
        });
        Require(logoutPortal.Disconnect() == DisconnectionResult::Disconnected, "logout completes only after gateway confirms offline");
        Require(logoutSubmissions == 1 && logoutChecks >= 3, "logout submits once and confirms offline twice");
        Require(logoutPortal.Disconnect() == DisconnectionResult::AlreadyOffline && logoutSubmissions == 1, "offline logout submits nothing");
        logoutOnline = true; rejectLogout = true;
        Reject([&] { logoutPortal.Disconnect(); }, "rejected logout must fail");
        Require(logoutSubmissions == 2, "rejected logout is not retried");
        rejectLogout = false; confirmLogout = false; transientOffline = true;
        Reject([&] { logoutPortal.Disconnect(); }, "transient offline response must not count as successful logout");
        Require(logoutSubmissions == 3, "unconfirmed logout is not resubmitted");
        transientOffline = false; unsupportedLogout = true;
        Reject([&] { logoutPortal.Disconnect(); }, "unknown logout protocol fails before mutation");
        Require(logoutSubmissions == 3, "unknown logout protocol sends nothing");
        unsupportedLogout = false; missingLogoutContext = true;
        Reject([&] { logoutPortal.Disconnect(); }, "missing session context fails before logout");
        Require(logoutSubmissions == 3, "missing session context sends nothing");
        missingLogoutContext = false; legacyLogout = true; confirmLogout = true;
        Require(logoutPortal.Disconnect() == DisconnectionResult::Disconnected && logoutSubmissions == 4, "explicit local protocol uses legacy logout once");

        bool bindingOnline = true, rejectUnbind = false, failUnbindTransport = false, missingCurrentDevice = false, missingCurrentAccount = false;
        int unbindRequests = 0, ordinaryRequests = 0;
        Portal bindingPortal([&](Operation operation, Query const& query) -> std::wstring {
            if (operation == Operation::Status) return !bindingOnline ? L"campusLogin({\"result\":0});" : missingCurrentAccount
                ? L"campusLogin({\"result\":1});" : L"campusLogin({\"result\":1,\"uid\":\"online-account\"});";
            if (operation == Operation::Page) return L"v4ip='10.20.30.40';ss4='000000000000';vlanid=7;";
            if (operation == Operation::Config) return L"campusLogin({\"code\":1,\"data\":{\"login_method\":\"1\",\"ac_logout\":\"0\",\"register_mode\":\"1\",\"un_bind_mac\":\"1\",\"find_mac\":\"0\"}});";
            if (operation == Operation::MacFind) {
                Require(query.at(L"user_account") == L"online-account" && query.at(L"wlan_user_ip") == L"10.20.30.40", "MAC lookup uses active gateway identity and this IP");
                if (missingCurrentDevice) return L"campusLogin({\"result\":1,\"list\":[{\"online_ip\":\"10.20.30.99\",\"online_mac\":\"111122223333\"}]});";
                return L"campusLogin({\"result\":1,\"list\":[{\"online_ip\":\"10.20.30.99\",\"online_mac\":\"111122223333\"},{\"online_ip\":\"10.20.30.40\",\"online_mac\":\"12:34:56:ab:cd:ef\"}]});";
            }
            if (operation == Operation::MacUnbind) {
                ++unbindRequests;
                Require(query.at(L"user_account") == L"online-account" && query.at(L"wlan_user_mac") == L"123456ABCDEF" && query.at(L"wlan_user_ip") == L"169090600", "unbind targets only matching current device with uppercase MAC and decimal IP");
                Require(query.count(L"user_password") == 0, "unbind never reads or sends a saved password");
                if (failUnbindTransport) throw std::runtime_error("network");
                if (rejectUnbind) return L"campusLogin({\"result\":0});";
                bindingOnline = false;
                return L"campusLogin({\"result\":1});";
            }
            Require(operation == Operation::Logout, "unbind cannot trigger authentication or another device operation");
            ++ordinaryRequests; bindingOnline = false;
            return L"campusLogin({\"result\":1});";
        });
        Require(bindingPortal.Disconnect() == DisconnectionResult::Disconnected && unbindRequests == 1 && ordinaryRequests == 0, "enabled school MAC-unbind flow replaces ordinary logout");
        bindingOnline = true; rejectUnbind = true;
        Require(bindingPortal.Disconnect() == DisconnectionResult::Disconnected && unbindRequests == 2 && ordinaryRequests == 1, "explicit unbind rejection follows school ordinary-logout fallback once");
        bindingOnline = true; rejectUnbind = false; failUnbindTransport = true;
        Reject([&] { bindingPortal.Disconnect(); }, "unknown unbind transport outcome must fail");
        Require(unbindRequests == 3 && ordinaryRequests == 1, "transport failure must not issue a second mutation");
        failUnbindTransport = false; missingCurrentDevice = true;
        Reject([&] { bindingPortal.Disconnect(); }, "other bound devices are never selected");
        Require(unbindRequests == 3, "no current device match sends no unbind request");
        missingCurrentDevice = false; missingCurrentAccount = true;
        Reject([&] { bindingPortal.Disconnect(); }, "missing gateway identity does not use input credentials");
        Require(unbindRequests == 3, "missing identity sends no unbind request");

        Require(GatewayHost(Gateway::Campus) == L"172.17.1.90" && GatewayUrl(Gateway::Wired) == L"http://172.31.0.10/a79.htm", "only confirmed school gateway destinations");
        Reject([] { GatewayHost(static_cast<Gateway>(99)); }, "unknown gateway is never accepted");
        int campusProbes = 0, wiredLogins = 0, wiredLogouts = 0;
        bool wiredOnline = false, loginTransportFailure = false, unsupportedWired = false, confirmationFailure = false;
        GatewayTransport dualGateway = [&](Gateway gateway, Operation operation, Query const& query) -> std::wstring {
            if (gateway == Gateway::Campus) {
                Require(operation == Operation::Status && query.count(L"user_password") == 0, "gateway discovery is status-only");
                ++campusProbes;
                throw NetworkFailure(gateway, operation, L"发送请求", 12029);
            }
            if (operation == Operation::Status) {
                if (confirmationFailure && wiredOnline) throw NetworkFailure(gateway, operation, L"接收响应", 12002);
                return wiredOnline ? L"campusLogin({\"result\":1});" : L"campusLogin({\"result\":0,\"v46ip\":\"10.40.50.60\"});";
            }
            if (operation == Operation::Page) return L"v4ip='10.40.50.60';ss4='AABBCCDDEEFF';vlanid=8;";
            if (operation == Operation::Config) return unsupportedWired
                ? L"campusLogin({\"code\":1,\"data\":{\"login_method\":7}});"
                : L"campusLogin({\"code\":1,\"data\":{\"login_method\":1,\"un_bind_mac\":0}});";
            if (operation == Operation::Login) {
                ++wiredLogins;
                Require(query.at(L"wlan_user_ip") == L"10.40.50.60", "wired login derives IP from current gateway, not another user's browser URL");
                Require(query.at(L"user_password") == credentials.password, "only selected gateway receives password");
                if (loginTransportFailure) throw NetworkFailure(gateway, operation, L"接收响应", 12002);
                wiredOnline = true; return L"campusLogin({\"result\":1});";
            }
            Require(operation == Operation::Logout, "wired logout uses configured endpoint");
            ++wiredLogouts; wiredOnline = false;
            return L"campusLogin({\"result\":1});";
        };
        Portal wiredPortal(dualGateway);
        Require(wiredPortal.Connect(credentials) == ConnectionResult::Connected && wiredLogins == 1 && campusProbes == 1, "unreachable campus gateway falls back to wired gateway before authentication");
        Require(wiredPortal.SelectedGateway() == Gateway::Wired, "wired endpoint selected");
        Require(wiredPortal.Check().online && campusProbes == 1, "selected gateway remains pinned for status confirmation");
        Require(wiredPortal.Disconnect() == DisconnectionResult::Disconnected && wiredLogouts == 1 && campusProbes == 1, "logout stays on selected wired endpoint");
        loginTransportFailure = true;
        Portal failedWired(dualGateway);
        Reject([&] { failedWired.Connect(credentials); }, "uncertain wired login outcome surfaces error");
        Require(wiredLogins == 2 && campusProbes == 2, "failed login is not resubmitted or tried on another gateway");
        loginTransportFailure = false; unsupportedWired = true;
        Portal unsupportedWiredPortal(dualGateway);
        Reject([&] { unsupportedWiredPortal.Connect(credentials); }, "wired protocol validated before password submission");
        Require(wiredLogins == 2 && campusProbes == 3, "unsupported wired protocol submits no password");
        unsupportedWired = false; confirmationFailure = true;
        Portal failedConfirmation(dualGateway);
        Reject([&] { failedConfirmation.Connect(credentials); }, "failed post-login confirmation is not reported as success");
        Require(wiredLogins == 3 && campusProbes == 4, "failed post-login confirmation neither switches gateway nor resubmits");
        confirmationFailure = false;
        Portal preferWired(dualGateway, Gateway::Wired);
        Require(preferWired.Check().online && campusProbes == 4, "reuse last reachable gateway during current app session");
        int primaryStatusRequests = 0;
        Portal primaryOnly([&](Gateway gateway, Operation operation, Query const&) -> std::wstring {
            Require(gateway == Gateway::Campus && operation == Operation::Status, "reachable campus gateway does not probe alternate unnecessarily");
            ++primaryStatusRequests; return L"campusLogin({\"result\":1});";
        });
        Require(primaryOnly.Check().online && primaryStatusRequests == 1, "existing campus workflow unchanged");
        Portal invalidFirst([&](Gateway gateway, Operation operation, Query const&) -> std::wstring {
            Require(operation == Operation::Status, "unexpected response triggers only read-only fallback");
            return gateway == Gateway::Campus ? L"<html>another network</html>" : L"campusLogin({\"result\":0});";
        });
        Require(!invalidFirst.Check().online && invalidFirst.SelectedGateway() == Gateway::Wired, "unrecognized first gateway cannot receive authentication");

        int wrongGatewayPasswords = 0;
        wiredOnline = false;
        Portal statusOnlyCampus([&](Gateway gateway, Operation operation, Query const& query) -> std::wstring {
            if (gateway == Gateway::Campus) {
                if (operation == Operation::Status) return L"campusLogin({\"result\":0});";
                if (operation == Operation::Page) return L"<html>missing device context</html>";
                ++wrongGatewayPasswords; throw std::runtime_error("unexpected operation");
            }
            return dualGateway(gateway, operation, query);
        });
        Require(statusOnlyCampus.Connect(credentials) == ConnectionResult::Connected && statusOnlyCampus.SelectedGateway() == Gateway::Wired,
            "gateway answering status without current device context falls back before password submission");
        Require(wrongGatewayPasswords == 0 && wiredLogins == 4, "incomplete gateway never receives password");

        int campusConfigCalls = 0;
        wiredOnline = false;
        Portal failedCampusConfig([&](Gateway gateway, Operation operation, Query const& query) -> std::wstring {
            if (gateway == Gateway::Campus) {
                if (operation == Operation::Status) return L"campusLogin({\"result\":0});";
                if (operation == Operation::Page) return L"v4ip='10.60.70.80';";
                Require(operation == Operation::Config && query.count(L"user_password") == 0, "only read-only preparation can fail over");
                ++campusConfigCalls; throw NetworkFailure(gateway, operation, L"接收响应", 12002);
            }
            return dualGateway(gateway, operation, query);
        });
        Require(failedCampusConfig.Connect(credentials) == ConnectionResult::Connected && campusConfigCalls == 1 && wiredLogins == 5,
            "unreachable config on status-reachable gateway falls back before password submission");

        bool networkRecovered = false;
        wiredOnline = false;
        Portal recoveredDiscovery([&](Gateway gateway, Operation operation, Query const& query) -> std::wstring {
            if (!networkRecovered) throw NetworkFailure(gateway, operation, L"发送请求", 12029);
            if (gateway == Gateway::Campus) {
                if (operation == Operation::Status) return L"campusLogin({\"result\":0});";
                Require(operation == Operation::Page, "recovered discovery does not authenticate via incomplete gateway");
                return L"<html>missing context</html>";
            }
            return dualGateway(gateway, operation, query);
        });
        Reject([&] { recoveredDiscovery.Check(); }, "first discovery round has no reachable gateway");
        networkRecovered = true;
        Require(recoveredDiscovery.Connect(credentials) == ConnectionResult::Connected && wiredLogins == 6,
            "new discovery round clears stale failures so recovered alternate remains eligible");

        int diagnosticCalls = 0;
        auto diagnostic = DiagnoseNetwork([&](Gateway gateway, Operation operation, Query const& query) -> std::wstring {
            ++diagnosticCalls;
            Require(query.count(L"user_password") == 0 && query.count(L"user_account") == 0, "diagnostic never sends account or password");
            if (gateway == Gateway::Campus) {
                Require(operation == Operation::Status, "unreachable diagnostic gateway stops at status");
                throw NetworkFailure(gateway, operation, L"发送请求", 12029);
            }
            Require(operation == Operation::Status || operation == Operation::Page || operation == Operation::Config, "diagnostic never mutates network session");
            return dualGateway(gateway, operation, query);
        });
        Require(diagnosticCalls == 4 && diagnostic.find(L"172.31.0.10") != std::wstring::npos && diagnostic.find(L"12029") != std::wstring::npos, "diagnostic isolates gateway availability and reports fixed network code");
        Require(diagnostic.find(L"10.40.50.60") == std::wstring::npos && diagnostic.find(L"AABBCCDDEEFF") == std::wstring::npos
            && diagnostic.find(credentials.account) == std::wstring::npos && diagnostic.find(credentials.password) == std::wstring::npos, "diagnostic omits personal identifiers and credentials");
        auto httpError = FriendlyError(NetworkFailure(Gateway::Wired, Operation::Config, L"HTTP 状态", 0, 403));
        Require(httpError.find(L"801") != std::wstring::npos && httpError.find(L"HTTP 403") != std::wstring::npos, "HTTP failure is distinguishable from disconnected Wi-Fi");

        int startupChecks = 0;
        Portal startupPortal([&](Operation operation, Query const&) -> std::wstring {
            Require(operation == Operation::Status, "online startup must not submit authentication");
            if (++startupChecks == 1) throw std::runtime_error("network");
            return L"campusLogin({\"result\":1});";
        });
        Require(startupPortal.Connect(credentials, true) == ConnectionResult::AlreadyOnline && startupChecks == 2, "startup waits for network without repeating authentication");
        int manualChecks = 0;
        Portal unavailablePortal([&](Operation, Query const&) -> std::wstring { ++manualChecks; throw std::runtime_error("network"); });
        Reject([&] { unavailablePortal.Connect(credentials); }, "manual network failure must surface immediately");
        Require(manualChecks == 1, "manual login must not loop on network failure");

        Require(StartupCommand(L"C:\\共享 应用\\CampusLink.exe") == L"\"C:\\共享 应用\\CampusLink.exe\" --startup", "startup path quoted for spaces and Unicode");
        Reject([] { StartupCommand(L"bad\"path.exe"); }, "invalid startup command rejected");
        std::wstring startupTestKey = L"Software\\CampusLink.TestStartup." + std::to_wstring(GetCurrentProcessId());
        try {
            SetStartupEnabled(true, startupTestKey);
            Require(StartupEnabled(startupTestKey), "startup registration round-trip in isolated test key");
            HKEY testKey{};
            Require(RegOpenKeyExW(HKEY_CURRENT_USER, startupTestKey.c_str(), 0, KEY_SET_VALUE, &testKey) == ERROR_SUCCESS, "open isolated startup test key");
            DWORD marker = 42;
            auto markerResult = RegSetValueExW(testKey, L"Unrelated", 0, REG_DWORD, reinterpret_cast<BYTE const*>(&marker), sizeof(marker));
            RegCloseKey(testKey);
            Require(markerResult == ERROR_SUCCESS, "write unrelated startup test value");
            SetStartupEnabled(false, startupTestKey);
            Require(!StartupEnabled(startupTestKey), "startup registration removed");
            DWORD restoredMarker{}, markerBytes = sizeof(restoredMarker);
            Require(RegGetValueW(HKEY_CURRENT_USER, startupTestKey.c_str(), L"Unrelated", RRF_RT_REG_DWORD, nullptr, &restoredMarker, &markerBytes) == ERROR_SUCCESS && restoredMarker == marker, "disable startup preserves unrelated settings");
            RegDeleteKeyW(HKEY_CURRENT_USER, startupTestKey.c_str());
        } catch (...) { RegDeleteKeyW(HKEY_CURRENT_USER, startupTestKey.c_str()); throw; }

        std::wstring target = L"CampusLink.Test." + std::to_wstring(GetCurrentProcessId());
        try {
            WriteCredentials(credentials, target);
            auto restored = ReadCredentials(target);
            Require(restored.has_value() && restored->account == credentials.account && restored->password == credentials.password, "Windows credential vault round-trip");
            ForgetCredentials(target);
            Require(!ReadCredentials(target).has_value(), "test credential removed");
        } catch (...) { ForgetCredentials(target); throw; }
        std::cout << "PASS: encoding, JSONP, network context, authentication, online skip, logout confirmation, dual gateways, pinned mutations, private read-only diagnostics, startup network wait, isolated startup registration, Windows credential vault.\n";
        return 0;
    } catch (winrt::hresult_error const&) { std::cerr << "FAIL: unexpected Windows API error\n"; }
    catch (std::exception const& error) { std::cerr << "FAIL: " << error.what() << '\n'; }
    return 1;
}
