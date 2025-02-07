#include <hyprland/src/desktop/Window.hpp>
#include <hyprland/src/config/ConfigManager.hpp>
#include <hyprland/src/config/ConfigValue.hpp>
#include <hyprland/src/debug/Log.hpp>
#include <hyprutils/math/Vector2D.hpp>
#include <hyprland/src/desktop/Workspace.hpp>
#include <hyprland/src/Compositor.hpp>
#include <hyprland/src/managers/KeybindManager.hpp>
#include <sstream>
#include <string>
#ifdef COLORS_IPC
#include <hyprland/src/managers/EventManager.hpp>
#endif

#include "dispatchers.h"
#include "scroller.h"
#include <unordered_map>
#include <vector>

extern HANDLE PHANDLE;

struct Box {
    Box() : x(0), y(0), w(0), h(0) {}
    Box(double x_, double y_, double w_, double h_)
        : x(x_), y(y_), w(w_), h(h_) {}
    Box(Vector2D pos, Vector2D size)
        : x(pos.x), y(pos.y), w(size.x), h(size.y) {}
    Box(const Box &box)
        : x(box.x), y(box.y), w(box.w), h(box.h) {}

    void set_size(double w_, double h_) {
        w = w_;
        h = h_;
    }
    void set_pos(double x_, double y_) {
        x = x_;
        y = y_;
    }
    bool operator == (const Box &box) {
        if (box.x == x && box.y == y && box.w == w && box.h == h)
            return true;
        return false;
    }

    double x, y, w, h;
};

enum class StandardSize {
    OneSixth = 0,
    OneFourth,
    OneThird,
    OneHalf,
    TwoThirds,
    ThreeQuarters,
    FiveSixths,
    One,
    Free
};

enum class Reorder {
    Auto,
    Lazy
};


class CycleSizes {
public:
    CycleSizes() {}
    ~CycleSizes() { reset(); }
    StandardSize get_default() { return sizes[0]; }
    StandardSize get_next(StandardSize size, int step) {
        int current = -1;
        for (size_t i = 0; i < sizes.size(); ++i) {
            if (sizes[i] == size) {
                current = i;
                break;
            }
        }
        if (current == -1) {
            return sizes[0];
        }
        int number = sizes.size();
        current = (number + current + step) % number;
        return sizes[current];
    }
    void update(const std::string &option) {
        if (option == str)
            return;
        reset();
        std::string size;
        std::stringstream stream(option);
        while (std::getline(stream, size, ' ')) {
            if (size == "onesixth") {
                sizes.push_back(StandardSize::OneSixth);
            } else if (size == "onefourth") {
                sizes.push_back(StandardSize::OneFourth);
            } else if (size == "onethird") {
                sizes.push_back(StandardSize::OneThird);
            } else if (size == "onehalf") {
                sizes.push_back(StandardSize::OneHalf);
            } else if (size == "twothirds") {
                sizes.push_back(StandardSize::TwoThirds);
            } else if (size == "threequarters") {
                sizes.push_back(StandardSize::ThreeQuarters);
            } else if (size == "fivesixths") {
                sizes.push_back(StandardSize::FiveSixths);
            } else if (size == "one") {
                sizes.push_back(StandardSize::One);
            }
        }
        // if sizes is wrong, use a default value of onehalf
        if (sizes.size() == 0)
            sizes.push_back(StandardSize::OneHalf);

        str = option;
    }

private:
    void reset() {
        sizes.clear();
    }

    std::string str;
    std::vector<StandardSize> sizes;
};

static CycleSizes window_heights;
static CycleSizes column_widths;

enum class ConfigurationSize {
    OneSixth = 0,
    OneFourth,
    OneThird,
    OneHalf,
    TwoThirds,
    ThreeQuarters,
    FiveSixths,
    One,
    Maximized,
    Floating
};

class ScrollerSizes {
public:
    ScrollerSizes() {}
    ~ScrollerSizes() { reset(); }

    Mode get_mode(PHLMONITOR monitor) {
        update_sizes(monitor);
        const auto monitor_data = monitors.find(monitor->szName);
        if (monitor_data != monitors.end()) {
            return monitor_data->second.mode;
        } else {
            // shouldn't be here
            return Mode::Row;
        }
    }

    ConfigurationSize get_window_default_height(PHLWINDOW window) {
        const auto monitor = window->m_pMonitor.lock();
        update_sizes(monitor);
        const auto monitor_data = monitors.find(monitor->szName);
        if (monitor_data != monitors.end()) {
            return monitor_data->second.window_default_height;
        } else {
            // shouldn't be here
            return ConfigurationSize::One;
        }
    }

    ConfigurationSize get_column_default_width(PHLWINDOW window) {
        const auto monitor = window->m_pMonitor.lock();
        update_sizes(monitor);
        const auto monitor_data = monitors.find(monitor->szName);
        if (monitor_data != monitors.end()) {
            return monitor_data->second.column_default_width;
        } else {
            // shouldn't be here
            return ConfigurationSize::OneHalf;
        }
    }

private:
    void update_sizes(PHLMONITOR monitor) {
        MonitorData monitor_data;
        monitor_data.mode = Mode::Row;
        monitor_data.window_default_height = updatate_window_default_height();
        monitor_data.column_default_width = updatate_column_default_width();

        static auto const *monitor_modes_str = (Hyprlang::STRING const *)HyprlandAPI::getConfigValue(PHANDLE, "plugin:scroller:monitor_options")->getDataStaticPtr();
        if (monitor_modes_str != nullptr) {
            std::string input = *monitor_modes_str;
            trim(input);
            size_t b = input.find_first_of('(');
            if (b != std::string::npos) {
                size_t e = input.find_last_of(')');
                if (e != std::string::npos) {
                    std::string modes = input.substr(b + 1, e - b - 1);
                    if (!modes.empty()) {
                        std::string monitor_mode;
                        std::stringstream stream(modes);
                        while (std::getline(stream, monitor_mode, ',')) {
                            size_t pos = monitor_mode.find('=');
                            if (pos != std::string::npos) {
                                std::string name = monitor_mode.substr(0, pos);
                                trim(name);
                                if (monitor->szName == name) {
                                    std::string data = monitor_mode.substr(pos + 1);
                                    b = data.find_first_of('(');
                                    if (b != std::string::npos) {
                                        e = data.find_last_of(')');
                                        if (e != std::string::npos) {
                                            std::string options = data.substr(b + 1, e - b - 1);
                                            trim(options);
                                            if (!options.empty()) {
                                                std::string option;
                                                std::stringstream options_stream(options);
                                                while (std::getline(options_stream, option, ';')) {
                                                    size_t pos = option.find('=');
                                                    if (pos != std::string::npos) {
                                                        std::string option_name = option.substr(0, pos);
                                                        std::string option_data = option.substr(pos + 1);
                                                        trim(option_name);
                                                        trim(option_data);
                                                        if (!option_data.empty()) {
                                                            if (option_name == "mode") {
                                                                if (option_data == "r" || option_data == "row") {
                                                                    monitor_data.mode = Mode::Row;
                                                                } else if (option_data == "c" || option_data == "col" || option_data == "column") {
                                                                    monitor_data.mode = Mode::Column;
                                                                }
                                                            } else if (option_name == "column_default_width") {
                                                                monitor_data.column_default_width = get_column_default_width_fron_string(option_data);
                                                            } else if (option_name == "window_default_height") {
                                                                monitor_data.window_default_height = get_window_default_height_fron_string(option_data);
                                                            }
                                                        }
                                                    }
                                                }
                                            }
                                        }
                                    }
                                }
                            }
                        }
                    }
                }
            }
        }
        monitors[monitor->szName] = monitor_data;
    }
    void reset() {
        monitors.clear();
    }
    ConfigurationSize get_window_default_height_fron_string(const std::string &window_height) {
        if (window_height == "one") {
            return ConfigurationSize::One;
        } else if (window_height == "onesixth") {
            return ConfigurationSize::OneSixth;
        } else if (window_height == "onefourth") {
            return ConfigurationSize::OneFourth;
        } else if (window_height == "onethird") {
            return ConfigurationSize::OneThird;
        } else if (window_height == "onehalf") {
            return ConfigurationSize::OneHalf;
        } else if (window_height == "twothirds") {
            return ConfigurationSize::TwoThirds;
        } else if (window_height == "threequarters") {
            return ConfigurationSize::ThreeQuarters;
        } else if (window_height == "fivesixths") {
            return ConfigurationSize::FiveSixths;
        } else {
            return ConfigurationSize::One;
        }
    }
    ConfigurationSize updatate_window_default_height() {
        static auto const *window_default_height = (Hyprlang::STRING const *)HyprlandAPI::getConfigValue(PHANDLE, "plugin:scroller:window_default_height")->getDataStaticPtr();
        std::string window_height = *window_default_height;
        return get_window_default_height_fron_string(window_height);
    }
    ConfigurationSize get_column_default_width_fron_string(const std::string &column_width) {
        if (column_width == "onehalf") {
            return ConfigurationSize::OneHalf;
        } else if (column_width == "onesixth") {
            return ConfigurationSize::OneSixth;
        } else if (column_width == "onefourth") {
            return ConfigurationSize::OneFourth;
        } else if (column_width == "onethird") {
            return ConfigurationSize::OneThird;
        } else if (column_width == "twothirds") {
            return ConfigurationSize::TwoThirds;
        } else if (column_width == "threequarters") {
            return ConfigurationSize::ThreeQuarters;
        } else if (column_width == "fivesixths") {
            return ConfigurationSize::FiveSixths;
        } else if (column_width == "one") {
            return ConfigurationSize::One;
        } else if (column_width == "maximized") {
            return ConfigurationSize::Maximized;
        } else if (column_width == "floating") {
            return ConfigurationSize::Floating;
        } else {
            return ConfigurationSize::OneHalf;
        }
    }
    ConfigurationSize updatate_column_default_width() {
        static auto const *column_default_width = (Hyprlang::STRING const *)HyprlandAPI::getConfigValue(PHANDLE, "plugin:scroller:column_default_width")->getDataStaticPtr();
        std::string column_width = *column_default_width;
        return get_column_default_width_fron_string(column_width);
    }

    void trim(std::string &str) {
      str.erase(str.begin(),
                std::find_if(str.begin(), str.end(),
                             [](unsigned char c) { return !std::isspace(c); }));
      str.erase(std::find_if(str.rbegin(), str.rend(),
                             [](unsigned char c) { return !std::isspace(c); })
                    .base(),
                str.end());
    }

    typedef struct {
        Mode mode;
        ConfigurationSize window_default_height;
        ConfigurationSize column_default_width;
    } MonitorData;

    std::unordered_map<std::string, MonitorData> monitors;
};

static ScrollerSizes scroller_sizes;

class Marks {
public:
    Marks() {}
    ~Marks() { reset(); }
    void reset() {
        marks.clear();
    }
    // Add a mark with name for window, overwriting any existing one with that name
    void add(PHLWINDOW window, const std::string &name) {
        const auto mark = marks.find(name);
        if (mark != marks.end()) {
            mark->second = window;
            return;
        }
        marks[name] = window;
    }
    void del(const std::string &name) {
        const auto mark = marks.find(name);
        if (mark != marks.end()) {
            marks.erase(mark);
        }
    }
    // Remove window from list of marks (used when a window gets deleted)
    void remove(PHLWINDOW window) {
        for(auto it = marks.begin(); it != marks.end();) {
            if (it->second.lock() == window)
                it = marks.erase(it);
            else
                it++;
        }
    }
    // If the mark exists, returns that window, otherwise it returns null
    PHLWINDOW visit(const std::string &name) {
        const auto mark = marks.find(name);
        if (mark != marks.end()) {
            return mark->second.lock();
        }
        return nullptr;
    }

private:
    std::unordered_map<std::string, PHLWINDOWREF> marks;
};

static Marks marks;

static std::function<SDispatchResult(std::string)> orig_moveFocusTo;

SDispatchResult this_moveFocusTo(std::string args) {
    dispatchers::dispatch_movefocus(args);
    return {};
}

static std::function<SDispatchResult(std::string)> orig_moveActiveTo;

SDispatchResult this_moveActiveTo(std::string args) {
    dispatchers::dispatch_movewindow(args);
    return {};
}

static eFullscreenMode window_fullscreen_state(PHLWINDOW window)
{
    return window->m_sFullscreenState.internal;
}

