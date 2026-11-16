#include "pch.h"
#include "App.xaml.h"
#include "MainWindow.xaml.h"
namespace winrt::CampusLink::implementation {
App::App() { InitializeComponent(); }
void App::OnLaunched(Microsoft::UI::Xaml::LaunchActivatedEventArgs const&) {
    bool diagnostic = std::wstring(GetCommandLineW()).find(L"--demo") != std::wstring::npos || std::wstring(GetCommandLineW()).find(L"--smoke-test") != std::wstring::npos;
    static HANDLE instance = CreateMutexW(nullptr, FALSE, diagnostic ? L"Local\\CampusLink.KashiUniversity.Diagnostic" : L"Local\\CampusLink.KashiUniversity");
    if (instance && GetLastError() == ERROR_ALREADY_EXISTS) {
        if (HWND existing = FindWindowW(nullptr, L"校园网 · CampusLink")) { ShowWindow(existing, SW_RESTORE); SetForegroundWindow(existing); }
        Exit(); return;
    }
    try {
        window_ = winrt::make<MainWindow>();
        window_.Activate();
    } catch (winrt::hresult_error const& error) {
        wchar_t message[256]{};
        swprintf_s(message, L"应用暂时无法启动（错误 0x%08X）。请重新安装完整版本。", static_cast<unsigned>(error.code().value));
        MessageBoxW(nullptr, message, L"校园网", MB_OK | MB_ICONERROR);
        Exit();
    }
}
}
