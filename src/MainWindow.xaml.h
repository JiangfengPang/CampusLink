#pragma once
#include "MainWindow.g.h"
#include "Store.hpp"
#include <winrt/Microsoft.UI.Dispatching.h>
#include <winrt/Microsoft.UI.System.h>
#include <winrt/Microsoft.UI.Xaml.Controls.h>
#include <winrt/Microsoft.UI.Xaml.Input.h>
#include <winrt/Microsoft.UI.Xaml.Media.h>
#include <winrt/Microsoft.UI.Xaml.Media.Animation.h>
#include <winrt/Windows.System.h>
#include <winrt/Windows.UI.ViewManagement.h>
namespace winrt::CampusLink::implementation {
struct MainWindow : MainWindowT<MainWindow> {
    MainWindow();
    void OnLoaded(Windows::Foundation::IInspectable const&, Microsoft::UI::Xaml::RoutedEventArgs const&);
    void OnConnect(Windows::Foundation::IInspectable const&, Microsoft::UI::Xaml::RoutedEventArgs const&);
    void OnSave(Windows::Foundation::IInspectable const&, Microsoft::UI::Xaml::RoutedEventArgs const&);
    void OnForget(Windows::Foundation::IInspectable const&, Microsoft::UI::Xaml::RoutedEventArgs const&);
    void OnPreferencesChanged(Windows::Foundation::IInspectable const&, Microsoft::UI::Xaml::RoutedEventArgs const&);
    void OnInputKeyDown(Windows::Foundation::IInspectable const&, Microsoft::UI::Xaml::Input::KeyRoutedEventArgs const&);
    void OnNavigationChanged(Microsoft::UI::Xaml::Controls::NavigationView const&, Microsoft::UI::Xaml::Controls::NavigationViewSelectionChangedEventArgs const&);
    void OnNavigationDisplayModeChanged(Microsoft::UI::Xaml::Controls::NavigationView const&, Microsoft::UI::Xaml::Controls::NavigationViewDisplayModeChangedEventArgs const&);
    void OnConnectionLayoutChanged(Windows::Foundation::IInspectable const&, Microsoft::UI::Xaml::SizeChangedEventArgs const&);
    void OnStartupChanged(Windows::Foundation::IInspectable const&, Microsoft::UI::Xaml::RoutedEventArgs const&);
    void OnRefresh(Windows::Foundation::IInspectable const&, Microsoft::UI::Xaml::RoutedEventArgs const&);
    winrt::fire_and_forget OnLogout(Windows::Foundation::IInspectable const&, Microsoft::UI::Xaml::RoutedEventArgs const&);
    winrt::fire_and_forget OnDiagnose(Windows::Foundation::IInspectable const&, Microsoft::UI::Xaml::RoutedEventArgs const&);
private:
    enum class StatusState { Waiting, Connecting, Online, Error };
    winrt::fire_and_forget Run(bool login);
    winrt::fire_and_forget RunLogout();
    void RefreshStartupUi();
    void UpdateContentInsets();
    void UpdateTitleBarTheme() noexcept;
    void SetBusy(bool value);
    void SetStatus(std::wstring const& title, std::wstring const& description, StatusState state);
    void PlayStatusMotion(bool transition = true);
    void StopStatusMotion();
    bool MotionEnabled() const;
    void AdvanceAnimationDemo();
    void ShowNotice(std::wstring const& message, Microsoft::UI::Xaml::Controls::InfoBarSeverity severity);
    void ScheduleClose();
    campus::Credentials InputCredentials();
    campus::Preferences preferences_;
    bool initialized_{false}, busy_{false}, closed_{false}, saved_{false}, loaded_{false}, demo_{false}, smoke_{false};
    HWND hwnd_{};
    Microsoft::UI::Dispatching::DispatcherQueueTimer closeTimer_{nullptr};
    Microsoft::UI::Dispatching::DispatcherQueueTimer demoTimer_{nullptr};
    Microsoft::UI::Xaml::Media::Animation::Storyboard entryMotion_{nullptr}, connectingMotion_{nullptr}, onlineMotion_{nullptr}, errorMotion_{nullptr};
    Windows::UI::ViewManagement::UISettings animationSettings_;
    winrt::event_token motionChanged_{};
    bool motionSubscribed_{false}, motionFault_{false}, reducedMotionPreview_{false};
    StatusState statusState_{StatusState::Connecting};
    unsigned demoStep_{};
    bool wideLayout_{false};
    bool startup_{false}, startupUpdating_{false};
    campus::Gateway preferredGateway_{campus::Gateway::Campus};
    Microsoft::UI::System::ThemeSettings titleBarSettings_{nullptr};
    winrt::event_token titleBarSettingsChanged_{};
    bool titleBarSettingsSubscribed_{false};
};
}
namespace winrt::CampusLink::factory_implementation {
struct MainWindow : MainWindowT<MainWindow, implementation::MainWindow> {};
}