static void toggle_window_fullscreen_internal(PHLWINDOW window, eFullscreenMode mode)
{
    if (window_fullscreen_state(window) != eFullscreenMode::FSMODE_NONE) {
        g_pCompositor->setWindowFullscreenInternal(window, FSMODE_NONE);
    } else {
        g_pCompositor->setWindowFullscreenInternal(window, mode);
    }
}

static void force_focus_to_window(PHLWINDOW window) {
    g_pInputManager->unconstrainMouse();
    g_pCompositor->focusWindow(window);
    g_pCompositor->warpCursorTo(window->middle());

    g_pInputManager->m_pForcedFocus = window;
    g_pInputManager->simulateMouseMovement();
    g_pInputManager->m_pForcedFocus.reset();
}

static bool switch_to_window(PHLWINDOW from, PHLWINDOW to)
{
    auto fwid = from->workspaceID();
    auto twid = to->workspaceID();
    bool change_workspace = fwid != twid;
    if (from != to) {
        const PHLWORKSPACE workspace = to->m_pWorkspace;
        eFullscreenMode mode = workspace->m_efFullscreenMode;
        if (mode != eFullscreenMode::FSMODE_NONE) {
            if (change_workspace) {
                auto fwindow = workspace->getLastFocusedWindow(); 
                toggle_window_fullscreen_internal(fwindow, eFullscreenMode::FSMODE_NONE);
            } else {
                toggle_window_fullscreen_internal(from, eFullscreenMode::FSMODE_NONE);
            }
        }
        force_focus_to_window(to);
        if (mode != eFullscreenMode::FSMODE_NONE) {
            toggle_window_fullscreen_internal(to, mode);
        }
    }
    return !change_workspace;
}

class Window {
public:
    Window(PHLWINDOW window, double box_h) : window(window) {
        ConfigurationSize window_height =  scroller_sizes.get_window_default_height(window);
        StandardSize h;
        if (window_height == ConfigurationSize::One) {
            h = StandardSize::One;
        } else if (window_height == ConfigurationSize::OneSixth) {
            h = StandardSize::OneSixth;
        } else if (window_height == ConfigurationSize::OneFourth) {
            h = StandardSize::OneFourth;
        } else if (window_height == ConfigurationSize::OneThird) {
            h = StandardSize::OneThird;
        } else if (window_height == ConfigurationSize::OneHalf) {
            h = StandardSize::OneHalf;
        } else if (window_height == ConfigurationSize::TwoThirds) {
            h = StandardSize::TwoThirds;
        } else if (window_height == ConfigurationSize::ThreeQuarters) {
            h = StandardSize::ThreeQuarters;
        } else if (window_height == ConfigurationSize::FiveSixths) {
            h = StandardSize::FiveSixths;
        } else {
            h = StandardSize::One;
        }
        update_height(h, box_h);
    }
    PHLWINDOWREF ptr() { return window; }
    double get_geom_h() const { return box_h; }
    void set_geom_h(double geom_h) { box_h = geom_h; }
    void push_geom() {
        mem.box_h = box_h;
        mem.pos_y = window.lock()->m_vPosition.y;
    }
    void pop_geom() {
        box_h = mem.box_h;
        window.lock()->m_vPosition.y = mem.pos_y;
    }
    StandardSize get_height() const { return height; }
    void update_height(StandardSize h, double max) {
        height = h;
        switch (height) {
        case StandardSize::One:
            box_h = max;
            break;
        case StandardSize::FiveSixths:
            box_h = 5.0 * max / 6.0;
            break;
        case StandardSize::ThreeQuarters:
            box_h = 3.0 * max / 4.0;
            break;
        case StandardSize::TwoThirds:
            box_h = 2.0 * max / 3.0;
            break;
        case StandardSize::OneHalf:
            box_h = 0.5 * max;
            break;
        case StandardSize::OneThird:
            box_h = max / 3.0;
            break;
        case StandardSize::OneFourth:
            box_h = max / 4.0;
            break;
        case StandardSize::OneSixth:
            box_h = max / 6.0;
            break;
        default:
            break;
        }
    }
    void set_height_free() { height = StandardSize::Free; }
private:
    struct Memory {
        double pos_y;
        double box_h;
    };
    PHLWINDOWREF window;
    StandardSize height;
    double box_h;
    Memory mem;    // memory to store old height and win y when in maximized/overview modes
};

class Column {
public:
    Column(PHLWINDOW cwindow, double maxw, double maxh)
        : height(StandardSize::One), reorder(Reorder::Auto), initialized(false) {
        ConfigurationSize column_width = scroller_sizes.get_column_default_width(cwindow);
        if (column_width == ConfigurationSize::OneHalf) {
            width = StandardSize::OneHalf;
        } else if (column_width == ConfigurationSize::OneSixth) {
            width = StandardSize::OneSixth;
        } else if (column_width == ConfigurationSize::OneFourth) {
            width = StandardSize::OneFourth;
        } else if (column_width == ConfigurationSize::OneThird) {
            width = StandardSize::OneThird;
        } else if (column_width == ConfigurationSize::TwoThirds) {
            width = StandardSize::TwoThirds;
        } else if (column_width == ConfigurationSize::ThreeQuarters) {
            width = StandardSize::ThreeQuarters;
        } else if (column_width == ConfigurationSize::FiveSixths) {
            width = StandardSize::FiveSixths;
        } else if (column_width == ConfigurationSize::One) {
            width = StandardSize::One;
        } else if (column_width == ConfigurationSize::Maximized) {
            width = StandardSize::Free;
        } else if (column_width == ConfigurationSize::Floating) {
            if (cwindow->m_vLastFloatingSize.y > 0) {
                width = StandardSize::Free;
                maxw = cwindow->m_vLastFloatingSize.x;
            } else {
                width = StandardSize::OneHalf;
            }
        } else {
            width = StandardSize::OneHalf;
        }
        Window *window = new Window(cwindow, maxh);
        windows.push_back(window);
        active = windows.first();
        update_width(width, maxw, maxh);
    }
    Column(Window *window, StandardSize width, double maxw, double maxh)
        : width(width), height(StandardSize::One), reorder(Reorder::Auto), initialized(true) {
        window->set_geom_h(maxh);
        windows.push_back(window);
        active = windows.first();
        update_width(width, maxw, maxh);
    }
    ~Column() {
        for (auto win = windows.first(); win != nullptr; win = win->next()) {
            delete win->data();
        }
        windows.clear();
    }
    std::string get_name() const { return name; }
    void set_name (const std::string &str) { name = str; }
    bool get_init() const { return initialized; }
    void set_init() { initialized = true; }
    size_t size() {
        return windows.size();
    }
    bool has_window(PHLWINDOW window) const {
        for (auto win = windows.first(); win != nullptr; win = win->next()) {
            if (win->data()->ptr().lock() == window)
                return true;
        }
        return false;
    }
    void add_active_window(PHLWINDOW window, double maxh) {
        reorder = Reorder::Auto;
        active = windows.emplace_after(active, new Window(window, maxh));
    }
    void remove_window(PHLWINDOW window) {
        reorder = Reorder::Auto;
        for (auto win = windows.first(); win != nullptr; win = win->next()) {
            if (win->data()->ptr().lock() == window) {
                if (window == active->data()->ptr().lock()) {
                    // Make next window active (like PaperWM)
                    // If it is the last, make the previous one active.
                    // If it is the only window. active will point to nullptr,
                    // but it doesn't matter because the caller will delete
                    // the column.
                    active = active != windows.last() ? active->next() : active->prev();
                }
                windows.erase(win);
                return;
            }
        }
    }
    void focus_window(PHLWINDOW window) {
        for (auto win = windows.first(); win != nullptr; win = win->next()) {
            if (win ->data()->ptr().lock() == window) {
                active = win;
                return;
            }
        }
    }
    double get_geom_x() const {
        return geom.x;
    }
    double get_geom_w() const {
        return geom.w;
    }
    // Used by Row::fit_width()
    void set_geom_w(double w) {
        geom.w = w;
    }
    Vector2D get_height() const {
        Vector2D height;
        PHLWINDOW first = windows.first()->data()->ptr().lock();
        PHLWINDOW last = windows.last()->data()->ptr().lock();
        SBoxExtents first_reserved = first->getFullWindowReservedArea();
        SBoxExtents last_reserved = last->getFullWindowReservedArea();
        height.x = first->m_vPosition.y - first->getRealBorderSize() - first_reserved.topLeft.y;
        height.y = last->m_vPosition.y + last->m_vSize.y + last->getRealBorderSize() + last_reserved.bottomRight.y;
        return height;
    }
    void scale(const Vector2D &bmin, const Vector2D &start, double scale, double gap) {
        for (auto win = windows.first(); win != nullptr; win = win->next()) {
            win->data()->set_geom_h(win->data()->get_geom_h() * scale);
            PHLWINDOW window = win->data()->ptr().lock();
            SBoxExtents reserved_area = window->getFullWindowReservedArea();
            auto border = window->getRealBorderSize();
            auto gap0 = win == windows.first() ? 0.0 : gap;
            window->m_vPosition = start + Vector2D(border, border) + reserved_area.topLeft + (window->m_vPosition - reserved_area.topLeft - Vector2D(border, border) - bmin) * scale;
            window->m_vPosition.y += gap0;
            auto gap1 = win == windows.last() ? 0.0 : gap;
            window->m_vSize.x *= scale;
            window->m_vSize.y = (window->m_vSize.y + 2.0 * border + reserved_area.topLeft.y + reserved_area.bottomRight.y + gap0 + gap1) * scale - gap0 - gap1 - 2.0 * border - reserved_area.topLeft.y - reserved_area.bottomRight.y;
            window->m_vSize = Vector2D(std::max(window->m_vSize.x, 1.0), std::max(window->m_vSize.y, 1.0));
            window->m_vRealSize = window->m_vSize;
            window->m_vRealPosition = window->m_vPosition;
        }
    }
    void push_geom() {
        mem.geom = geom;
        for (auto w = windows.first(); w != nullptr; w = w->next()) {
            w->data()->push_geom();
        }
    }
    void pop_geom() {
        geom = mem.geom;
        for (auto w = windows.first(); w != nullptr; w = w->next()) {
            w->data()->pop_geom();
        }
    }
    void set_active_window_geometry(const Box &box) {
        PHLWINDOW wactive = active->data()->ptr().lock();
        wactive->m_vPosition = Vector2D(box.x, box.y);
        wactive->m_vSize = Vector2D(box.w, box.h);
        wactive->m_vRealPosition = wactive->m_vPosition;
        wactive->m_vRealSize = wactive->m_vSize;
    }
    void push_active_window_geometry() {
        mem.wpos = active->data()->ptr()->m_vPosition;
        mem.wsize = active->data()->ptr()->m_vSize;
    }
    void pop_active_window_geometry() {
        PHLWINDOW wactive = active->data()->ptr().lock();
        wactive->m_vPosition = mem.wpos;
        wactive->m_vSize = mem.wsize;
        wactive->m_vRealPosition = wactive->m_vPosition;
        wactive->m_vRealSize = wactive->m_vSize;
    }
    bool fullscreen() const {
        return window_fullscreen_state(active->data()->ptr().lock())  != eFullscreenMode::FSMODE_NONE;
    }
    bool maximized() const {
        return window_fullscreen_state(active->data()->ptr().lock())  == eFullscreenMode::FSMODE_MAXIMIZED;
    }
    // Used by auto-centering of columns
    void set_geom_pos(double x, double y) {
        geom.set_pos(x, y);
    }
    // Recalculates the geometry of the windows in the column
    void recalculate_col_geometry(const Vector2D &gap_x, double gap) {
        // In theory, every window in the Columm should have the same size,
        // but the standard layouts don't follow this rule (to make the code
        // simpler?). Windows close to the border of the monitor will have
        // their sizes affected by gaps_out vs. gaps_in.
        // I follow the same rules.
        // Each window has a gap to its bounding box of "gaps_in + border",
        // except on the monitor sides, where the gap is "gaps_out + border",
        // but the window sizes are different because of those different
        // gaps. So the distance between two window border boundaries is
        // two times gaps_in (one per window).
        Window *wactive = active->data();
        PHLWINDOW win = wactive->ptr().lock();
        SBoxExtents reserved_area = win->getFullWindowReservedArea();
        Vector2D topL = reserved_area.topLeft, botR = reserved_area.bottomRight;
        auto gap0 = active == windows.first() ? 0.0 : gap;
        auto gap1 = active == windows.last() ? 0.0 : gap;
        auto border = win->getRealBorderSize();
        auto a_y0 = std::round(win->m_vPosition.y - border - topL.y - gap0);
        auto a_y1 = std::round(win->m_vPosition.y - border - topL.y - gap0 + wactive->get_geom_h());
        if (a_y0 < geom.y) {
            // active starts above, set it on the top edge, unless it is the last one and there are more,
            // then move it to the bottom
            if (active == windows.last() && active->prev() != nullptr) {
                win->m_vPosition = Vector2D(geom.x + border + topL.x + gap_x.x, geom.y + geom.h - wactive->get_geom_h() + border + topL.y + gap0);
            } else {
                win->m_vPosition = Vector2D(geom.x + border + topL.x + gap_x.x, geom.y + border + topL.y + gap0);
            }
        } else if (a_y1 > geom.y + geom.h) {
            // active overflows below the bottom, move to bottom of viewport, unless it is the first window
            // and there are more, then move it to the top
            if (active == windows.first() && active->next() != nullptr) {
                win->m_vPosition = Vector2D(geom.x + border + topL.x + gap_x.x, geom.y + border + topL.y + gap0);
            } else {
                win->m_vPosition = Vector2D(geom.x + border + topL.x + gap_x.x, geom.y + geom.h - wactive->get_geom_h() + border + topL.y + gap0);
            }
        } else {
            // active window is inside the viewport
            if (reorder == Reorder::Auto) {
                // The active window should always be completely in the viewport.
                // If any of the windows next to it, above or below are already
                // in the viewport, keep the current position.
                bool keep_current = false;
                if (active->prev() != nullptr) {
                    Window *prev = active->prev()->data();
                    PHLWINDOW prev_window = prev->ptr().lock();
                    auto gap0 = active->prev() == windows.first() ? 0.0 : gap;
                    auto border = prev_window->getRealBorderSize();
                    SBoxExtents reserved_area = prev_window->getFullWindowReservedArea();
                    Vector2D topL = reserved_area.topLeft, botR = reserved_area.bottomRight;
                    auto p_y0 = std::round(prev_window->m_vPosition.y - border - topL.y - gap0);
                    auto p_y1 = std::round(prev_window->m_vPosition.y - border - topL.y - gap0 + prev->get_geom_h());
                    if (p_y0 >= geom.y && p_y1 <= geom.y + geom.h) {
                        keep_current = true;
                    }
                }
                if (!keep_current && active->next() != nullptr) {
                    Window *next = active->next()->data();
                    PHLWINDOW next_window = next->ptr().lock();
                    auto gap0 = active->next() == windows.first() ? 0.0 : gap;
                    auto border = next_window->getRealBorderSize();
                    SBoxExtents reserved_area = next_window->getFullWindowReservedArea();
                    Vector2D topL = reserved_area.topLeft, botR = reserved_area.bottomRight;
                    auto p_y0 = std::round(next_window->m_vPosition.y - border - topL.y - gap0);
                    auto p_y1 = std::round(next_window->m_vPosition.y - border - topL.y - gap0 + next->get_geom_h());
                    if (p_y0 >= geom.y && p_y1 <= geom.y + geom.h) {
                        keep_current = true;
                    }
                }
                if (!keep_current) {
                    // If not:
                    // We try to fit the window right below it if it fits
                    // completely, otherwise the one above it. If none of them fit,
                    // we leave it as it is.
                    if (active->next() != nullptr) {
                        if (wactive->get_geom_h() + active->next()->data()->get_geom_h() <= geom.h) {
                            // set next at the bottom edge of the viewport
                            win->m_vPosition = Vector2D(geom.x + border + topL.x + gap_x.x, geom.y + geom.h - wactive->get_geom_h() - active->next()->data()->get_geom_h() + border + gap0 + topL.y);
                        } else if (active->prev() != nullptr) {
                            if (active->prev()->data()->get_geom_h() + wactive->get_geom_h() <= geom.h) {
                                // set previous at the top edge of the viewport
                                win->m_vPosition = Vector2D(geom.x + border + topL.x + gap_x.x, geom.y + active->prev()->data()->get_geom_h() + border + topL.y + gap0);
                            } else {
                                // none of them fit, leave active as it is (only modify x)
                                win->m_vPosition.x = geom.x + border + topL.x + gap_x.x;
                            }
                        } else {
                            // nothing above, move active to top of viewport
                            win->m_vPosition = Vector2D(geom.x + border + topL.x + gap_x.x, geom.y + border + topL.y + gap0);
                        }
                    } else if (active->prev() != nullptr) {
                        if (active->prev()->data()->get_geom_h() + wactive->get_geom_h() <= geom.h) {
                            // set previous at the top edge of the viewport
                            win->m_vPosition = Vector2D(geom.x + border + topL.x + gap_x.x, geom.y + active->prev()->data()->get_geom_h() + border + topL.y + gap0);
                        } else {
                            // it doesn't fit and nothing above, move active to bottom of viewport
                            win->m_vPosition = Vector2D(geom.x + border + topL.x + gap_x.x, geom.y + geom.h - wactive->get_geom_h() + border + topL.y + gap0);
                        }
                    } else {
                        // nothing on the right or left, the window is in a correct position
                        win->m_vPosition.x = geom.x + border + topL.x + gap_x.x;
                    }
                } else {
                    // if the window is first or last, ensure it is at the edge
                    if (active == windows.first()) {
                        win->m_vPosition = Vector2D(geom.x + border + topL.x + gap_x.x, geom.y + border + topL.y + gap0);
                    } else if (active == windows.last()) {
                        win->m_vPosition = Vector2D(geom.x + border + topL.x + gap_x.x, geom.y + geom.h - wactive->get_geom_h() + border + topL.y + gap0);
                    } else {
                        // the window is in a correct position
                        win->m_vPosition.x = geom.x + border + topL.x + gap_x.x;
                    }
                }
            } else {
                // the window is in a correct position
                win->m_vPosition.x = geom.x + border + topL.x + gap_x.x;
            }
        }
        adjust_windows(active, gap_x, gap);
    }
    // Recalculates the geometry of the windows in the column for overview mode
    void recalculate_col_geometry_overview(const Vector2D &gap_x, double gap) {
        Window *wactive = active->data();
        PHLWINDOW win = wactive->ptr().lock();
        SBoxExtents reserved_area = win->getFullWindowReservedArea();
        Vector2D topL = reserved_area.topLeft, botR = reserved_area.bottomRight;
        auto gap0 = active == windows.first() ? 0.0 : gap;
        auto border = win->getRealBorderSize();
         // the window is in a correct position
        win->m_vPosition.x = geom.x + border + topL.x + gap_x.x;
        adjust_windows(active, gap_x, gap);
    }
    PHLWINDOW get_active_window() {
        return active->data()->ptr().lock();
    }
    void move_active_up() {
        if (active == windows.first())
            return;

        reorder = Reorder::Auto;
        auto prev = active->prev();
        windows.swap(active, prev);
    }
    void move_active_down() {
        if (active == windows.last())
            return;

        reorder = Reorder::Auto;
        auto next = active->next();
        windows.swap(active, next);
    }
    bool move_focus_up(bool focus_wrap) {
        if (active == windows.first()) {
            PHLMONITOR monitor = g_pCompositor->getMonitorInDirection('u');
            if (monitor == nullptr) {
                if (focus_wrap)
                    active = windows.last();
                return true;
            }
            // use default dispatch for movefocus (change monitor)
            orig_moveFocusTo("u");
            return false;
        }
        reorder = Reorder::Auto;
        active = active->prev();
        return true;
    }
    bool move_focus_down(bool focus_wrap) {
        if (active == windows.last()) {
            PHLMONITOR monitor = g_pCompositor->getMonitorInDirection('d');
            if (monitor == nullptr) {
                if (focus_wrap)
                    active = windows.first();
                return true;
            }
            // use default dispatch for movefocus (change monitor)
            orig_moveFocusTo("d");
            return false;
        }
        reorder = Reorder::Auto;
        active = active->next();
        return true;
    }
    void admit_window(Window *window) {
        reorder = Reorder::Auto;
        active = windows.emplace_after(active, window);
    }

