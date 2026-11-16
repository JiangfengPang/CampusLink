#include "pch.h"
#include "MainWindow.xaml.h"
#include "MainWindow.g.cpp"
#include <microsoft.ui.xaml.window.h>
#include <winrt/Microsoft.UI.Windowing.h>
#include <winrt/Microsoft.UI.Xaml.Controls.h>
#include <winrt/Windows.Graphics.h>
#include <winrt/Windows.ApplicationModel.DataTransfer.h>
#include <filesystem>
#include <fstream>

using namespace winrt;
using namespace Microsoft::UI::Xaml;
using namespace Microsoft::UI::Xaml::Controls;

namespace winrt::CampusLink::implementation {
MainWindow::MainWindow() {
    InitializeComponent();
    auto command = std::wstring(GetCommandLineW());
    smoke_ = command.find(L"--smoke-test") != std::wstring::npos;
    demo_ = !smoke_ && command.find(L"--demo") != std::wstring::npos;
    startup_ = !demo_ && !smoke_ && command.find(L"--startup") != std::wstring::npos;
    reducedMotionPreview_ = (demo_ || smoke_) && command.find(L"--reduced-motion") != std::wstring::npos;
    if (demo_ || smoke_) {
        if (command.find(L"--demo-dark") != std::wstring::npos) RootPanel().RequestedTheme(ElementTheme::Dark);
        else if (command.find(L"--demo-light") != std::wstring::npos) RootPanel().RequestedTheme(ElementTheme::Light);
    }
    auto motion = [this](wchar_t const* key) { return RootPanel().Resources().Lookup(box_value(key)).as<Media::Animation::Storyboard>(); };
    entryMotion_ = motion(L"StatusEntryAnimation");
    connectingMotion_ = motion(L"StatusConnectingAnimation");
    onlineMotion_ = motion(L"StatusOnlineAnimation");
    errorMotion_ = motion(L"StatusErrorAnimation");
    auto native = this->try_as<::IWindowNative>();
    check_hresult(native->get_WindowHandle(&hwnd_));
    auto weakWindow = get_weak();
    RootPanel().ActualThemeChanged([weakWindow](auto&&, auto&&) {
        if (auto self = weakWindow.get(); self && !self->closed_) self->UpdateTitleBarTheme();
    });
    try {
        titleBarSettings_ = Microsoft::UI::System::ThemeSettings::CreateForWindowId(AppWindow().Id());
        auto queue = DispatcherQueue();
        titleBarSettingsChanged_ = titleBarSettings_.Changed([weakWindow, queue](auto&&, auto&&) {
            queue.TryEnqueue([weakWindow] {
                if (auto self = weakWindow.get(); self && !self->closed_) self->UpdateTitleBarTheme();
            });
        });
        titleBarSettingsSubscribed_ = true;
    } catch (...) { /* Keep native system styling if the theme notification API is unavailable. */ }
    UpdateTitleBarTheme();
    UINT dpi = GetDpiForWindow(hwnd_);
    bool narrowPreview = demo_ && command.find(L"--demo-narrow") != std::wstring::npos;
    int width = MulDiv(narrowPreview ? 560 : 1080, dpi, 96), height = MulDiv(740, dpi, 96);
    MONITORINFO monitor{sizeof(MONITORINFO)};
    GetMonitorInfoW(MonitorFromWindow(hwnd_, MONITOR_DEFAULTTONEAREST), &monitor);
    width = std::min(width, static_cast<int>(monitor.rcWork.right - monitor.rcWork.left));
    height = std::min(height, static_cast<int>(monitor.rcWork.bottom - monitor.rcWork.top));
    SetWindowPos(hwnd_, nullptr, monitor.rcWork.left + (monitor.rcWork.right - monitor.rcWork.left - width) / 2,
        monitor.rcWork.top + (monitor.rcWork.bottom - monitor.rcWork.top - height) / 2, width, height, SWP_NOZORDER | SWP_NOACTIVATE);
    auto presenter = AppWindow().Presenter().try_as<Microsoft::UI::Windowing::OverlappedPresenter>();
    if (presenter) {
        presenter.IsMaximizable(true);
        presenter.IsResizable(true);
        auto minimumWidth = std::min(MulDiv(500, dpi, 96), static_cast<int>(monitor.rcWork.right - monitor.rcWork.left));
        auto minimumHeight = std::min(MulDiv(520, dpi, 96), static_cast<int>(monitor.rcWork.bottom - monitor.rcWork.top));
        presenter.PreferredMinimumWidth(box_value(minimumWidth).as<Windows::Foundation::IReference<int32_t>>());
        presenter.PreferredMinimumHeight(box_value(minimumHeight).as<Windows::Foundation::IReference<int32_t>>());
    }
    auto icon = static_cast<HICON>(LoadImageW(GetModuleHandleW(nullptr), MAKEINTRESOURCEW(101), IMAGE_ICON, 0, 0, LR_DEFAULTSIZE | LR_SHARED));
    if (icon) { SendMessageW(hwnd_, WM_SETICON, ICON_BIG, reinterpret_cast<LPARAM>(icon)); SendMessageW(hwnd_, WM_SETICON, ICON_SMALL, reinterpret_cast<LPARAM>(icon)); }
    try { SystemBackdrop(Media::MicaBackdrop()); } catch (...) {}
    preferences_ = demo_ || smoke_ ? campus::Preferences{false, false} : campus::ReadPreferences();
    AutoLoginSwitch().IsOn(preferences_.autoLogin);
    AutoCloseSwitch().IsOn(preferences_.autoClose);
    if (!demo_ && !smoke_) {
        try {
            auto credentials = campus::ReadCredentials();
            if (credentials) {
                AccountBox().Text(credentials->account);
                PasswordInput().Password(credentials->password);
                saved_ = true;
            }
        } catch (std::exception const& error) { ShowNotice(campus::FriendlyError(error), InfoBarSeverity::Warning); }
    }
    ForgetButton().IsEnabled(saved_);
    RefreshStartupUi();
    Closed([this](auto&&, auto&&) {
        closed_ = true;
        if (closeTimer_) closeTimer_.Stop();
        if (demoTimer_) demoTimer_.Stop();
        StopStatusMotion();
        if (motionSubscribed_) animationSettings_.AnimationsEnabledChanged(motionChanged_);
        if (titleBarSettingsSubscribed_) titleBarSettings_.Changed(titleBarSettingsChanged_);
    });
    AccountBox().GettingFocus([this](auto&&, auto&&) { if (closeTimer_) closeTimer_.Stop(); });
    PasswordInput().GettingFocus([this](auto&&, auto&&) { if (closeTimer_) closeTimer_.Stop(); });
    RootPanel().PointerPressed([this](auto&&, auto&&) { if (closeTimer_) closeTimer_.Stop(); });
    RootPanel().PreviewKeyDown([this](auto&&, Input::KeyRoutedEventArgs const& args) {
        if (closeTimer_) closeTimer_.Stop();
        // Preview theme changes without touching Windows personalization or saved preferences.
        if (demo_ && args.Key() == Windows::System::VirtualKey::F6) {
            RootPanel().RequestedTheme(RootPanel().ActualTheme() == ElementTheme::Dark ? ElementTheme::Light : ElementTheme::Dark);
            args.Handled(true);
        }
    });
    try {
        auto weak = get_weak();
        auto queue = DispatcherQueue();
        motionChanged_ = animationSettings_.AnimationsEnabledChanged([weak, queue](auto&&, auto&&) {
            queue.TryEnqueue([weak] {
                if (auto current = weak.get(); current && !current->closed_) current->PlayStatusMotion(false);
            });
        });
        motionSubscribed_ = true;
    } catch (...) { /* Older supported Windows builds still check the setting on each state change. */ }
    Navigation().SelectedItem(ConnectionNavItem());
    UpdateContentInsets();
    initialized_ = true;
}
void MainWindow::UpdateTitleBarTheme() noexcept {
    if (!hwnd_ || closed_) return;
    try {
        using Microsoft::UI::Windowing::TitleBarTheme;
        auto theme = titleBarSettings_ && titleBarSettings_.HighContrast() ? TitleBarTheme::Legacy
            : RootPanel().ActualTheme() == ElementTheme::Dark ? TitleBarTheme::Dark : TitleBarTheme::Light;
        AppWindow().TitleBar().PreferredTheme(theme);
    } catch (...) {
        try { AppWindow().TitleBar().PreferredTheme(Microsoft::UI::Windowing::TitleBarTheme::UseDefaultAppMode); }
        catch (...) { /* Older Windows builds retain their native title bar. */ }
    }
}
void MainWindow::UpdateContentInsets() {
    // In minimal mode the native hamburger sits over the content's top row.
    if (ContentPanel()) ContentPanel().Padding(Thickness{28, Navigation().DisplayMode() == NavigationViewDisplayMode::Minimal ? 64.0 : 24.0, 28, 28});
}
void MainWindow::OnNavigationDisplayModeChanged(NavigationView const&, NavigationViewDisplayModeChangedEventArgs const&) {
    UpdateContentInsets();
}
void MainWindow::OnNavigationChanged(NavigationView const&, NavigationViewSelectionChangedEventArgs const& args) {
    if (!args.SelectedItem()) return;
    bool preferences = args.SelectedItem() == PreferencesNavItem();
    bool disclaimer = args.SelectedItem() == DisclaimerNavItem();
    ConnectionPage().Visibility(preferences || disclaimer ? Visibility::Collapsed : Visibility::Visible);
    PreferencesPage().Visibility(preferences ? Visibility::Visible : Visibility::Collapsed);
    DisclaimerPage().Visibility(disclaimer ? Visibility::Visible : Visibility::Collapsed);
    PageHeading().Text(disclaimer ? L"免责声明" : preferences ? L"偏好设置" : L"校园网连接");
    PageDescription().Text(disclaimer ? L"使用前了解适用范围、数据处理与责任边界。" : preferences ? L"按你的习惯连接校园网。" : L"轻松连接校园网，开始每一天。");
    if (closeTimer_) closeTimer_.Stop();
    if (initialized_ && Navigation().DisplayMode() != NavigationViewDisplayMode::Expanded) Navigation().IsPaneOpen(false);
}
void MainWindow::OnConnectionLayoutChanged(Windows::Foundation::IInspectable const&, SizeChangedEventArgs const& args) {
    bool wide = args.NewSize().Width >= 700;
    if (wide == wideLayout_) return;
    wideLayout_ = wide;
    StatusColumn().Width(GridLength{wide ? 2.0 : 1.0, GridUnitType::Star});
    AccountColumn().Width(GridLength{wide ? 3.0 : 0.0, GridUnitType::Star});
    Grid::SetColumn(AccountCard(), wide ? 1 : 0);
    Grid::SetRow(AccountCard(), wide ? 0 : 1);
}
void MainWindow::OnLoaded(Windows::Foundation::IInspectable const&, RoutedEventArgs const&) {
    if (loaded_) return;
    loaded_ = true;
    UpdateTitleBarTheme();
    if (demo_) {
        auto command = std::wstring(GetCommandLineW());
        if (command.find(L"--demo-animations") != std::wstring::npos) {
            AdvanceAnimationDemo();
            demoTimer_ = DispatcherQueue().CreateTimer();
            demoTimer_.Interval(std::chrono::milliseconds(2600));
            auto weak = get_weak();
            demoTimer_.Tick([weak](auto&&, auto&&) { if (auto self = weak.get(); self && !self->closed_) self->AdvanceAnimationDemo(); });
            demoTimer_.Start();
            return;
        }
        if (command.find(L"--demo-connecting") != std::wstring::npos) {
            SetBusy(true);
            SetStatus(L"正在认证", L"正在联系校园网认证服务器…", StatusState::Connecting);
        }
        else if (command.find(L"--demo-online") != std::wstring::npos) SetStatus(L"已连接校园网", L"认证已通过，可以开始上网了。", StatusState::Online);
        else if (command.find(L"--demo-error") != std::wstring::npos) {
            SetStatus(L"暂时无法连接", L"请确认已连接学校的 Wi-Fi 或网线。", StatusState::Error);
            ShowNotice(L"无法连接学校认证服务器。连接校园网后，点击一键登录重试。", InfoBarSeverity::Error);
        } else SetStatus(L"等待认证", L"填入校园网账号和密码，即可一键连接。", StatusState::Waiting);
        return;
    }
    Run(saved_ && (preferences_.autoLogin || startup_));
}
campus::Credentials MainWindow::InputCredentials() {
    std::wstring account(AccountBox().Text());
    auto first = account.find_first_not_of(L" \t\r\n");
    account = first == std::wstring::npos ? L"" : account.substr(first, account.find_last_not_of(L" \t\r\n") - first + 1);
    return { std::move(account), std::wstring(PasswordInput().Password()) };
}
void MainWindow::SetBusy(bool value) {
    busy_ = value;
    ConnectButton().IsEnabled(!value); SaveButton().IsEnabled(!value);
    ForgetButton().IsEnabled(!value && saved_);
    AccountBox().IsEnabled(!value); PasswordInput().IsEnabled(!value); RememberBox().IsEnabled(!value);
    ConnectButton().Content(box_value(value ? L"正在连接…" : L"一键登录"));
    RefreshStatusButton().IsEnabled(!value);
    DiagnoseButton().IsEnabled(!value);
    LogoutButton().IsEnabled(!value && statusState_ == StatusState::Online);
    StartupSwitch().IsEnabled(!value);
}
void MainWindow::SetStatus(std::wstring const& title, std::wstring const& description, StatusState state) {
    statusState_ = state;
    StatusTitle().Text(title);
    StatusDescription().Text(description);
    StatusIcon().Visibility(state == StatusState::Waiting ? Visibility::Visible : Visibility::Collapsed);
    OnlineSymbol().Visibility(state == StatusState::Online ? Visibility::Visible : Visibility::Collapsed);
    OnlineRipple().Visibility(state == StatusState::Online ? Visibility::Visible : Visibility::Collapsed);
    ErrorSymbol().Visibility(state == StatusState::Error ? Visibility::Visible : Visibility::Collapsed);
    LogoutButton().IsEnabled(!busy_ && state == StatusState::Online);
    PlayStatusMotion();
}
bool MainWindow::MotionEnabled() const {
    if (reducedMotionPreview_) return false;
    try { return animationSettings_.AnimationsEnabled(); } catch (...) { return false; }
}
void MainWindow::StopStatusMotion() {
    for (auto const& motion : { entryMotion_, connectingMotion_, onlineMotion_, errorMotion_ }) {
        if (motion) motion.Stop();
    }
    BusyRing().IsActive(false);
}
void MainWindow::PlayStatusMotion(bool transition) {
    StopStatusMotion();
    bool enabled = MotionEnabled();
    bool connecting = statusState_ == StatusState::Connecting;
    BusyHalo().Visibility(connecting ? Visibility::Visible : Visibility::Collapsed);
    BusyHalo().Opacity(connecting ? 0.08 : 0);
    BusyRing().Visibility(connecting && enabled ? Visibility::Visible : Visibility::Collapsed);
    BusyRing().IsActive(connecting && enabled);
    BusyStillIcon().Visibility(connecting && !enabled ? Visibility::Visible : Visibility::Collapsed);
    if (!enabled) return;
    try {
        if (connecting) connectingMotion_.Begin();
        if (transition) {
            entryMotion_.Begin();
            if (statusState_ == StatusState::Online) onlineMotion_.Begin();
            if (statusState_ == StatusState::Error) errorMotion_.Begin();
        }
    } catch (...) {
        // Keep the state readable if animation initialization fails.
        motionFault_ = true;
        StopStatusMotion();
        BusyStillIcon().Visibility(connecting ? Visibility::Visible : Visibility::Collapsed);
    }
}
void MainWindow::AdvanceAnimationDemo() {
    unsigned step = demoStep_++ % 5;
    SetBusy(step == 0 || step == 3);
    if (step == 0) SetStatus(L"正在检查连接", L"正在联系校园网认证服务器…", StatusState::Connecting);
    else if (step == 1) SetStatus(L"已连接校园网", L"当前已经在线，无需重复认证。", StatusState::Online);
    else if (step == 2) SetStatus(L"等待认证", L"填入校园网账号和密码，即可一键连接。", StatusState::Waiting);
    else if (step == 3) SetStatus(L"正在认证", L"正在提交登录请求，请稍候…", StatusState::Connecting);
    else SetStatus(L"暂时未能连接", L"请确认已连接学校的 Wi-Fi 或网线。", StatusState::Error);
}
void MainWindow::ShowNotice(std::wstring const& message, InfoBarSeverity severity) {
    Notice().Message(message); Notice().Severity(severity); Notice().IsOpen(true);
}
void MainWindow::ScheduleClose() {
    if (!preferences_.autoClose || !saved_ || smoke_ || demo_) return;
    StatusDescription().Text(L"已确认在线，窗口将在 4 秒后自动关闭。\n操作窗口可取消自动关闭。");
    closeTimer_ = DispatcherQueue().CreateTimer();
    closeTimer_.Interval(std::chrono::seconds(4)); closeTimer_.IsRepeating(false);
    auto weak = get_weak();
    closeTimer_.Tick([weak](auto&&, auto&&) { if (auto self = weak.get()) { if (!self->closed_) self->Close(); } });
    closeTimer_.Start();
}
fire_and_forget MainWindow::Run(bool login) {
    auto lifetime = get_strong();
    if (busy_) co_return;
    if (closeTimer_) closeTimer_.Stop();
    Notice().IsOpen(false);
    auto credentials = InputCredentials();
    if (login && (credentials.account.empty() || credentials.password.empty())) {
        ShowNotice(L"请完整填写校园网账号和密码。", InfoBarSeverity::Warning);
        if (credentials.account.empty()) AccountBox().Focus(FocusState::Programmatic); else PasswordInput().Focus(FocusState::Programmatic);
        co_return;
    }
    bool remember = RememberBox().IsChecked().Value();
    bool waitForNetwork = startup_ && login;
    startup_ = false;
    SetBusy(true);
    SetStatus(waitForNetwork ? L"等待校园网就绪" : login ? L"正在认证" : L"正在检查连接", waitForNetwork ? L"正在准备开机连接，请确认已连接校园 Wi-Fi 或网线…" : L"正在联系校园网认证服务器…", StatusState::Connecting);
    apartment_context ui;
    std::wstring failure;
    bool online = false, connected = false, credentialSaved = false, credentialForgotten = false;
    std::wstring storageWarning;
    std::optional<campus::Gateway> selectedGateway;
    auto preferred = preferredGateway_;
    co_await resume_background();
    auto apartment = RoInitialize(RO_INIT_MULTITHREADED);
    campus::Portal portal(preferred);
    try {
        check_hresult(apartment);
        if (login) {
            auto result = portal.Connect(credentials, waitForNetwork);
            online = true; connected = result == campus::ConnectionResult::Connected;
            if (connected) {
                try {
                    if (remember) { campus::WriteCredentials(credentials); credentialSaved = true; }
                    else { campus::ForgetCredentials(); credentialForgotten = true; campus::SetStartupEnabled(false); }
                } catch (std::exception const& error) { storageWarning = campus::FriendlyError(error); }
            }
        } else online = portal.Check().online;
    } catch (hresult_error const& error) {
        failure = error.code() == E_ACCESSDENIED ? std::wstring(error.message()) : L"认证服务器返回的数据暂时无法识别，请重试或打开原登录页。";
    } catch (std::exception const& error) { failure = campus::FriendlyError(error); }
    selectedGateway = portal.SelectedGateway();
    if (SUCCEEDED(apartment)) RoUninitialize();
    try { co_await ui; } catch (...) { co_return; }
    if (closed_) co_return;
    SetBusy(false);
    if (selectedGateway) {
        preferredGateway_ = *selectedGateway;
        SchoolLoginLink().NavigateUri(Windows::Foundation::Uri(campus::GatewayUrl(*selectedGateway)));
    }
    if (!failure.empty()) {
        SetStatus(L"暂时未能连接", L"请查看下方提示，调整后再试。", StatusState::Error);
        ShowNotice(failure, InfoBarSeverity::Error);
    } else if (online) {
        if (credentialSaved) saved_ = true;
        if (credentialForgotten) saved_ = false;
        ForgetButton().IsEnabled(saved_);
        RefreshStartupUi();
        SetStatus(connected ? L"登录成功" : L"已连接校园网", connected ? L"学校认证网关已确认在线。" : L"当前已经在线，无需重复认证。", StatusState::Online);
        if (!storageWarning.empty()) ShowNotice(L"网络已连接，但本机账号操作未完成。" + storageWarning, InfoBarSeverity::Warning);
        else if (login) ScheduleClose();
    } else SetStatus(L"等待认证", L"填入校园网账号和密码，即可一键连接。", StatusState::Waiting);
    if (smoke_) {
        try {
            wchar_t temporary[MAX_PATH]{}; GetTempPathW(MAX_PATH, temporary);
            std::ofstream output(std::filesystem::path(temporary) / L"CampusLink-smoke.json", std::ios::binary);
            output << "{\"uiStarted\":true,\"online\":" << (online ? "true" : "false")
                << ",\"checkCompleted\":" << (failure.empty() ? "true" : "false")
                << ",\"animationsEnabled\":" << (MotionEnabled() ? "true" : "false")
                << ",\"animationError\":" << (motionFault_ ? "true" : "false") << "}";
        } catch (...) {}
        Close();
    }
}
void MainWindow::OnConnect(Windows::Foundation::IInspectable const&, RoutedEventArgs const&) {
    if (demo_) { ShowNotice(L"界面预览模式，不会提交账号密码。", InfoBarSeverity::Informational); return; }
    Run(true);
}
fire_and_forget MainWindow::OnDiagnose(Windows::Foundation::IInspectable const&, RoutedEventArgs const&) {
    auto lifetime = get_strong();
    if (busy_ || smoke_) co_return;
    if (closeTimer_) closeTimer_.Stop();
    auto previousTitle = std::wstring(StatusTitle().Text());
    auto previousDescription = std::wstring(StatusDescription().Text());
    auto previousState = statusState_;
    bool preview = demo_;
    SetBusy(true);
    SetStatus(L"正在诊断网络", L"正在检查学校的两个已知入口，不会提交账号密码。", StatusState::Connecting);
    apartment_context ui;
    std::wstring report;
    co_await resume_background();
    auto apartment = RoInitialize(RO_INIT_MULTITHREADED);
    try {
        check_hresult(apartment);
        if (preview) {
            report = L"界面演示：以下为模拟结果。\n\n" + campus::DiagnoseNetwork([](campus::Gateway gateway, campus::Operation operation, campus::Query const&) -> std::wstring {
                if (gateway == campus::Gateway::Campus) throw campus::NetworkFailure(gateway, operation, L"发送请求", 12029);
                if (operation == campus::Operation::Status) return L"campusLogin({\"result\":0});";
                if (operation == campus::Operation::Page) return L"v4ip='10.20.30.40';";
                if (operation == campus::Operation::Config) return L"campusLogin({\"code\":1,\"data\":{\"login_method\":1}});";
                throw std::runtime_error("protocol");
            });
        } else report = campus::DiagnoseNetwork();
    }
    catch (...) { report = L"网络诊断未能完成，请使用学校登录页。"; }
    if (SUCCEEDED(apartment)) RoUninitialize();
    try { co_await ui; } catch (...) { co_return; }
    if (closed_) co_return;
    SetBusy(false);
    SetStatus(previousTitle, previousDescription, previousState);
    try {
        TextBox contents;
        contents.AcceptsReturn(true); contents.IsReadOnly(true);
        contents.TextWrapping(TextWrapping::Wrap); contents.MinHeight(240); contents.MaxHeight(430);
        contents.Text(report);
        ContentDialog dialog;
        dialog.XamlRoot(RootPanel().XamlRoot()); dialog.Title(box_value(L"网络诊断"));
        dialog.Content(contents); dialog.PrimaryButtonText(L"复制结果"); dialog.CloseButtonText(L"关闭");
        auto result = co_await dialog.ShowAsync();
        if (result == ContentDialogResult::Primary && !closed_) {
            Windows::ApplicationModel::DataTransfer::DataPackage data;
            data.SetText(report);
            Windows::ApplicationModel::DataTransfer::Clipboard::SetContent(data);
            ShowNotice(L"诊断结果已复制，可以粘贴给开发者排查。", InfoBarSeverity::Success);
        }
    } catch (...) { if (!closed_) ShowNotice(L"无法显示或复制诊断结果，请稍后重试。", InfoBarSeverity::Warning); }
}
void MainWindow::OnSave(Windows::Foundation::IInspectable const&, RoutedEventArgs const&) {
    if (closeTimer_) closeTimer_.Stop();
    if (demo_ || smoke_) return;
    auto credentials = InputCredentials();
    if (credentials.account.empty() || credentials.password.empty()) { ShowNotice(L"请先填写完整的账号和密码。", InfoBarSeverity::Warning); return; }
    try {
        campus::WriteCredentials(credentials); saved_ = true; RememberBox().IsChecked(true); ForgetButton().IsEnabled(true);
        RefreshStartupUi();
        ShowNotice(preferences_.autoLogin ? L"账号已保存在这台电脑，下次打开应用即可自动认证。" : L"账号已保存在这台电脑，点击一键登录即可使用。", InfoBarSeverity::Success);
    } catch (std::exception const& error) { ShowNotice(campus::FriendlyError(error), InfoBarSeverity::Error); }
}
void MainWindow::OnForget(Windows::Foundation::IInspectable const&, RoutedEventArgs const&) {
    if (closeTimer_) closeTimer_.Stop();
    if (demo_ || smoke_) return;
    try {
        campus::SetStartupEnabled(false);
        campus::ForgetCredentials(); saved_ = false;
        AccountBox().Text(L""); PasswordInput().Password(L""); ForgetButton().IsEnabled(false);
        RefreshStartupUi();
        ShowNotice(L"已清除这台电脑保存的账号，并关闭开机连接；不会注销当前网络连接。", InfoBarSeverity::Success);
    } catch (std::exception const& error) { ShowNotice(campus::FriendlyError(error), InfoBarSeverity::Error); }
}
void MainWindow::OnPreferencesChanged(Windows::Foundation::IInspectable const&, RoutedEventArgs const&) {
    if (!initialized_ || demo_ || smoke_) return;
    if (closeTimer_) closeTimer_.Stop();
    preferences_ = { AutoLoginSwitch().IsOn(), AutoCloseSwitch().IsOn() };
    try { campus::WritePreferences(preferences_); }
    catch (std::exception const& error) { ShowNotice(campus::FriendlyError(error), InfoBarSeverity::Warning); }
}
void MainWindow::OnInputKeyDown(Windows::Foundation::IInspectable const&, Input::KeyRoutedEventArgs const& event) {
    if (event.Key() == Windows::System::VirtualKey::Enter && !busy_) { event.Handled(true); OnConnect(nullptr, nullptr); }
}
void MainWindow::RefreshStartupUi() {
    startupUpdating_ = true;
    try {
        StartupSwitch().IsOn(!demo_ && !smoke_ && campus::StartupEnabled());
        StartupHelp().Text(saved_ ? L"登录 Windows 后使用已保存的账号自动连接。移动便携版文件夹后，请重新开启此项。" : L"先在校园网连接页保存账号，再开启开机连接。此功能默认关闭。" );
    } catch (std::exception const& error) { ShowNotice(campus::FriendlyError(error), InfoBarSeverity::Warning); }
    startupUpdating_ = false;
}
void MainWindow::OnStartupChanged(Windows::Foundation::IInspectable const&, RoutedEventArgs const&) {
    if (!initialized_ || startupUpdating_ || demo_ || smoke_) return;
    if (closeTimer_) closeTimer_.Stop();
    if (StartupSwitch().IsOn() && !saved_) {
        RefreshStartupUi();
        ShowNotice(L"请先在校园网连接页保存账号密码，再开启开机自动连接。", InfoBarSeverity::Informational);
        return;
    }
    try { campus::SetStartupEnabled(StartupSwitch().IsOn()); }
    catch (std::exception const& error) {
        auto message = campus::FriendlyError(error);
        RefreshStartupUi();
        ShowNotice(message, InfoBarSeverity::Warning);
    }
}
void MainWindow::OnRefresh(Windows::Foundation::IInspectable const&, RoutedEventArgs const&) {
    if (demo_) { ShowNotice(L"界面预览模式，不会发送真实请求。", InfoBarSeverity::Informational); return; }
    Run(false);
}
fire_and_forget MainWindow::OnLogout(Windows::Foundation::IInspectable const&, RoutedEventArgs const&) {
    auto lifetime = get_strong();
    if (busy_) co_return;
    if (closeTimer_) closeTimer_.Stop();
    campus::LogLogoutStage("begin", true);
    ContentDialog dialog;
    dialog.XamlRoot(RootPanel().XamlRoot());
    dialog.RequestedTheme(RootPanel().ActualTheme());
    dialog.Title(box_value(L"注销校园网？"));
    dialog.Content(box_value(L"将断开本机的校园网连接。按学校设置，注销时可能同时解除本机的自动认证绑定。已保存的账号密码会保留，本次不会自动重新连接。"));
    dialog.PrimaryButtonText(L"注销并下线");
    dialog.CloseButtonText(L"取消");
    dialog.DefaultButton(ContentDialogButton::Close);
    ContentDialogResult result;
    try { campus::LogLogoutStage("confirmation.open"); result = co_await dialog.ShowAsync(); }
    catch (...) { campus::LogLogoutStage("failure.confirmation"); if (!closed_) ShowNotice(L"无法打开注销确认窗口，请重试。", InfoBarSeverity::Warning); co_return; }
    if (closed_ || result != ContentDialogResult::Primary) { campus::LogLogoutStage("cancelled"); co_return; }
    campus::LogLogoutStage("confirmed");
    if (demo_ || smoke_) { ShowNotice(L"界面预览模式，不会注销真实网络。", InfoBarSeverity::Informational); co_return; }
    RunLogout();
}
fire_and_forget MainWindow::RunLogout() {
    auto lifetime = get_strong();
    if (busy_) co_return;
    startup_ = false;
    SetBusy(true);
    Notice().IsOpen(false);
    SetStatus(L"正在注销", L"正在等待学校网关确认离线…", StatusState::Connecting);
    apartment_context ui;
    std::wstring failure;
    campus::DisconnectionResult result = campus::DisconnectionResult::AlreadyOffline;
    auto preferred = preferredGateway_;
    co_await resume_background();
    auto apartment = RoInitialize(RO_INIT_MULTITHREADED);
    try {
        check_hresult(apartment);
        campus::Portal portal([](campus::Gateway gateway, campus::Operation operation, campus::Query const& query) {
            std::string name;
            switch (operation) {
            case campus::Operation::Status: name = "status"; break;
            case campus::Operation::Page: name = "page"; break;
            case campus::Operation::Config: name = "config"; break;
            case campus::Operation::MacFind: name = "mac-find"; break;
            case campus::Operation::MacUnbind: name = "mac-unbind"; break;
            case campus::Operation::Logout: name = "portal-logout"; break;
            case campus::Operation::LegacyLogout: name = "local-logout"; break;
            default: throw std::runtime_error("protocol");
            }
            campus::LogLogoutStage("request." + name);
            auto body = campus::HttpRequestAt(gateway, operation, query);
            if (operation == campus::Operation::MacUnbind || operation == campus::Operation::Logout || operation == campus::Operation::LegacyLogout) {
                try {
                    auto reply = campus::ParseJsonp(body);
                    auto code = campus::JsonText(reply, L"result");
                    campus::LogLogoutStage("reply." + name + ((code == L"1" || code == L"ok") ? ".accepted" : ".rejected"));
                } catch (...) { campus::LogLogoutStage("reply." + name + ".invalid"); }
            }
            return body;
        }, preferred);
        result = portal.Disconnect();
        campus::LogLogoutStage(result == campus::DisconnectionResult::Disconnected ? "offline.confirmed" : "offline.already");
    } catch (std::exception const& error) {
        failure = campus::FriendlyError(error);
        std::string code = error.what();
        bool known = code == "network" || code == "response" || code == "protocol" || code == "config" || code == "ip"
            || code == "logout-account" || code == "logout-device" || code == "logout-rejected" || code == "logout-unconfirmed";
        campus::LogLogoutStage(known ? "failure." + code : "failure.unknown");
    }
    catch (...) { campus::LogLogoutStage("failure.windows"); failure = L"暂时无法确认注销结果，请刷新状态或打开学校登录页。"; }
    if (SUCCEEDED(apartment)) RoUninitialize();
    try { co_await ui; } catch (...) { co_return; }
    if (closed_) co_return;
    SetBusy(false);
    if (!failure.empty()) {
        SetStatus(L"注销尚未确认", L"请刷新状态，或使用学校登录页处理。", StatusState::Error);
        ShowNotice(failure, InfoBarSeverity::Warning);
    } else {
        SetStatus(result == campus::DisconnectionResult::Disconnected ? L"已注销校园网" : L"当前未登录", L"本机已离线，需要时可再次点击一键登录。", StatusState::Waiting);
    }
}
}
