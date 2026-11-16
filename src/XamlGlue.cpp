#include "pch.h"
#include "App.xaml.h"
#include "MainWindow.xaml.h"

// These template definitions and the native entry point are produced in XAML
// pass 2. Compile this translation unit in the XamlGenerated iteration only.
#include "App.xaml.g.hpp"
#include "MainWindow.xaml.g.hpp"

namespace winrt::CampusLink::implementation {
template struct MainWindowT<MainWindow>;
}