    Window *expel_active(double gap) {
        reorder = Reorder::Auto;
        Window *window = active->data();
        auto act = active == windows.first() ? active->next() : active->prev();
        windows.erase(active);
        active = act;
        return window;
    }
    void align_window(Direction direction, double gap) {
        PHLWINDOW window = active->data()->ptr().lock();
        SBoxExtents reserved_area = window->getFullWindowReservedArea();
        Vector2D topL = reserved_area.topLeft, botR = reserved_area.bottomRight;
        auto border = window->getRealBorderSize();
        auto gap0 = active == windows.first() ? 0.0 : gap;
        auto gap1 = active == windows.last() ? 0.0 : gap;
        switch (direction) {
        case Direction::Up:
            reorder = Reorder::Lazy;
            window->m_vPosition.y = geom.y + border + topL.y + gap0;
            break;
        case Direction::Down:
            reorder = Reorder::Lazy;
            window->m_vPosition.y = geom.y + geom.h - (window->m_vSize.y + border + botR.y + gap1);
            break;
        case Direction::Center:
            reorder = Reorder::Lazy;
            window->m_vPosition.y = geom.y + 0.5 * (geom.h - (botR.y - topL.y + gap1 - gap0 + window->m_vSize.y));
            break;
        default:
            break;
        }
    }
    StandardSize get_width() const {
        return width;
    }
    // used by Row::fit_width()
    void set_width_free() {
        width = StandardSize::Free;
    }
    // Update heights according to new maxh
    void update_heights(double maxh) {
        for (auto win = windows.first(); win != nullptr; win = win->next()) {
            Window *window = win->data();
            window->update_height(window->get_height(), maxh);
        }
    }
    void update_width(StandardSize cwidth, double maxw, double maxh) {
        if (maximized()) {
            geom.w = maxw;
        } else {
            switch (cwidth) {
            case StandardSize::OneSixth:
                geom.w = maxw / 6.0;
                break;
            case StandardSize::OneFourth:
                geom.w = maxw / 4.0;
                break;
            case StandardSize::OneThird:
                geom.w = maxw / 3.0;
                break;
            case StandardSize::OneHalf:
                geom.w = maxw / 2.0;
                break;
            case StandardSize::TwoThirds:
                geom.w = 2.0 * maxw / 3.0;
                break;
            case StandardSize::ThreeQuarters:
                geom.w = 3.0 * maxw / 4.0;
                break;
            case StandardSize::FiveSixths:
                geom.w = 5.0 * maxw / 6.0;
                break;
            case StandardSize::One:
                geom.w = maxw;
                break;
            case StandardSize::Free:
                // Only used when creating a column from an expelled window
                geom.w = maxw;
            default:
                break;
            }
        }
        geom.h = maxh;
        width = cwidth;
    }
    void fit_size(FitSize fitsize, const Vector2D &gap_x, double gap) {
        reorder = Reorder::Auto;
        ListNode<Window *> *from, *to;
        switch (fitsize) {
        case FitSize::Active:
            from = to = active;
            break;
        case FitSize::Visible:
            for (auto w = windows.first(); w != nullptr; w = w->next()) {
                auto gap0 = w == windows.first() ? 0.0 : gap;
                auto gap1 = w == windows.last() ? 0.0 : gap;
                Window *win = w->data();
                PHLWINDOW window = win->ptr().lock();
                SBoxExtents reserved_area = window->getFullWindowReservedArea();
                Vector2D topL = reserved_area.topLeft, botR = reserved_area.bottomRight;
                auto border = window->getRealBorderSize();
                auto c0 = window->m_vPosition.y - border - topL.y;
                auto c1 = window->m_vPosition.y - border - topL.y - gap0 + win->get_geom_h();
                if (c0 < geom.y + geom.h && c0 >= geom.y ||
                    c1 > geom.y && c1 <= geom.y + geom.h ||
                    //should never happen as windows are never taller than the screen
                    c0 < geom.y && c1 >= geom.y + geom.h) {
                    from = w;
                    break;
                }
            }
            for (auto w = windows.last(); w != nullptr; w = w->prev()) {
                auto gap0 = w == windows.first() ? 0.0 : gap;
                auto gap1 = w == windows.last() ? 0.0 : gap;
                Window *win = w->data();
                PHLWINDOW window = win->ptr().lock();
                SBoxExtents reserved_area = window->getFullWindowReservedArea();
                Vector2D topL = reserved_area.topLeft, botR = reserved_area.bottomRight;
                auto border = window->getRealBorderSize();
                auto c0 = window->m_vPosition.y - border - topL.y;
                auto c1 = window->m_vPosition.y - border - topL.y - gap0 + win->get_geom_h();
                if (c0 < geom.y + geom.h && c0 >= geom.y ||
                    c1 > geom.y && c1 <= geom.y + geom.h ||
                    //should never happen as columns are never wider than the screen
                    c0 < geom.y && c1 >= geom.y + geom.h) {
                    to = w;
                    break;
                }
            }
            break;
        case FitSize::All:
            from = windows.first();
            to = windows.last();
            break;
        case FitSize::ToEnd:
            from = active;
            to = windows.last();
            break;
        case FitSize::ToBeg:
            from = windows.first();
            to = active;
            break;
        default:
            return;
        }

        // Now align from top of the screen (geom.y), split height of
        // screen (geom.h) among from->to, and readapt the rest
        if (from != nullptr && to != nullptr) {
            auto c = from;
            double total = 0.0;
            for (auto c = from; c != to->next(); c = c->next()) {
                total += c->data()->get_geom_h();
            }
            for (auto c = from; c != to->next(); c = c->next()) {
                Window *win = c->data();
                win->set_height_free();
                win->set_geom_h(win->get_geom_h() / total * geom.h);
            }
            auto gap0 = from == windows.first() ? 0.0 : gap;
            PHLWINDOW from_window = from->data()->ptr().lock();
            from_window->m_vPosition.y = geom.y + gap0 + from_window->getRealBorderSize() + from_window->getFullWindowReservedArea().topLeft.y;

            adjust_windows(from, gap_x, gap);
        }
    }
    void cycle_size_active_window(int step, const Vector2D &gap_x, double gap) {
        static auto const *window_heights_str = (Hyprlang::STRING const *)HyprlandAPI::getConfigValue(PHANDLE, "plugin:scroller:window_heights")->getDataStaticPtr();
        window_heights.update(*window_heights_str);

        reorder = Reorder::Auto;
        StandardSize height = active->data()->get_height();
        if (height == StandardSize::Free) {
            // When cycle-resizing from Free mode, always move back to first
            height = window_heights.get_default();
        } else {
            height = window_heights.get_next(height, step);
        }
        active->data()->update_height(height, geom.h);
        recalculate_col_geometry(gap_x, gap);
    }
private:
    // Adjust all the windows in the column using 'window' as anchor
    void adjust_windows(ListNode<Window *> *win, const Vector2D &gap_x, double gap) {
        // 2. adjust positions of windows above
        for (auto w = win->prev(), p = win; w != nullptr; p = w, w = w->prev()) {
            PHLWINDOW ww = w->data()->ptr().lock();
            PHLWINDOW pw = p->data()->ptr().lock();
            auto wgap0 = w == windows.first() ? 0.0 : gap;
            auto wborder = ww->getRealBorderSize();
            auto pborder = pw->getRealBorderSize();
            auto wreserved = ww->getFullWindowReservedArea();
            auto preserved = pw->getFullWindowReservedArea();
            ww->m_vPosition = Vector2D(geom.x + wborder + wreserved.topLeft.x + gap_x.x, pw->m_vPosition.y - gap - pborder - preserved.topLeft.y - w->data()->get_geom_h() + wborder + wreserved.topLeft.y + wgap0);
        }
        // 3. adjust positions of windows below
        for (auto w = win->next(), p = win; w != nullptr; p = w, w = w->next()) {
            PHLWINDOW ww = w->data()->ptr().lock();
            PHLWINDOW pw = p->data()->ptr().lock();
            auto pgap0 = p == windows.first() ? 0.0 : gap;
            auto wborder = ww->getRealBorderSize();
            auto pborder = pw->getRealBorderSize();
            auto wreserved = ww->getFullWindowReservedArea();
            auto preserved = pw->getFullWindowReservedArea();
            ww->m_vPosition = Vector2D(geom.x + wborder + wreserved.topLeft.x + gap_x.x, pw->m_vPosition.y - pborder - pgap0 - preserved.topLeft.y + p->data()->get_geom_h() + wborder + wreserved.topLeft.y + gap);
        }
        for (auto w = windows.first(); w != nullptr; w = w->next()) {
            PHLWINDOW win = w->data()->ptr().lock();
            auto gap0 = w == windows.first() ? 0.0 : gap;
            auto gap1 = w == windows.last() ? 0.0 : gap;
            auto border = win->getRealBorderSize();
            auto wh = w->data()->get_geom_h();
            auto reserved = win->getFullWindowReservedArea();
            //win->m_vSize = Vector2D(geom.w - 2.0 * border - gap_x.x - gap_x.y, wh - 2.0 * border - gap0 - gap1);
            win->m_vSize = Vector2D(std::max(geom.w - 2.0 * border - reserved.topLeft.x - reserved.bottomRight.x - gap_x.x - gap_x.y, 1.0), std::max(wh - 2.0 * border - reserved.topLeft.y - reserved.bottomRight.y - gap0 - gap1, 1.0));
            win->m_vRealPosition = win->m_vPosition;
            win->m_vRealSize = win->m_vSize;
        }
    }
public:
    void resize_active_window(double maxw, const Vector2D &gap_x, double gap, const Vector2D &delta) {
        // First, check if resize is possible or it would leave any window
        // with an invalid size.
        PHLWINDOW win = active->data()->ptr().lock();
        SBoxExtents reserved_area = win->getFullWindowReservedArea();
        Vector2D topL = reserved_area.topLeft, botR = reserved_area.bottomRight;

        // Width check
        auto border = win->getRealBorderSize();
        auto rwidth = geom.w + delta.x - 2.0 * border - topL.x - botR.x - gap_x.x - gap_x.y;
        // Now we check for a size smaller than the maximum possible gap, so
        // we never get in trouble when a window gets expelled from a column
        // with gaps_out, gaps_in, to a column with gaps_in on both sides.
        auto mwidth = geom.w + delta.x - topL.x - botR.x - 2.0 * (border + std::max(std::max(gap_x.x, gap_x.y), gap));
        if (mwidth <= 0.0 || rwidth >= maxw)
            return;

        if (std::abs(static_cast<int>(delta.y)) > 0) {
            for (auto win = windows.first(); win != nullptr; win = win->next()) {
                auto gap0 = win == windows.first() ? 0.0 : gap;
                auto gap1 = win == windows.last() ? 0.0 : gap;
                SBoxExtents reserved_area = win->data()->ptr().lock()->getFullWindowReservedArea();
                Vector2D topL = reserved_area.topLeft, botR = reserved_area.bottomRight;
                auto wh = win->data()->get_geom_h() - gap0 - gap1 - 2.0 * border - topL.y - botR.y;
                if (win == active)
                    wh += delta.y;
                if (wh <= 0.0 || wh + 2.0 * win->data()->ptr().lock()->getRealBorderSize() + gap0 + gap1 + topL.y + botR.y > geom.h)
                    // geom.h already includes gaps_out
                    return;
            }
        }
        reorder = Reorder::Auto;
        // Now, resize.
        width = StandardSize::Free;

        geom.w += delta.x;
        if (std::abs(static_cast<int>(delta.y)) > 0) {
            for (auto win = windows.first(); win != nullptr; win = win->next()) {
                Window *window = win->data();
                if (win == active) {
                    window->set_geom_h(window->get_geom_h() + delta.y);
                    window->set_height_free();
                }
            }
        }
    }

private:
    struct Memory {
        Box geom;        // memory of the column's box while in overview mode
        Vector2D wpos;   // memory of the active window pos
        Vector2D wsize;  // and size while in fullscreen mode
    };
    StandardSize width;
    StandardSize height;
    Reorder reorder;
    bool initialized;
    Box geom;        // bbox of column
    Memory mem;      // memory
    Box full;        // full screen geometry
    ListNode<Window *> *active;
    List<Window *> windows;
    std::string name;
};

