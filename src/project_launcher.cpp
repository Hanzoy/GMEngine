#include "project_launcher.hpp"
#include "project.hpp"
#include <windows.h>
#include <commdlg.h>
#include <commctrl.h>
#include <shellapi.h>
#ifdef _MSC_VER
#pragma comment(                                                                                             \
    linker,                                                                                                  \
    "\"/manifestdependency:type='win32' name='Microsoft.Windows.Common-Controls' version='6.0.0.0' processorArchitecture='*' publicKeyToken='6595b64144ccf1df' language='*'\"")
#endif

namespace gm {
namespace {
std::wstring wideText(const std::string &s) {
    auto u = std::u8string(s.begin(), s.end());
    return std::filesystem::path(u).wstring();
}
std::string utfText(const std::filesystem::path &p) {
    auto u = p.u8string();
    return {u.begin(), u.end()};
}
struct Launcher {
    HWND window = nullptr, list = nullptr, status = nullptr;
    HFONT font = nullptr, titleFont = nullptr;
    Json recent;
    std::filesystem::path selected;
    HWND control(const wchar_t *kind, const wchar_t *label, DWORD style, int x, int y, int w, int h,
                 int id = 0) {
        auto child =
            CreateWindowExW(0, kind, label, WS_CHILD | WS_VISIBLE | style, x, y, w, h, window,
                            reinterpret_cast<HMENU>(intptr_t(id)), GetModuleHandle(nullptr), nullptr);
        SendMessageW(child, WM_SETFONT, reinterpret_cast<WPARAM>(font), TRUE);
        return child;
    }
    void refresh() {
        recent = recentProjects();
        SendMessageW(list, LB_RESETCONTENT, 0, 0);
        for (const auto &item : recent) {
            auto label = wideText(item.at("name").get<std::string>()) + L"    ·    " +
                         wideText(item.at("path").get<std::string>());
            if (!item.value("exists", false))
                label = L"[找不到] " + label;
            SendMessageW(list, LB_ADDSTRING, 0, reinterpret_cast<LPARAM>(label.c_str()));
        }
        if (!recent.empty())
            SendMessageW(list, LB_SETCURSEL, 0, 0);
    }
    void open(std::filesystem::path path) {
        auto project = readProject(path);
        rememberProject(project);
        selected = project.manifest;
        DestroyWindow(window);
    }
    void command(int id) {
        if (id == 1) {
            auto row = SendMessageW(list, LB_GETCURSEL, 0, 0);
            if (row != LB_ERR)
                open(std::filesystem::path(wideText(recent.at(size_t(row)).at("path").get<std::string>())));
        }
        if (id == 2 || id == 3) {
            wchar_t path[32768]{};
            OPENFILENAMEW dialog{sizeof(dialog)};
            dialog.hwndOwner = window;
            dialog.lpstrFile = path;
            dialog.nMaxFile = 32768;
            dialog.lpstrFilter = id == 2 ? L"GM Engine 项目\0*.gmproject\0\0" : L"新项目文件夹\0*.*\0\0";
            dialog.lpstrTitle = id == 2 ? L"打开项目" : L"新建项目：输入新的文件夹名称";
            dialog.Flags = OFN_NOCHANGEDIR | OFN_PATHMUSTEXIST | (id == 2 ? OFN_FILEMUSTEXIST : 0);
            if (!(id == 2 ? GetOpenFileNameW(&dialog) : GetSaveFileNameW(&dialog)))
                return;
            std::filesystem::path chosen(path);
            if (id == 3) {
                SetWindowTextW(status, L"正在创建项目…");
                createProject(chosen, utfText(chosen.filename()));
                chosen /= L"game.gmproject";
            }
            open(chosen);
        }
        if (id == 4) {
            auto row = SendMessageW(list, LB_GETCURSEL, 0, 0);
            if (row != LB_ERR) {
                forgetProject(
                    std::filesystem::path(wideText(recent.at(size_t(row)).at("path").get<std::string>())));
                refresh();
            }
        }
        if (id == 5) {
            selected = engineRoot() / "editor/editor.ui.json";
            DestroyWindow(window);
        }
    }
};
LRESULT CALLBACK procedure(HWND window, UINT message, WPARAM wp, LPARAM lp) {
    auto *app = reinterpret_cast<Launcher *>(GetWindowLongPtrW(window, GWLP_USERDATA));
    if (message == WM_NCCREATE) {
        app = static_cast<Launcher *>(reinterpret_cast<CREATESTRUCTW *>(lp)->lpCreateParams);
        SetWindowLongPtrW(window, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(app));
        app->window = window;
    }
    try {
        if (message == WM_CTLCOLORSTATIC) {
            SetBkMode(reinterpret_cast<HDC>(wp), TRANSPARENT);
            return reinterpret_cast<LRESULT>(GetSysColorBrush(COLOR_WINDOW));
        }
        if (app && message == WM_COMMAND) {
            int id = LOWORD(wp);
            if (id == 10) {
                if (HIWORD(wp) == LBN_DBLCLK)
                    app->command(1);
            } else
                app->command(id);
            return 0;
        }
        if (message == WM_CLOSE) {
            DestroyWindow(window);
            return 0;
        }
    } catch (const std::exception &e) {
        MessageBoxW(window, wideText(e.what()).c_str(), L"无法打开项目", MB_OK | MB_ICONERROR);
        if (app)
            SetWindowTextW(app->status, L"项目内容未被删除。请检查路径或构建工具链。");
    }
    return DefWindowProcW(window, message, wp, lp);
}
} // namespace
std::filesystem::path chooseProject() {
    Launcher app;
    auto instance = GetModuleHandle(nullptr);
    WNDCLASSW cls{};
    cls.lpfnWndProc = procedure;
    cls.hInstance = instance;
    cls.lpszClassName = L"GMEngineProjectLauncher";
    cls.hbrBackground = reinterpret_cast<HBRUSH>(COLOR_WINDOW + 1);
    cls.hCursor = LoadCursor(nullptr, IDC_ARROW);
    cls.hIcon = LoadIcon(nullptr, IDI_APPLICATION);
    RegisterClassW(&cls);
    RECT bounds{0, 0, 920, 570};
    AdjustWindowRect(&bounds, WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX, FALSE);
    auto window = CreateWindowExW(0, cls.lpszClassName, L"GM Engine — 项目",
                                  WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX, CW_USEDEFAULT,
                                  CW_USEDEFAULT, bounds.right - bounds.left, bounds.bottom - bounds.top,
                                  nullptr, nullptr, instance, &app);
    if (!window)
        throw std::runtime_error("Cannot create project launcher");
    app.font = CreateFontW(-17, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE, DEFAULT_CHARSET, 0, 0,
                           CLEARTYPE_QUALITY, 0, L"Microsoft YaHei UI");
    app.titleFont = CreateFontW(-30, 0, 0, 0, FW_SEMIBOLD, FALSE, FALSE, FALSE, DEFAULT_CHARSET, 0, 0,
                                CLEARTYPE_QUALITY, 0, L"Segoe UI");
    auto title = app.control(L"STATIC", L"GM Engine", 0, 32, 24, 700, 42);
    SendMessageW(title, WM_SETFONT, reinterpret_cast<WPARAM>(app.titleFont), TRUE);
    app.control(L"STATIC", L"选择一个项目，开始编辑场景与游戏。", 0, 34, 77, 800, 28);
    app.control(L"BUTTON", L"打开项目…", WS_TABSTOP | BS_PUSHBUTTON, 32, 122, 150, 38, 2);
    app.control(L"BUTTON", L"新建项目…", WS_TABSTOP | BS_PUSHBUTTON, 194, 122, 150, 38, 3);
    app.control(L"STATIC", L"最近项目", 0, 32, 185, 300, 28);
    app.list = app.control(
        L"LISTBOX", L"", WS_TABSTOP | WS_BORDER | WS_VSCROLL | WS_HSCROLL | LBS_NOTIFY | LBS_NOINTEGRALHEIGHT,
        32, 218, 856, 238, 10);
    SendMessageW(app.list, LB_SETHORIZONTALEXTENT, 1800, 0);
    app.control(L"BUTTON", L"打开所选项目", WS_TABSTOP | BS_DEFPUSHBUTTON, 32, 471, 180, 36, 1);
    app.control(L"BUTTON", L"从列表移除", WS_TABSTOP | BS_PUSHBUTTON, 224, 471, 145, 36, 4);
    app.control(L"BUTTON", L"编辑器界面工程", WS_TABSTOP | BS_PUSHBUTTON, 698, 471, 190, 36, 5);
    app.status = app.control(
        L"STATIC", L"游戏项目保存在你选择的位置。首次编译 C++ 项目需要 CMake 与 Visual Studio C++ 工具。", 0,
        32, 529, 856, 28);
    app.refresh();
    ShowWindow(window, SW_SHOW);
    UpdateWindow(window);
    SetFocus(app.list);
    MSG msg{};
    while (IsWindow(window) && GetMessageW(&msg, nullptr, 0, 0) > 0) {
        if (msg.message == WM_KEYDOWN && msg.wParam == VK_ESCAPE) {
            DestroyWindow(window);
            continue;
        }
        if (!IsDialogMessageW(window, &msg)) {
            TranslateMessage(&msg);
            DispatchMessageW(&msg);
        }
    }
    DeleteObject(app.font);
    DeleteObject(app.titleFont);
    return app.selected;
}
} // namespace gm
