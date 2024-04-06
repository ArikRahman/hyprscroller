#include <hyprland/src/config/ConfigManager.hpp>
#include <hyprland/src/plugins/PluginAPI.hpp>

#include "dispatchers.h"
#include "scroller.h"

HANDLE PHANDLE = nullptr;
std::unique_ptr<ScrollerLayout> g_ScrollerLayout;

APICALL EXPORT std::string PLUGIN_API_VERSION() {
    return HYPRLAND_API_VERSION;
}

APICALL EXPORT PLUGIN_DESCRIPTION_INFO PLUGIN_INIT(HANDLE handle) {
    PHANDLE = handle;

    g_ScrollerLayout = std::make_unique<ScrollerLayout>();
    HyprlandAPI::addLayout(PHANDLE, "scroller", g_ScrollerLayout.get());

    dispatchers::addDispatchers();

    // one value out of: { onethird, onehalf (default), twothirds, floating, maximized }
    HyprlandAPI::addConfigValue(PHANDLE, "plugin:scroller:column_default_width", SConfigValue{.strValue = "onehalf"});

    HyprlandAPI::reloadConfig();

    return {"hyprscroller", "scrolling window layout", "dawser", "1.0"};
}

APICALL EXPORT void PLUGIN_EXIT() {}