class Row {
public:
    Row(PHLWINDOW window)
        : workspace(window->workspaceID()), reorder(Reorder::Auto),
        overview(false), active(nullptr), pinned(nullptr) {
        post_event("overview");
        const auto PMONITOR = window->m_pMonitor.lock();
        set_mode(scroller_sizes.get_mode(PMONITOR));
        update_sizes(PMONITOR);
    }
    ~Row() {
        for (auto col = columns.first(); col != nullptr; col = col->next()) {
            delete col->data();
        }
        columns.clear();
    }
    WORKSPACEID get_workspace() const { return workspace; }
    bool has_window(PHLWINDOW window) const {
        for (auto col = columns.first(); col != nullptr; col = col->next()) {
            if (col->data()->has_window(window))
                return true;
        }
        return false;
    }
    PHLWINDOW get_active_window() const {
        return active->data()->get_active_window();
    }
    bool is_active(PHLWINDOW window) const {
        return get_active_window() == window;
    }
    void add_active_window(PHLWINDOW window) {
        bool overview_on = overview;
        if (overview)
            toggle_overview();

        eFullscreenMode fsmode;
        if (active != nullptr) {
            auto awindow = get_active_window();
            fsmode = window_fullscreen_state(awindow);
            if (fsmode != eFullscreenMode::FSMODE_NONE) {
                toggle_window_fullscreen_internal(awindow, eFullscreenMode::FSMODE_NONE);
            }
        } else {
            fsmode = eFullscreenMode::FSMODE_NONE;
        }

        if (active && mode == Mode::Column) {
            active->data()->add_active_window(window, max.h);
            if (fsmode == eFullscreenMode::FSMODE_NONE)
                active->data()->recalculate_col_geometry(calculate_gap_x(active), gap);
        } else {
            active = columns.emplace_after(active, new Column(window, max.w, max.h));
            if (fsmode == eFullscreenMode::FSMODE_NONE) {
                reorder = Reorder::Auto;
                recalculate_row_geometry();
            }
        }
        if (fsmode != eFullscreenMode::FSMODE_NONE) {
            toggle_window_fullscreen_internal(window, fsmode);
            force_focus_to_window(window);
        }
        if (overview_on)
            toggle_overview();
    }

    // Remove a window and re-adapt rows and columns, returning
    // true if successful, or false if this is the last row
    // so the layout can remove it.
    bool remove_window(PHLWINDOW window) {
        bool overview_on = overview;
        if (overview)
            toggle_overview();

        reorder = Reorder::Auto;
        for (auto c = columns.first(); c != nullptr; c = c->next()) {
            Column *col = c->data();
            if (col->has_window(window)) {
                col->remove_window(window);
                if (col->size() == 0) {
                    if (c == pinned) {
                        pinned = nullptr;
                    }
                    if (c == active) {
                        // make NEXT one active before deleting (like PaperWM)
                        // If active was the only one left, doesn't matter
                        // whether it points to end() or not, the row will
                        // be deleted by the parent.
                        active = active != columns.last() ? active->next() : active->prev();
                    }
                    delete col;
                    columns.erase(c);
                    if (columns.empty()) {
                        return false;
                    } else {
                        recalculate_row_geometry();
                        break;
                    }
                } else {
                    c->data()->recalculate_col_geometry(calculate_gap_x(c), gap);
                    break;
                }
            }
        }
        if(overview_on)
            toggle_overview();

        return true;
    }
    void focus_window(PHLWINDOW window) {
        for (auto c = columns.first(); c != nullptr; c = c->next()) {
            if (c->data()->has_window(window)) {
                c->data()->focus_window(window);
                active = c;
                recalculate_row_geometry();
                return;
            }
        }
    }
    bool move_focus(Direction dir, bool focus_wrap) {
        auto from = get_active_window();
        eFullscreenMode mode = window_fullscreen_state(from);

        switch (dir) {
        case Direction::Left:
            if (!move_focus_left(focus_wrap))
                return false;
            break;
        case Direction::Right:
            if (!move_focus_right(focus_wrap))
                return false;
            break;
        case Direction::Up:
            if (!active->data()->move_focus_up(focus_wrap))
                return false;
            break;
        case Direction::Down:
            if (!active->data()->move_focus_down(focus_wrap))
                return false;
            break;
        case Direction::Begin:
            move_focus_begin();
            break;
        case Direction::End:
            move_focus_end();
            break;
        default:
            return true;
        }

        if (mode == eFullscreenMode::FSMODE_NONE) {
            reorder = Reorder::Auto;
            recalculate_row_geometry();
        }
        auto to = get_active_window();
        return switch_to_window(from, to);
    }

private:
    bool move_focus_left(bool focus_wrap) {
        if (active == columns.first()) {
            PHLMONITOR monitor = g_pCompositor->getMonitorInDirection('l');
            if (monitor == nullptr) {
                if (focus_wrap)
                    active = columns.last();
                return true;
            }

            orig_moveFocusTo("l");
            return false;
        }
        active = active->prev();
        return true;
    }
    bool move_focus_right(bool focus_wrap) {
        if (active == columns.last()) {
            PHLMONITOR monitor = g_pCompositor->getMonitorInDirection('r');
            if (monitor == nullptr) {
                if (focus_wrap)
                    active = columns.first();
                return true;
            }

            orig_moveFocusTo("r");
            return false;
        }
        active = active->next();
        return true;
    }
    void move_focus_begin() {
        active = columns.first();
    }
    void move_focus_end() {
        active = columns.last();
    }

    // Calculate lateral gaps for a column
    Vector2D calculate_gap_x(const ListNode<Column *> *column) const {
        // First and last columns need a different gap
        auto gap0 = column == columns.first() ? 0.0 : gap;
        auto gap1 = column == columns.last() ? 0.0 : gap;
        return Vector2D(gap0, gap1);
    }

public:
    void resize_active_column(int step) {
        if (active->data()->fullscreen())
            return;

        bool overview_on = overview;
        if (overview)
            toggle_overview();

        if (mode == Mode::Column) {
            active->data()->cycle_size_active_window(step, calculate_gap_x(active), gap);
        } else {
            static auto const *column_widths_str = (Hyprlang::STRING const *)HyprlandAPI::getConfigValue(PHANDLE, "plugin:scroller:column_widths")->getDataStaticPtr();
            column_widths.update(*column_widths_str);

            StandardSize width = active->data()->get_width();
            if (width == StandardSize::Free) {
                // When cycle-resizing from Free mode, always move back to first
                width = column_widths.get_default();
            } else {
                width = column_widths.get_next(width, step);
            }
            active->data()->update_width(width, max.w, max.h);
            reorder = Reorder::Auto;
            recalculate_row_geometry();
        }
        if(overview_on)
            toggle_overview();
    }
    void resize_active_window(const Vector2D &delta) {
        // If the active window in the active column is fullscreen, ignore.
        if (active->data()->fullscreen())
            return;
        if (overview)
            return;

        active->data()->resize_active_window(max.w, calculate_gap_x(active), gap, delta);
        recalculate_row_geometry();
    }
    void set_mode(Mode m) {
        mode = m;
        post_event("mode");
    }
    void align_column(Direction dir) {
        if (active->data()->fullscreen())
            return;
        if (overview)
            return;

        switch (dir) {
        case Direction::Left:
            active->data()->set_geom_pos(max.x, max.y);
            break;
        case Direction::Right:
            active->data()->set_geom_pos(max.x + max.w - active->data()->get_geom_w(), max.y);
            break;
        case Direction::Center:
            if (mode == Mode::Column) {
                active->data()->align_window(Direction::Center, gap);
                active->data()->recalculate_col_geometry(calculate_gap_x(active), gap);
            } else {
                center_active_column();
            }
            break;
        case Direction::Up:
        case Direction::Down:
            active->data()->align_window(dir, gap);
            active->data()->recalculate_col_geometry(calculate_gap_x(active), gap);
            break;
        default:
            return;
        }
        reorder = Reorder::Lazy;
        recalculate_row_geometry();
    }
    void pin() {
        pinned = active;
    }
    void unpin() {
        pinned = nullptr;
    }
private:
    void center_active_column() {
        Column *column = active->data();
        if (column->fullscreen())
            return;

        switch (column->get_width()) {
        case StandardSize::OneSixth:
            column->set_geom_pos(max.x + 5.0 * max.w / 12.0, max.y);
            break;
        case StandardSize::OneFourth:
            column->set_geom_pos(max.x + 3.0 * max.w / 8.0, max.y);
            break;
        case StandardSize::OneThird:
            column->set_geom_pos(max.x + max.w / 3.0, max.y);
            break;
        case StandardSize::OneHalf:
            column->set_geom_pos(max.x + max.w / 4.0, max.y);
            break;
        case StandardSize::TwoThirds:
            column->set_geom_pos(max.x + max.w / 6.0, max.y);
            break;
        case StandardSize::ThreeQuarters:
            column->set_geom_pos(max.x + max.w / 8.0, max.y);
            break;
        case StandardSize::FiveSixths:
            column->set_geom_pos(max.x + max.w / 12.0, max.y);
            break;
        case StandardSize::One:
            column->set_geom_pos(max.x, max.y);
            break;
        case StandardSize::Free:
            column->set_geom_pos(0.5 * (max.w - column->get_geom_w()), max.y);
            break;
        default:
            break;
        }
    }
public:
    void move_active_window_to_group(const std::string &name) {
        for (auto c = columns.first(); c != nullptr; c = c->next()) {
            Column *col = c->data();
            if (col->get_name() == name) {
                PHLWINDOW window = active->data()->get_active_window();
                remove_window(window);
                col->add_active_window(window, max.h);
                if (!window->isFullscreen())
                    col->recalculate_col_geometry(calculate_gap_x(c), gap);
                active = c;
                if (!window->isFullscreen())
                    recalculate_row_geometry();
                else {
                    force_focus_to_window(window);
                }
                return;
            }
        }
        active->data()->set_name(name);
    }
    void move_active_column(Direction dir) {
        bool overview_on = overview;
        if (overview)
            toggle_overview();

        switch (dir) {
        case Direction::Right:
            if (active != columns.last()) {
                auto next = active->next();
                columns.swap(active, next);
            }
            break;
        case Direction::Left:
            if (active != columns.first()) {
                auto prev = active->prev();
                columns.swap(active, prev);
            }
            break;
        case Direction::Up:
            active->data()->move_active_up();
            break;
        case Direction::Down:
            active->data()->move_active_down();
            break;
        case Direction::Begin: {
            if (active == columns.first())
                break;
            columns.move_before(columns.first(), active);
            break;
        }
        case Direction::End: {
            if (active == columns.last())
                break;
            columns.move_after(columns.last(), active);
            break;
        }
        case Direction::Center:
            return;
        }
 
        auto window = active->data()->get_active_window();
        eFullscreenMode mode = window_fullscreen_state(window);
        if (mode == eFullscreenMode::FSMODE_NONE) {
            reorder = Reorder::Auto;
            recalculate_row_geometry();
        }

        if(overview_on)
            toggle_overview();
    }
    void admit_window_left() {
        if (active->data()->fullscreen())
            return;
        if (active == columns.first())
            return;

        bool overview_on = overview;
        if (overview)
            toggle_overview();

        auto w = active->data()->expel_active(gap);
        auto prev = active->prev();
        if (active->data()->size() == 0) {
            if (active == pinned)
                pinned = nullptr;
            columns.erase(active);
        }
        active = prev;
        active->data()->admit_window(w);

        reorder = Reorder::Auto;
        recalculate_row_geometry();

        post_event("admitwindow");

        if(overview_on)
            toggle_overview();
    }
    void expel_window_right() {
        if (active->data()->fullscreen())
            return;
        if (active->data()->size() == 1)
            // nothing to expel
            return;

        bool overview_on = overview;
        if (overview)
            toggle_overview();

        auto w = active->data()->expel_active(gap);
        StandardSize width = active->data()->get_width();
        // This code inherits the width of the original column. There is a
        // problem with that when the mode is "Free". The new column may have
        // more reserved space for gaps, and the new window in that column
        // end up having negative size --> crash.
        // There are two options:
        // 1. We don't let column resizing make a column smaller than gap
        // 2. We compromise and inherit the StandardSize attribute unless it is
        // "Free". In that case, we force OneHalf (the default).
#if 1
        double maxw = width == StandardSize::Free ? active->data()->get_geom_w() : max.w;
#else
        double maxw = max.w;
        if (width == StandardSize::Free)
            width = StandardSize::OneHalf;
#endif
        active = columns.emplace_after(active, new Column(w, width, maxw, max.h));
        // Initialize the position so it is located after its previous column
        // This helps the heuristic in recalculate_row_geometry()
        active->data()->set_geom_pos(active->prev()->data()->get_geom_x() + active->prev()->data()->get_geom_w(), max.y);

        reorder = Reorder::Auto;
        recalculate_row_geometry();

        post_event("expelwindow");

        if(overview_on)
            toggle_overview();
    }
    Vector2D predict_window_size() const {
        return Vector2D(0.5 * max.w, max.h);
    }
    void post_event(const std::string &event) {
        if (event == "mode") {
            g_pEventManager->postEvent(SHyprIPCEvent{"scroller", std::format("mode, {}", mode == Mode::Row? "row" : "column")});
        } else if (event == "overview") {
            g_pEventManager->postEvent(SHyprIPCEvent{"scroller", std::format("overview, {}", overview? 1 : 0)});
        } else if (event == "admitwindow") {
            g_pEventManager->postEvent(SHyprIPCEvent{"scroller", "admitwindow"});
        } else if (event == "expelwindow") {
            g_pEventManager->postEvent(SHyprIPCEvent{"scroller", "expelwindow"});
        }
    }
    // Returns the old viewport
    Box update_sizes(PHLMONITOR monitor) {
        // for gaps outer
        static auto PGAPSINDATA = CConfigValue<Hyprlang::CUSTOMTYPE>("general:gaps_in");
        static auto PGAPSOUTDATA = CConfigValue<Hyprlang::CUSTOMTYPE>("general:gaps_out");
        auto *const PGAPSIN = (CCssGapData *)(PGAPSINDATA.ptr())->getData();
        auto *const PGAPSOUT = (CCssGapData *)(PGAPSOUTDATA.ptr())->getData();
        // For now, support only constant CCssGapData
        auto gaps_in = PGAPSIN->top;
        auto gaps_out = *PGAPSOUT;

        const auto SIZE = monitor->vecSize;
        const auto POS = monitor->vecPosition;
        const auto TOPLEFT = monitor->vecReservedTopLeft;
        const auto BOTTOMRIGHT = monitor->vecReservedBottomRight;

        full = Box(POS, SIZE);
        const Box newmax = Box(POS.x + TOPLEFT.x + gaps_out.left,
                POS.y + TOPLEFT.y + gaps_out.top,
                SIZE.x - TOPLEFT.x - BOTTOMRIGHT.x - gaps_out.left - gaps_out.right,
                SIZE.y - TOPLEFT.y - BOTTOMRIGHT.y - gaps_out.top - gaps_out.bottom);
        gap = gaps_in;

        const Box oldmax = max;
        max = newmax;
        return oldmax;
    }
    void set_fullscreen_mode_windows(eFullscreenMode mode) {
        Column *column = active->data();
        switch (mode) {
        case eFullscreenMode::FSMODE_NONE:
            break;
        case eFullscreenMode::FSMODE_FULLSCREEN:
            column->set_active_window_geometry(full);
            break;
        case eFullscreenMode::FSMODE_MAXIMIZED:
            column->set_active_window_geometry(max);
            break;
        default:
            break;
        }
    }

    void set_fullscreen_mode(eFullscreenMode cur_mode, eFullscreenMode new_mode) {
        reorder = Reorder::Auto;
        Column *column = active->data();
        switch (new_mode) {
        case eFullscreenMode::FSMODE_NONE:
            column->pop_active_window_geometry();
            set_fullscreen_mode_windows(eFullscreenMode::FSMODE_NONE);
            break;
        case eFullscreenMode::FSMODE_FULLSCREEN:
            if (cur_mode == eFullscreenMode::FSMODE_NONE)
                column->push_active_window_geometry();
            set_fullscreen_mode_windows(eFullscreenMode::FSMODE_FULLSCREEN);
            break;
        case eFullscreenMode::FSMODE_MAXIMIZED:
            if (cur_mode == eFullscreenMode::FSMODE_NONE)
                column->push_active_window_geometry();
            set_fullscreen_mode_windows(eFullscreenMode::FSMODE_MAXIMIZED);
            break;
        default:
            return;
        }
    }

    void fit_size(FitSize fitsize) {
        if (active->data()->fullscreen()) {
            return;
        }
        if (overview) {
            return;
        }
        if (mode == Mode::Column) {
            active->data()->fit_size(fitsize, calculate_gap_x(active), gap);
            return;
        }
        ListNode<Column *> *from, *to;
        switch (fitsize) {
        case FitSize::Active:
            from = to = active;
            break;
        case FitSize::Visible:
            for (auto c = columns.first(); c != nullptr; c = c->next()) {
                Column *col = c->data();
                auto c0 = col->get_geom_x();
                auto c1 = col->get_geom_x() + col->get_geom_w();
                if (c0 < max.x + max.w && c0 >= max.x ||
                    c1 > max.x && c1 <= max.x + max.w ||
                    //should never happen as columns are never wider than the screen
                    c0 < max.x && c1 >= max.x + max.w) {
                    from = c;
                    break;
                }
            }
            for (auto c = columns.last(); c != nullptr; c = c->prev()) {
                Column *col = c->data();
                auto c0 = col->get_geom_x();
                auto c1 = col->get_geom_x() + col->get_geom_w();
                if (c0 < max.x + max.w && c0 >= max.x ||
                    c1 > max.x && c1 <= max.x + max.w ||
                    //should never happen as columns are never wider than the screen
                    c0 < max.x && c1 >= max.x + max.w) {
                    to = c;
                    break;
                }
            }
            break;
        case FitSize::All:
            from = columns.first();
            to = columns.last();
            break;
        case FitSize::ToEnd:
            from = active;
            to = columns.last();
            break;
        case FitSize::ToBeg:
            from = columns.first();
            to = active;
            break;
        default:
            return;
        }

        // Now align from to left edge of the screen (max.x), split width of
        // screen (max.w) among from->to, and readapt the rest
        if (from != nullptr && to != nullptr) {
            auto c = from;
            double total = 0.0;
            for (auto c = from; c != to->next(); c = c->next()) {
                total += c->data()->get_geom_w();
            }
            for (auto c = from; c != to->next(); c = c->next()) {
                Column *col = c->data();
                col->set_width_free();
                col->set_geom_w(col->get_geom_w() / total * max.w);
            }
            from->data()->set_geom_pos(max.x, max.y);

            adjust_columns(from);
        }
    }

    bool is_overview() const {
        return overview;
    }

    void toggle_overview() {
        overview = !overview;
        post_event("overview");
        if (overview) {
            // Turn off fullscreen mode if enabled
            auto window = get_active_window();
            preoverview_fsmode = window_fullscreen_state(window);
            if (preoverview_fsmode != eFullscreenMode::FSMODE_NONE) {
                toggle_window_fullscreen_internal(window, preoverview_fsmode);
            }
            // Find the bounding box
            Vector2D bmin(max.x + max.w, max.y + max.h);
            Vector2D bmax(max.x, max.y);
            for (auto c = columns.first(); c != nullptr; c = c->next()) {
                auto cx0 = c->data()->get_geom_x();
                auto cx1 = cx0 + c->data()->get_geom_w();
                Vector2D cheight = c->data()->get_height();
                if (cx0 < bmin.x)
                    bmin.x = cx0;
                if (cx1 > bmax.x)
                    bmax.x = cx1;
                if (cheight.x < bmin.y)
                    bmin.y = cheight.x;
                if (cheight.y > bmax.y)
                    bmax.y = cheight.y;
            }
            double w = bmax.x - bmin.x;
            double h = bmax.y - bmin.y;
            double scale = std::min(max.w / w, max.h / h);
            for (auto c = columns.first(); c != nullptr; c = c->next()) {
                Column *col = c->data();
                col->push_geom();
                Vector2D cheight = col->get_height();
                Vector2D offset(0.5 * (max.w - w * scale), 0.5 * (max.h - h * scale));
                col->set_geom_pos(offset.x + max.x + (col->get_geom_x() - bmin.x) * scale, offset.y + max.y + (cheight.x - bmin.y) * scale);
                col->set_geom_w(col->get_geom_w() * scale);
                Vector2D start(offset.x + max.x, offset.y + max.y);
                col->scale(bmin, start, scale, gap);
            }
            adjust_overview_columns();
        } else {
            for (auto c = columns.first(); c != nullptr; c = c->next()) {
                Column *col = c->data();
                col->pop_geom();
            }
            // Try to maintain the positions except if the active is not visible,
            // in that case, make it visible.
            Column *acolumn = active->data();
            if (acolumn->get_geom_x() < max.x) {
                acolumn->set_geom_pos(max.x, max.y);
            } else if (acolumn->get_geom_x() + acolumn->get_geom_w() > max.x + max.w) {
                acolumn->set_geom_pos(max.x + max.w - acolumn->get_geom_w(), max.y);
            }
            adjust_columns(active);
            // Turn fullscreen mode back on if enabled
            auto window = get_active_window();
            if (preoverview_fsmode != eFullscreenMode::FSMODE_NONE) {
                toggle_window_fullscreen_internal(window, preoverview_fsmode);
            }
        }
    }

    void update_windows(const Box &oldmax) {
        if (oldmax == max)
            return;

        // Update active column position
        if (active) {
            double posx = max.x + max.w * (active->data()->get_geom_x() - oldmax.x) / oldmax.w;
            active->data()->set_geom_pos(posx, max.y);
        }
        // Redo all columns: widths according to "width" (unless Free)
        for (auto col = columns.first(); col != nullptr; col = col->next()) {
            Column *column = col->data();
            StandardSize width = column->get_width();
            double maxw = width == StandardSize::Free ? column->get_geom_w() : max.w;
            column->update_width(width, maxw, max.h);
            // Redo all windows for each column according to "height" (unless Free)
            column->update_heights(max.h);
        }
        recalculate_row_geometry();
    }

    void recalculate_row_geometry() {
        if (active == nullptr)
            return;

        if (active->data()->fullscreen()) {
            return;
        }
        if (overview) {
            adjust_overview_columns();
            return;
        }
        auto a_w = active->data()->get_geom_w();
        double a_x;
        if (active->data()->get_init()) {
            a_x = active->data()->get_geom_x();
        } else {
            // If the column hasn't been initialized yet (newly created window),
            // we know it will be located on the right of active->prev()
            if (active->prev()) {
                // there is a previous one, locate it on its right
                Column *prev = active->prev()->data();
                a_x = prev->get_geom_x() + prev->get_geom_w();
            } else {
                // first window, locate it at the center
                a_x = max.x + 0.5 * (max.w - a_w);
            }
            // mark column as initialized
            active->data()->set_init();
        }
        // Pinned will stay in place, with active having second priority to fit in
        // the screen on either side of pinned.
        if (pinned != nullptr) {
            // If pinned got kicked out of the screen (overview, for example),
            // bring it back in
            auto p_w = pinned->data()->get_geom_w();
            auto p_x = pinned->data()->get_geom_x();
            if (p_x < max.x) {
                pinned->data()->set_geom_pos(max.x, max.y);
            } else if (std::round(p_x + p_w) > max.x + max.w) {
                // pin overflows to the right, move to end of viewport
                pinned->data()->set_geom_pos(max.x + max.w - p_w, max.y);
            }
            if (a_x < max.x || std::round(a_x + a_w) > max.x + max.w) {
                // Active doesn't fit, move it next to pinned
                // Find space
                auto p_w = pinned->data()->get_geom_w();
                auto p_x = pinned->data()->get_geom_x();
                auto const lt = p_x - max.x;
                auto const rt = max.x + max.w - p_x - p_w;
                // From pinned to active, try to fit as many columns as possible
                if (pinned != active) {
                    int p = 0, a = 0;
                    int i = 0;
                    for (auto col = columns.first(); col != nullptr; col = col->next(), ++i) {
                        if (col == pinned)
                            p = i;
                        if (col == active)
                            a = i;
                    }
                    if (p > a) {
                        // Pinned is after active
                        // The priority is to keep active before pinned if it fits.
                        // If it doesn't fit, see if it fits right after, otherwise
                        // move it to where there is more room, and if equal, leave
                        // it where it is.
                        auto w = a_w;
                        auto swap = active, col = active;
                        while (col != pinned) {
                            if (0 <= std::round(lt - w)) {
                                swap = col;
                                col = col->next();
                                w += col->data()->get_geom_w();
                            } else {
                                break;
                            }
                        }
                        if (0 <= std::round(lt - a_w)) {
                            // fits on the left
                            columns.move_after(swap, pinned);
                        } else {
                            if (0 <= std::round(rt - a_w) || rt > lt) {
                                // fits on the right
                                columns.move_before(active, pinned);
                            } else {
                                // doesn't fit, and there is the same or more
                                // room on the side where it is now, leave it
                                // there (right before pinned)
                                columns.move_after(active, pinned);
                            }
                        }
                    } else {
                        // Pinned is before active
                        // The priority is to keep active after pinned if it fits.
                        // If it doesn't fit, see if it fits right before, otherwise
                        // move it to where there is more room, and if equal, leave
                        // it where it is.
                        auto w = a_w;
                        auto swap = active, col = active;
                        while (col != pinned) {
                            if (0 <= std::round(rt - w)) {
                                swap = col;
                                col = col->prev();
                                w += col->data()->get_geom_w();
                            } else {
                                break;
                            }
                        }
                        if (0 <= std::round(rt - a_w)) {
                            // fits on the right
                            columns.move_before(swap, pinned);
                        } else {
                            if (0 <= std::round(lt - a_w) || lt > rt) {
                                // fits on the left or there is more room there
                                columns.move_after(active, pinned);
                            } else {
                                // doesn't fit, and there is the same or more
                                // room on the side where it is now, leave it
                                // there (right after pinned)
                                columns.move_before(active, pinned);
                            }
                        }
                    }
                }
            }
            // Now, we know pinned is in the right position (it doesn't move)
            adjust_columns(pinned);
            return;
        }

        if (a_x < max.x) {
            // active starts outside on the left
            // set it on the left edge
            active->data()->set_geom_pos(max.x, max.y);
        } else if (std::round(a_x + a_w) > max.x + max.w) {
            // active overflows to the right, move to end of viewport
            active->data()->set_geom_pos(max.x + max.w - a_w, max.y);
        } else {
            // active is inside the viewport
            if (reorder == Reorder::Auto) {
                // The active column should always be completely in the viewport.
                // If any of the windows next to it on its right or left are
                // in the viewport, keep the current position.
                bool keep_current = false;
                if (active->prev() != nullptr) {
                    Column *prev = active->prev()->data();
                    if (prev->get_geom_x() >= max.x && prev->get_geom_x() + prev->get_geom_w() <= max.x + max.w) {
                        keep_current = true;
                    }
                }
                if (!keep_current && active->next() != nullptr) {
                    Column *next = active->next()->data();
                    if (next->get_geom_x() >= max.x && next->get_geom_x() + next->get_geom_w() <= max.x + max.w) {
                        keep_current = true;
                    }
                }
                if (!keep_current) {
                    // If not:
                    // We try to fit the column next to it on the right if it fits
                    // completely, otherwise the one on the left. If none of them fit,
                    // we leave it as it is.
                    if (active->next() != nullptr) {
                        if (a_w + active->next()->data()->get_geom_w() <= max.w) {
                            // set next at the right edge of the viewport
                            active->data()->set_geom_pos(max.x + max.w - a_w - active->next()->data()->get_geom_w(), max.y);
                        } else if (active->prev() != nullptr) {
                            if (active->prev()->data()->get_geom_w() + a_w <= max.w) {
                                // set previous at the left edge of the viewport
                                active->data()->set_geom_pos(max.x + active->prev()->data()->get_geom_w(), max.y);
                            } else {
                                // none of them fit, leave active as it is
                                active->data()->set_geom_pos(a_x, max.y);
                            }
                        } else {
                            // nothing on the left, move active to left edge of viewport
                            active->data()->set_geom_pos(max.x, max.y);
                        }
                    } else if (active->prev() != nullptr) {
                        if (active->prev()->data()->get_geom_w() + a_w <= max.w) {
                            // set previous at the left edge of the viewport
                            active->data()->set_geom_pos(max.x + active->prev()->data()->get_geom_w(), max.y);
                        } else {
                            // it doesn't fit and nothing on the right, move active to right edge of viewport
                            active->data()->set_geom_pos(max.x + max.w - a_w, max.y);
                        }
                    } else {
                        // nothing on the right or left, the window is in a correct position
                        active->data()->set_geom_pos(a_x, max.y);
                    }
                } else {
                    // the window is in a correct position
                    // if the window is first or last, ensure it is at the edge
                    if (active == columns.first()) {
                        active->data()->set_geom_pos(max.x, max.y);
                    } else if (active == columns.last()) {
                        active->data()->set_geom_pos(max.x + max.w - a_w, max.y);
                    } else {
                        active->data()->set_geom_pos(a_x, max.y);
                    }
                }
            } else {  // lazy
                // Try to avoid moving the active column unless it is out of the screen.
                // the window is in a correct position
                active->data()->set_geom_pos(a_x, max.y);
            }
        }

        adjust_columns(active);
    }

private:
    // Adjust all the columns in the row using 'column' as anchor
    void adjust_columns(ListNode<Column *> *column) {
        // Adjust the positions of the columns to the left
        for (auto col = column->prev(), prev = column; col != nullptr; prev = col, col = col->prev()) {
            col->data()->set_geom_pos(prev->data()->get_geom_x() - col->data()->get_geom_w(), max.y);
        }
        // Adjust the positions of the columns to the right
        for (auto col = column->next(), prev = column; col != nullptr; prev = col, col = col->next()) {
            col->data()->set_geom_pos(prev->data()->get_geom_x() + prev->data()->get_geom_w(), max.y);
        }

        // Apply column geometry
        for (auto col = columns.first(); col != nullptr; col = col->next()) {
            // First and last columns need a different gap
            auto gap0 = col == columns.first() ? 0.0 : gap;
            auto gap1 = col == columns.last() ? 0.0 : gap;
            col->data()->recalculate_col_geometry(Vector2D(gap0, gap1), gap);
        }
    }
    // Adjust all the columns in the overview
    void adjust_overview_columns() {
        auto column = columns.first();
        // Should be at the left edge
        column->data()->set_geom_pos(max.x, max.y);
        // Adjust the positions of the columns to the right
        for (auto col = column->next(), prev = column; col != nullptr; prev = col, col = col->next()) {
            col->data()->set_geom_pos(prev->data()->get_geom_x() + prev->data()->get_geom_w(), max.y);
        }

        // Apply column geometry
        for (auto col = columns.first(); col != nullptr; col = col->next()) {
            // First and last columns need a different gap
            auto gap0 = col == columns.first() ? 0.0 : gap;
            auto gap1 = col == columns.last() ? 0.0 : gap;
            col->data()->recalculate_col_geometry_overview(Vector2D(gap0, gap1), gap);
        }
    }

    WORKSPACEID workspace;
    Box full;
    Box max;
    bool overview;
    eFullscreenMode preoverview_fsmode;
    int gap;
    Reorder reorder;
    Mode mode;
    ListNode<Column *> *pinned;
    ListNode<Column *> *active;
    List<Column *> columns;
};


Row *ScrollerLayout::getRowForWorkspace(WORKSPACEID workspace) {
    for (auto row = rows.first(); row != nullptr; row = row->next()) {
        if (row->data()->get_workspace() == workspace)
            return row->data();
    }
    return nullptr;
}

Row *ScrollerLayout::getRowForWindow(PHLWINDOW window) {
    for (auto row = rows.first(); row != nullptr; row = row->next()) {
        if (row->data()->has_window(window))
            return row->data();
    }
    return nullptr;
}

/*
    Called when a window is created (mapped)
    The layout HAS TO set the goal pos and size (anim mgr will use it)
    If !animationinprogress, then the anim mgr will not apply an anim.
*/
void ScrollerLayout::onWindowCreatedTiling(PHLWINDOW window, eDirection)
{
    WORKSPACEID wid = window->workspaceID();
    auto s = getRowForWorkspace(wid);
    if (s == nullptr) {
        s = new Row(window);
        rows.push_back(s);
    }

    // Undo possible modifications from general options.
    window->unsetWindowData(PRIORITY_LAYOUT);
    window->updateWindowData();

    s->add_active_window(window);

    // Check window rules
    for (auto &r: window->m_vMatchedRules) {
        if (r.szRule.starts_with("plugin:scroller:group")) {
            const auto name = r.szRule.substr(r.szRule.find_first_of(' ') + 1);
            s->move_active_window_to_group(name);
        } else if (r.szRule.starts_with("plugin:scroller:alignwindow")) {
            const auto dir = r.szRule.substr(r.szRule.find_first_of(' ') + 1);
            if (dir == "l" || dir == "left") {
                s->align_column(Direction::Left);
            } else if (dir == "r" || dir == "right") {
                s->align_column(Direction::Right);
            } else if (dir == "u" || dir == "up") {
                s->align_column(Direction::Up);
            } else if (dir == "d" || dir == "dn" || dir == "down") {
                s->align_column(Direction::Down);
            } else if (dir == "c" || dir == "centre" || dir == "center") {
                s->align_column(Direction::Center);
            }
        } else if (r.szRule.starts_with("plugin:scroller:marksadd")) {
            const auto mark_name = r.szRule.substr(r.szRule.find_first_of(' ') + 1);
            marks.add(window, mark_name);
        }
    }
}

/*
    Called when a window is removed (unmapped)
*/
void ScrollerLayout::onWindowRemovedTiling(PHLWINDOW window)
{
    marks.remove(window);

    auto s = getRowForWindow(window);
    if (s == nullptr) {
        s = getRowForWorkspace(window->workspaceID());
        if (s != nullptr)
            force_focus_to_window(s->get_active_window());
        return;
    }
    if (!s->remove_window(window)) {
        // It was the last one, remove the row
        for (auto row = rows.first(); row != nullptr; row = row->next()) {
            if (row->data() == s) {
                rows.erase(row);
                delete row->data();
                return;
            }
        }
    }
}

/*
    Called when a floating window is removed (unmapped)
*/
void ScrollerLayout::onWindowRemovedFloating(PHLWINDOW window)
{
    WORKSPACEID workspace_id = window->m_pMonitor->activeSpecialWorkspaceID();
    if (!workspace_id) {
        workspace_id = window->workspaceID();
    }
    auto s = getRowForWorkspace(workspace_id);
    if (s != nullptr)
        force_focus_to_window(s->get_active_window());
}

/*
    Internal: called when window focus changes
*/
void ScrollerLayout::onWindowFocusChange(PHLWINDOW window)
{
    if (window == nullptr) { // no window has focus
        return;
    }

    auto s = getRowForWindow(window);
    if (s == nullptr) {
        return;
    }
    s->focus_window(window);
}

/*
    Return tiled status
*/
bool ScrollerLayout::isWindowTiled(PHLWINDOW window)
{
    return getRowForWindow(window) != nullptr;
}

/*
    Called when the monitor requires a layout recalculation
    this usually means reserved area changes
*/
void ScrollerLayout::recalculateMonitor(const MONITORID &monitor_id)
{
    const auto PMONITOR = g_pCompositor->getMonitorFromID(monitor_id);
    if (!PMONITOR)
        return;

    g_pHyprRenderer->damageMonitor(PMONITOR);

    WORKSPACEID specialID = PMONITOR->activeSpecialWorkspaceID();
    if (specialID) {
        auto sw = getRowForWorkspace(specialID);
        if (sw == nullptr) {
            return;
        }
        Box max = sw->update_sizes(PMONITOR);
        auto PWORKSPACESPECIAL = PMONITOR->activeSpecialWorkspace;
        if (PWORKSPACESPECIAL->m_bHasFullscreenWindow) {
            sw->set_fullscreen_mode_windows(PWORKSPACESPECIAL->m_efFullscreenMode);
        } else {
            sw->update_windows(max);
        }
    }

    auto PWORKSPACE = PMONITOR->activeWorkspace;
    if (!PWORKSPACE)
        return;

    auto s = getRowForWorkspace(PWORKSPACE->m_iID);
    if (s == nullptr)
        return;

    Box max = s->update_sizes(PMONITOR);
    if (PWORKSPACE->m_bHasFullscreenWindow) {
        s->set_fullscreen_mode_windows(PWORKSPACE->m_efFullscreenMode);
    } else {
        s->update_windows(max);
    }
}

/*
    Called when the compositor requests a window
    to be recalculated, e.g. when pseudo is toggled.
*/
void ScrollerLayout::recalculateWindow(PHLWINDOW window)
{
    auto s = getRowForWindow(window);
    if (s == nullptr)
        return;

    s->recalculate_row_geometry();
}

/*
    Called when a user requests a resize of the current window by a vec
    Vector2D holds pixel values
    Optional pWindow for a specific window
*/
void ScrollerLayout::resizeActiveWindow(const Vector2D &delta,
                                        eRectCorner corner, PHLWINDOW window)
{
    const auto PWINDOW = window ? window : g_pCompositor->m_pLastWindow.lock();
    auto s = getRowForWindow(PWINDOW);
    if (s == nullptr) {
        // Window is not tiled
        PWINDOW->m_vRealSize = Vector2D(std::max((PWINDOW->m_vRealSize.goal() + delta).x, 20.0), std::max((PWINDOW->m_vRealSize.goal() + delta).y, 20.0));
        PWINDOW->updateWindowDecos();
        return;
    }

    s->resize_active_window(delta);
}

/*
   Called when a window / the user requests to toggle the fullscreen state of a
   window. The layout sets all the fullscreen flags. It can either accept or
   ignore.
*/
void ScrollerLayout::fullscreenRequestForWindow(PHLWINDOW window,
                                                const eFullscreenMode CURRENT_EFFECTIVE_MODE,
                                                const eFullscreenMode EFFECTIVE_MODE)
{
    auto s = getRowForWindow(window);

    if (s == nullptr) {
        // save position and size if floating
        if (window->m_bIsFloating && CURRENT_EFFECTIVE_MODE == FSMODE_NONE) {
            window->m_vLastFloatingSize     = window->m_vRealSize.goal();
            window->m_vLastFloatingPosition = window->m_vRealPosition.goal();
            window->m_vPosition             = window->m_vRealPosition.goal();
            window->m_vSize                 = window->m_vRealSize.goal();
        }
        if (EFFECTIVE_MODE == FSMODE_NONE) {
            // window is not tiled
            if (window->m_bIsFloating) {
                // get back its' dimensions from position and size
                window->m_vRealPosition = window->m_vLastFloatingPosition;
                window->m_vRealSize     = window->m_vLastFloatingSize;

                window->unsetWindowData(PRIORITY_LAYOUT);
                window->updateWindowData();
            }
        } else {
            // apply new pos and size being monitors' box
            const auto PMONITOR   = window->m_pMonitor.lock();
            if (EFFECTIVE_MODE == FSMODE_FULLSCREEN) {
                window->m_vRealPosition = PMONITOR->vecPosition;
                window->m_vRealSize     = PMONITOR->vecSize;
            } else {
                Box box = { PMONITOR->vecPosition + PMONITOR->vecReservedTopLeft,
                            PMONITOR->vecSize - PMONITOR->vecReservedTopLeft - PMONITOR->vecReservedBottomRight};
                window->m_vRealPosition = Vector2D(box.x, box.y);
                window->m_vRealSize = Vector2D(box.w, box.h);
            }
        }
    } else {
        if (EFFECTIVE_MODE == CURRENT_EFFECTIVE_MODE)
            return;
        s->set_fullscreen_mode(CURRENT_EFFECTIVE_MODE, EFFECTIVE_MODE);
    }
    g_pCompositor->changeWindowZOrder(window, true);
}

/*
    Called when a dispatcher requests a custom message
    The layout is free to ignore.
    std::any is the reply. Can be empty.
*/
std::any ScrollerLayout::layoutMessage(SLayoutMessageHeader header, std::string content)
{
    return "";
}

/*
    Required to be handled, but may return just SWindowRenderLayoutHints()
    Called when the renderer requests any special draw flags for
    a specific window, e.g. border color for groups.
*/
SWindowRenderLayoutHints ScrollerLayout::requestRenderHints(PHLWINDOW)
{
    return {};
}

/*
    Called when the user requests two windows to be swapped places.
    The layout is free to ignore.
*/
void ScrollerLayout::switchWindows(PHLWINDOW, PHLWINDOW)
{
}

/*
    Called when the user requests a window move in a direction.
    The layout is free to ignore.
*/
void ScrollerLayout::moveWindowTo(PHLWINDOW window, const std::string &direction, bool silent)
{
    auto s = getRowForWindow(window);
    if (s == nullptr) {
        return;
    } else if (!(s->is_active(window))) {
        // cannot move non active window?
        return;
    }

    switch (direction.at(0)) {
        case 'l': s->move_active_column(Direction::Left); break;
        case 'r': s->move_active_column(Direction::Right); break;
        case 'u': s->move_active_column(Direction::Up); break;
        case 'd': s->move_active_column(Direction::Down); break;
        default: break;
    }

    // "silent" requires to keep focus in the neighborhood of the moved window
    // before it moved. I ignore it for now.
}

/*
    Called when the user requests to change the splitratio by or to X
    on a window
*/
void ScrollerLayout::alterSplitRatio(PHLWINDOW, float, bool)
{
}

/*
    Called when something wants the current layout's name
*/
std::string ScrollerLayout::getLayoutName()
{
    return "scroller";
}

/*
    Called for getting the next candidate for a focus
*/
PHLWINDOW ScrollerLayout::getNextWindowCandidate(PHLWINDOW old_window)
{
    // This is called when a windows in unmapped. This means the window
    // has also been removed from the layout. In that case, returning the
    // new active window is the correct thing.
    WORKSPACEID workspace_id = g_pCompositor->m_pLastMonitor->activeSpecialWorkspaceID();
    if (!workspace_id) {
        workspace_id = g_pCompositor->m_pLastMonitor->activeSpecialWorkspaceID();
    }
    auto s = getRowForWorkspace(workspace_id);
    if (s == nullptr)
        return nullptr;
    else
        return s->get_active_window();
}

/*
    Called for replacing any data a layout has for a new window
*/
void ScrollerLayout::replaceWindowDataWith(PHLWINDOW from, PHLWINDOW to)
{
}

void ScrollerLayout::onEnable() {
    // Hijack Hyprland's default dispatchers
    orig_moveFocusTo = g_pKeybindManager->m_mDispatchers["movefocus"];
    orig_moveActiveTo = g_pKeybindManager->m_mDispatchers["movewindow"];
    g_pKeybindManager->m_mDispatchers["movefocus"] = this_moveFocusTo;
    g_pKeybindManager->m_mDispatchers["movewindow"] = this_moveActiveTo;

    enabled = true;
    marks.reset();
    for (auto& window : g_pCompositor->m_vWindows) {
        if (window->m_bIsFloating || !window->m_bIsMapped || window->isHidden())
            continue;

        onWindowCreatedTiling(window);
        recalculateMonitor(window->monitorID());
    }
}

void ScrollerLayout::onDisable() {
    // Restore Hyprland's default dispatchers
    g_pKeybindManager->m_mDispatchers["movefocus"] = orig_moveFocusTo;
    g_pKeybindManager->m_mDispatchers["movewindow"] = orig_moveActiveTo;

    enabled = false;
    for (auto row = rows.first(); row != nullptr; row = row->next()) {
        delete row->data();
    }
    rows.clear();
    marks.reset();
}

/*
    Called to predict the size of a newly opened window to send it a configure.
    Return 0,0 if unpredictable
*/
Vector2D ScrollerLayout::predictSizeForNewWindowTiled() {
    if (!g_pCompositor->m_pLastMonitor)
        return {};

    WORKSPACEID workspace_id = g_pCompositor->m_pLastMonitor->activeWorkspaceID();
    auto s = getRowForWorkspace(workspace_id);
    if (s == nullptr) {
        Vector2D size =g_pCompositor->m_pLastMonitor->vecSize;
        size.x *= 0.5;
        return size;
    }

    return s->predict_window_size();
}

void ScrollerLayout::cycle_window_size(WORKSPACEID workspace, int step)
{
    auto s = getRowForWorkspace(workspace);
    if (s == nullptr) {
        return;
    }

    s->resize_active_column(step);
}

void ScrollerLayout::move_focus(WORKSPACEID workspace, Direction direction)
{
    static auto* const *focus_wrap = (Hyprlang::INT* const *)HyprlandAPI::getConfigValue(PHANDLE, "plugin:scroller:focus_wrap")->getDataStaticPtr();
    auto s = getRowForWorkspace(workspace);
    if (s == nullptr) {
        // if workspace is empty, use the deault movefocus, which now
        // is "move to another monitor" (pass the direction)
        switch (direction) {
            case Direction::Left:
                orig_moveFocusTo("l");
                break;
            case Direction::Right:
                orig_moveFocusTo("r");
                break;
            case Direction::Up:
                orig_moveFocusTo("u");
                break;
            case Direction::Down:
                orig_moveFocusTo("d");
                break;
            default:
                break;
        }
        return;
    }

    if (!s->move_focus(direction, **focus_wrap == 0 ? false : true)) {
        // changed monitor
        s = getRowForWorkspace(g_pCompositor->m_pLastMonitor->activeWorkspaceID());
        if (s == nullptr) {
            // monitor is empty
            return;
        }
    }
}

void ScrollerLayout::move_window(WORKSPACEID workspace, Direction direction) {
    auto s = getRowForWorkspace(workspace);
    if (s == nullptr) {
        return;
    }

    s->move_active_column(direction);
}

void ScrollerLayout::align_window(WORKSPACEID workspace, Direction direction) {
    auto s = getRowForWorkspace(workspace);
    if (s == nullptr) {
        return;
    }

    s->align_column(direction);
}

void ScrollerLayout::admit_window_left(WORKSPACEID workspace) {
    auto s = getRowForWorkspace(workspace);
    if (s == nullptr) {
        return;
    }
    s->admit_window_left();
}

void ScrollerLayout::expel_window_right(WORKSPACEID workspace) {
    auto s = getRowForWorkspace(workspace);
    if (s == nullptr) {
        return;
    }
    s->expel_window_right();
}

void ScrollerLayout::set_mode(WORKSPACEID workspace, Mode mode) {
    auto s = getRowForWorkspace(workspace);
    if (s == nullptr) {
        return;
    }
    s->set_mode(mode);
}

void ScrollerLayout::fit_size(WORKSPACEID workspace, FitSize fitsize) {
    auto s = getRowForWorkspace(workspace);
    if (s == nullptr) {
        return;
    }
    s->fit_size(fitsize);
}

void ScrollerLayout::toggle_overview(WORKSPACEID workspace) {
    auto s = getRowForWorkspace(workspace);
    if (s == nullptr) {
        return;
    }
    s->toggle_overview();
}

static WORKSPACEID get_workspace_id() {
    WORKSPACEID workspace_id;
    if (g_pCompositor->m_pLastMonitor->activeSpecialWorkspaceID()) {
        workspace_id = g_pCompositor->m_pLastMonitor->activeSpecialWorkspaceID();
    } else {
        workspace_id = g_pCompositor->m_pLastMonitor->activeWorkspaceID();
    }
    if (workspace_id == WORKSPACE_INVALID)
        return -1;
    if (g_pCompositor->getWorkspaceByID(workspace_id) == nullptr)
        return -1;

    return workspace_id;
}

PHLWINDOW ScrollerLayout::getActiveWindow(WORKSPACEID workspace) {
    return getRowForWorkspace(workspace)->get_active_window();
}

void ScrollerLayout::marks_add(const std::string &name) {
    PHLWINDOW window = getActiveWindow(get_workspace_id());
    marks.add(window, name);
}

void ScrollerLayout::marks_delete(const std::string &name) {
    marks.del(name);
}

void ScrollerLayout::marks_visit(const std::string &name) {
    PHLWINDOW from = getActiveWindow(get_workspace_id());
    PHLWINDOW to = marks.visit(name);
    if (to != nullptr) {
        switch_to_window(from, to);
    }
}

void ScrollerLayout::marks_reset() {
    marks.reset();
}

void ScrollerLayout::pin(WORKSPACEID workspace) {
    auto s = getRowForWorkspace(workspace);
    if (s == nullptr) {
        return;
    }

    s->pin();
}

void ScrollerLayout::unpin(WORKSPACEID workspace) {
    auto s = getRowForWorkspace(workspace);
    if (s == nullptr) {
        return;
    }

    s->unpin();
}

void ScrollerLayout::post_event(WORKSPACEID workspace, const std::string &event) {
    auto s = getRowForWorkspace(workspace);
    if (s == nullptr) {
        return;
    }

    s->post_event(event);
}

void ScrollerLayout::swipe_begin(IPointer::SSwipeBeginEvent swipe_event) {
    WORKSPACEID wid = get_workspace_id();
    if (wid == -1) {
        return;
    }

    swipe_active = false;
    swipe_direction = Direction::Begin;
}

void ScrollerLayout::swipe_update(SCallbackInfo &info, IPointer::SSwipeUpdateEvent swipe_event) {
    WORKSPACEID wid = get_workspace_id();
    if (wid == -1) {
        return;
    }

    auto s = getRowForWorkspace(wid);

    static auto *const *NATURAL = (Hyprlang::INT *const *)HyprlandAPI::getConfigValue(PHANDLE, "input:touchpad:natural_scroll")->getDataStaticPtr();
    static auto *const *SENABLE = (Hyprlang::INT *const *)HyprlandAPI::getConfigValue(PHANDLE, "plugin:scroller:gesture_scroll_enable")->getDataStaticPtr();
    static auto *const *SFINGERS = (Hyprlang::INT *const *)HyprlandAPI::getConfigValue(PHANDLE, "plugin:scroller:gesture_scroll_fingers")->getDataStaticPtr();
    static auto *const *SDISTANCE = (Hyprlang::INT *const *)HyprlandAPI::getConfigValue(PHANDLE, "plugin:scroller:gesture_scroll_distance")->getDataStaticPtr();
    static auto *const *OENABLE = (Hyprlang::INT *const *)HyprlandAPI::getConfigValue(PHANDLE, "plugin:scroller:gesture_overview_enable")->getDataStaticPtr();
    static auto *const *OFINGERS = (Hyprlang::INT *const *)HyprlandAPI::getConfigValue(PHANDLE, "plugin:scroller:gesture_overview_fingers")->getDataStaticPtr();
    static auto *const *ODISTANCE = (Hyprlang::INT *const *)HyprlandAPI::getConfigValue(PHANDLE, "plugin:scroller:gesture_overview_distance")->getDataStaticPtr();

    if (!(**SENABLE && swipe_event.fingers == **SFINGERS) &&
        !(**OENABLE && swipe_event.fingers == **OFINGERS)) {
        return;
    }

    info.cancelled = true;
    Vector2D delta = swipe_event.delta;
    delta *= **NATURAL ? -1.0 : 1.0;
    if (!swipe_active) {
        gesture_delta = Vector2D(0.0, 0.0);
    }
    gesture_delta += delta;

    if (**SENABLE && swipe_event.fingers == **SFINGERS) {
        if (s == nullptr)
            return;
        int steps_x = gesture_delta.x / **SDISTANCE;
        int steps_y = gesture_delta.y / **SDISTANCE;
        gesture_delta.x -= steps_x * **SDISTANCE;
        gesture_delta.y -= steps_y * **SDISTANCE;
        Direction dir;
        int steps;
        if (std::abs(steps_x) > std::abs(steps_y)) {
            dir = steps_x < 0 ? Direction::Left : Direction::Right;
            steps = std::abs(steps_x);
        } else {
            dir = steps_y < 0 ? Direction::Up : Direction::Down;
            steps = std::abs(steps_y);
        }
        if (swipe_direction != dir) {
            gesture_delta = Vector2D(0.0, 0.0);
            swipe_direction = dir;
        }
        for (int i = 0; i < steps; ++i) {
            switch (dir) {
            case Direction::Left:
            case Direction::Right:
            case Direction::Up:
            case Direction::Down:
                s->move_focus(dir, false);
                // To avoid going to the next monitor, stays in s
                force_focus_to_window(s->get_active_window());
                break;
            default:
                break;
            }
        }
        s->recalculate_row_geometry();
    } else if (**OENABLE && swipe_event.fingers == **OFINGERS) {
        // Only accept the first update: one swipe, one trigger.
        if (swipe_active)
            return;
        // Undo natural
        const double distance = (**NATURAL ? -1.0 : 1.0) * **ODISTANCE;
        if (gesture_delta.y <= -distance) {
            if (s == nullptr)
                return;
            if (!s->is_overview()) {
                s->toggle_overview();
            }
        } else if (gesture_delta.y >= distance) {
            if (s == nullptr)
                return;
            if (s->is_overview()) {
                s->toggle_overview();
            }
        } else if (gesture_delta.x <= -distance) {
            g_pKeybindManager->m_mDispatchers["workspace"]("-1");
        } else if (gesture_delta.x >= distance) {
            g_pKeybindManager->m_mDispatchers["workspace"]("+1");
        }
    }
    swipe_active = true;
}

void ScrollerLayout::swipe_end(SCallbackInfo &info,
                               IPointer::SSwipeEndEvent swipe_event) {
    WORKSPACEID wid = get_workspace_id();
    if (wid == -1) {
        return;
    }

    swipe_active = false;
    gesture_delta = Vector2D(0.0, 0.0);
    swipe_direction = Direction::Begin;
    info.cancelled = true;
}
