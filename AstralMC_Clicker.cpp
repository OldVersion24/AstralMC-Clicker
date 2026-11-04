// AutoClicker para Minecraft Java - Win32 nativo, un solo archivo.
// Compilar con MSYS2 UCRT64:
// g++ -std=c++17 -O2 -s -municode -mwindows -static -static-libgcc -static-libstdc++ "AstralMC_Clicker_1.cpp" -o AstralClicker.exe -lcomctl32 -lwinmm -luxtheme -lgdiplus -luser32 -lgdi32
// Compilar con Developer Command Prompt for Visual Studio:
// cl /EHsc /O2 /MT /std:c++17 /utf-8 AstralMC_Clicker_1.cpp comctl32.lib winmm.lib user32.lib gdi32.lib

#ifndef UNICODE
#define UNICODE
#endif
#ifndef _UNICODE
#define _UNICODE
#endif
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <objidl.h>  // Define PROPID before including GDI+ headers
#include <mmsystem.h>
#include <commctrl.h>
#include <atomic>
#include <chrono>
#include <thread>
#include <random>
#include <algorithm>
#include <deque>
#include <cmath>
#include <cstdio>
#include <cwchar>
#include <iterator>
#include <uxtheme.h>
#include <windowsx.h>
using std::min;   // gdiplus.h necesita min/max (NOMINMAX los quita)
using std::max;
#include <gdiplus.h>
namespace Gp = Gdiplus;

#ifndef CREATE_WAITABLE_TIMER_HIGH_RESOLUTION
#define CREATE_WAITABLE_TIMER_HIGH_RESOLUTION 0x00000002
#endif
#ifndef PROCESS_POWER_THROTTLING_IGNORE_TIMER_RESOLUTION
#define PROCESS_POWER_THROTTLING_IGNORE_TIMER_RESOLUTION 0x4
#endif

#pragma comment(lib, "comctl32.lib")
#pragma comment(lib, "winmm.lib")
#pragma comment(lib, "user32.lib")
#pragma comment(lib, "gdi32.lib")
#pragma comment(lib, "uxtheme.lib")
#pragma comment(lib, "gdiplus.lib")
#pragma comment(linker, "/SUBSYSTEM:WINDOWS")

enum { ID_BIND = 1, ID_CPS, ID_CPSVAL, ID_STATUS, ID_NOTE };

struct SliderData { int minimum; int maximum; int value; bool dragging; };
SliderData g_cpsData{10, 160, 155, false};   // 1.0 - 16.0 CPS (defecto 15.5)

// Limites impuestos por los owners
constexpr double kMaxCps = 16.0;               // CPS instantaneos nunca por encima de 16
constexpr size_t kMaxClicksPerSecond = 16;     // y como maximo 16 clics en cualquier ventana de 1 s

std::atomic<bool> g_enabled{ false };
std::atomic<bool> g_binding{ false };
std::atomic<bool> g_running{ true };
std::atomic<bool> g_lmbHeld{ false };
std::atomic<bool> g_pressEval{ false };    // hay un clic nuevo por evaluar
std::atomic<bool> g_pressInMenu{ false };  // ese clic empezo dentro de un menu
std::atomic<int>  g_cpsX10{ 155 };
std::atomic<int>  g_key{ 0 };

HWND g_hwnd = nullptr, g_btnBind = nullptr, g_lblStatus = nullptr;
HWND g_lblCps = nullptr, g_sldCps = nullptr;
HHOOK g_mouseHook = nullptr;
HBRUSH g_darkBrush = nullptr;
constexpr COLORREF COL_BG = RGB(20, 21, 27);
constexpr COLORREF COL_PANEL = RGB(29, 30, 39);
constexpr COLORREF COL_PURPLE = RGB(145, 91, 220);
constexpr COLORREF COL_PURPLE_LIGHT = RGB(190, 150, 255);
constexpr COLORREF COL_TEXT = RGB(235, 233, 242);
constexpr COLORREF COL_MUTED = RGB(157, 157, 177);
constexpr COLORREF COL_PURPLE_DARK = RGB(83, 48, 125);
constexpr COLORREF COL_TRACK = RGB(44, 42, 53);
constexpr COLORREF COL_ORANGE = RGB(224, 157, 77);

// ---- Helpers de dibujo suave (GDI+) ----
inline Gp::Color GdipColor(COLORREF c, BYTE a = 255) {
    return Gp::Color(a, GetRValue(c), GetGValue(c), GetBValue(c));
}
void RoundRectPath(Gp::GraphicsPath& p, const Gp::RectF& r, float rad) {
    const float d = rad * 2.0f;
    p.AddArc(r.X, r.Y, d, d, 180.0f, 90.0f);
    p.AddArc(r.X + r.Width - d, r.Y, d, d, 270.0f, 90.0f);
    p.AddArc(r.X + r.Width - d, r.Y + r.Height - d, d, d, 0.0f, 90.0f);
    p.AddArc(r.X, r.Y + r.Height - d, d, d, 90.0f, 90.0f);
    p.CloseFigure();
}
HFONT MakeFont(int px, int weight) {
    return CreateFontW(-px, 0, 0, 0, weight, FALSE, FALSE, FALSE, DEFAULT_CHARSET,
        OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
        DEFAULT_PITCH | FF_SWISS, L"Segoe UI");
}

// Solo cuenta eventos fisicos: ignora los clics sinteticos que genera el programa.
LRESULT CALLBACK MouseProc(int n, WPARAM w, LPARAM l) {
    if (n == HC_ACTION) {
        const auto* ms = reinterpret_cast<const MSLLHOOKSTRUCT*>(l);
        if (!(ms->flags & LLMHF_INJECTED)) {
            if (w == WM_LBUTTONDOWN || w == WM_LBUTTONDBLCLK) {
                g_pressEval.store(true);   // primero esto...
                g_lmbHeld.store(true);     // ...y luego esto (el orden importa)
            } else if (w == WM_LBUTTONUP) {
                g_lmbHeld.store(false);
                g_pressEval.store(false);
                g_pressInMenu.store(false);
            }
        }
    }
    return CallNextHookEx(g_mouseHook, n, w, l);
}

// Reconoce Minecraft por el titulo o por el proceso javaw.exe.
bool MinecraftFocused() {
    HWND fg = GetForegroundWindow();
    if (!fg) return false;

    wchar_t title[512]{};
    GetWindowTextW(fg, title, static_cast<int>(std::size(title)));
    if (wcsstr(title, L"Minecraft") != nullptr) return true;

    DWORD pid = 0;
    GetWindowThreadProcessId(fg, &pid);
    if (!pid) return false;
    HANDLE process = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid);
    if (!process) return false;

    wchar_t path[1024]{};
    DWORD size = static_cast<DWORD>(std::size(path));
    bool isJava = false;
    if (QueryFullProcessImageNameW(process, 0, path, &size)) {
        const wchar_t* base = wcsrchr(path, L'\\');
        base = base ? base + 1 : path;
        isJava = (_wcsicmp(base, L"javaw.exe") == 0 || _wcsicmp(base, L"java.exe") == 0);
    }
    CloseHandle(process);
    return isJava;
}

using Clock = std::chrono::steady_clock;

long long NowMs() {
    return std::chrono::duration_cast<std::chrono::milliseconds>(
        Clock::now().time_since_epoch()).count();
}

// Temporizador de alta resolucion. No depende de timeBeginPeriod, que Windows 11
// ignora cuando la ventana del programa queda tapada por Minecraft.
class PreciseTimer {
public:
    PreciseTimer() {
        h_ = CreateWaitableTimerExW(nullptr, nullptr, CREATE_WAITABLE_TIMER_HIGH_RESOLUTION, TIMER_ALL_ACCESS);
        if (!h_) h_ = CreateWaitableTimerExW(nullptr, nullptr, 0, TIMER_ALL_ACCESS);
    }
    ~PreciseTimer() { if (h_) CloseHandle(h_); }
    PreciseTimer(const PreciseTimer&) = delete;
    PreciseTimer& operator=(const PreciseTimer&) = delete;

    // Duerme hasta t. Con spinTail, los ultimos ~1.2 ms se esperan activamente
    // para clavar el instante del clic.
    void SleepUntil(Clock::time_point t, bool spinTail = true) {
        const auto tail = spinTail ? std::chrono::microseconds(1200) : std::chrono::microseconds(0);
        const auto remaining = t - Clock::now();
        if (remaining > tail) {
            const auto wait = std::chrono::duration_cast<std::chrono::microseconds>(remaining - tail);
            LARGE_INTEGER due;
            due.QuadPart = -static_cast<LONGLONG>(wait.count()) * 10; // unidades de 100 ns
            if (h_ && SetWaitableTimer(h_, &due, 0, nullptr, nullptr, FALSE))
                WaitForSingleObject(h_, INFINITE);
            else
                std::this_thread::sleep_for(wait);
        }
        if (spinTail) while (Clock::now() < t) YieldProcessor();
    }
private:
    HANDLE h_ = nullptr;
};

// Aproxima si una GUI esta abierta comprobando el modo de raton de Minecraft.
// Con el raton capturado, Minecraft recoloca el cursor en el centro de la ventana
// en cada frame. Al mover la camara rapido el cursor se aleja del centro entre
// frames, asi que una sola lectura fuera del centro NO significa que haya un menu.
// Por eso un hilo muestrea el cursor cada ~1 ms y solo se considera GUI abierta
// si el cursor lleva mas de GUI_GRACE_MS sin pasar por el centro.
// Tiempo sin pasar por el centro antes de considerar que hay una GUI abierta.
// Mas bajo = el clic se corta antes al abrir el inventario, pero si lo bajas
// demasiado, mover la camara muy rapido puede cortar clics. Ajustable (40-100).
constexpr long long GUI_GRACE_MS = 60;
std::atomic<long long> g_lastInGameMs{ 0 };

// Senal rapida para 1.8.9 (LWJGL 2): jugando, Minecraft pone un cursor transparente
// propio; con una GUI abierta vuelve a un cursor estandar de Windows (flecha, texto,
// mano...). Eso se detecta al instante, sin esperar al periodo de gracia.
// Solo se usa si ya se ha comprobado que en juego NO aparece un cursor estandar
// (g_cursorSignalValid); si en algun PC no funciona, se sigue con el metodo del centro.
std::atomic<bool> g_cursorStd{ false };
std::atomic<bool> g_cursorSignalValid{ false };

bool IsStandardCursor(HCURSOR c) {
    if (!c) return true;   // sin cursor propio: no es el transparente de juego
    static const HCURSOR std_[] = {
        LoadCursorW(nullptr, IDC_ARROW),  LoadCursorW(nullptr, IDC_IBEAM),
        LoadCursorW(nullptr, IDC_HAND),   LoadCursorW(nullptr, IDC_WAIT),
        LoadCursorW(nullptr, IDC_APPSTARTING), LoadCursorW(nullptr, IDC_CROSS),
        LoadCursorW(nullptr, IDC_NO),     LoadCursorW(nullptr, IDC_SIZEALL),
    };
    for (HCURSOR h : std_) if (h && c == h) return true;
    return false;
}

void GuiMonitor() {
    SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_HIGHEST);
    PreciseTimer timer;
    constexpr LONG tolerance = 5;
    bool focused = false;
    auto nextFocusCheck = Clock::now();
    auto tick = Clock::now();
    g_lastInGameMs.store(NowMs());

    while (g_running.load()) {
        const auto now = Clock::now();
        if (now >= nextFocusCheck) {
            focused = MinecraftFocused();
            nextFocusCheck = now + std::chrono::milliseconds(40);
        }

        bool centered = false;
        if (focused) {
            HWND fg = GetForegroundWindow();
            RECT client{};
            if (fg && GetClientRect(fg, &client)) {
                POINT center{ (client.right - client.left) / 2, (client.bottom - client.top) / 2 };
                POINT cursor{};
                if (ClientToScreen(fg, &center) && GetCursorPos(&cursor)) {
                    centered = std::abs(cursor.x - center.x) <= tolerance &&
                               std::abs(cursor.y - center.y) <= tolerance;
                }
            }
        }
        bool stdCursor = false;
        if (focused) {
            CURSORINFO ci{};
            ci.cbSize = sizeof(ci);
            if (GetCursorInfo(&ci)) stdCursor = IsStandardCursor(ci.hCursor);
            // Calibracion: jugando (cursor en el centro) y sin cursor estandar => la senal es fiable.
            if (centered && !stdCursor) g_cursorSignalValid.store(true);
        }
        g_cursorStd.store(stdCursor);

        // Sin Minecraft enfocado no hay GUI que bloquear.
        if (!focused || centered) g_lastInGameMs.store(NowMs());

        tick += std::chrono::milliseconds(1);
        if (tick < Clock::now()) tick = Clock::now() + std::chrono::milliseconds(1);
        timer.SleepUntil(tick, false);
    }
}

bool GuiOpen() {
    if (g_cursorSignalValid.load() && g_cursorStd.load()) return true;   // instantaneo
    return NowMs() - g_lastInGameMs.load() > GUI_GRACE_MS;               // respaldo
}

void ClickOnce(int holdMs, PreciseTimer& timer) {
    INPUT input[2]{};
    input[0].type = INPUT_MOUSE;
    input[0].mi.dwFlags = MOUSEEVENTF_LEFTDOWN;
    input[1].type = INPUT_MOUSE;
    input[1].mi.dwFlags = MOUSEEVENTF_LEFTUP;

    // Enviar down y up como dos eventos separados permite que Windows/Minecraft
    // procese el clic completo. Si falla, el bucle continua y se puede diagnosticar.
    SendInput(1, &input[0], sizeof(INPUT));
    timer.SleepUntil(Clock::now() + std::chrono::milliseconds(holdMs), false);
    SendInput(1, &input[1], sizeof(INPUT));
}

void RefreshUI() {
    wchar_t buf[128]{};
    int vk = g_key.load();
    if (g_binding.load()) {
        wcscpy_s(buf, L"Pulsa una tecla...");
    } else if (!vk) {
        wcscpy_s(buf, L"Ninguna");
    } else {
        UINT sc = MapVirtualKeyW(static_cast<UINT>(vk), MAPVK_VK_TO_VSC);
        LONG keyNameParam = static_cast<LONG>(sc << 16);
        if (!GetKeyNameTextW(keyNameParam, buf, static_cast<int>(std::size(buf))))
            swprintf_s(buf, L"VK %d", vk);
    }
    if (g_btnBind) { SetWindowTextW(g_btnBind, buf); InvalidateRect(g_btnBind, nullptr, TRUE); }
    if (g_lblStatus) {
        const wchar_t* state = g_binding ? L"Estado: elige una tecla" :
            (g_enabled ? L"●  ACTIVADO" : L"●  DESACTIVADO");
        SetWindowTextW(g_lblStatus, state);
    }
}

void RefreshSliders() {
    auto* cpsData = reinterpret_cast<SliderData*>(GetWindowLongPtrW(g_sldCps, GWLP_USERDATA));
    if (!cpsData) return;
    g_cpsX10.store(cpsData->value);
    wchar_t b[32]{};
    swprintf_s(b, L"%.1f", cpsData->value / 10.0);
    if (g_lblCps) SetWindowTextW(g_lblCps, b);
}

void DrawGradient(HDC dc, const RECT& rc, COLORREF left, COLORREF right) {
    const int width = rc.right - rc.left;
    const int height = rc.bottom - rc.top;
    if (width <= 0 || height <= 0) return;
    for (int x = 0; x < width; ++x) {
        const double t = width > 1 ? static_cast<double>(x) / (width - 1) : 0.0;
        BYTE r = static_cast<BYTE>(GetRValue(left) + (GetRValue(right) - GetRValue(left)) * t);
        BYTE g = static_cast<BYTE>(GetGValue(left) + (GetGValue(right) - GetGValue(left)) * t);
        BYTE b = static_cast<BYTE>(GetBValue(left) + (GetBValue(right) - GetBValue(left)) * t);
        HPEN pen = CreatePen(PS_SOLID, 1, RGB(r, g, b));
        HPEN old = static_cast<HPEN>(SelectObject(dc, pen));
        MoveToEx(dc, rc.left + x, rc.top, nullptr);
        LineTo(dc, rc.left + x, rc.bottom);
        SelectObject(dc, old);
        DeleteObject(pen);
    }
}

void UpdateSliderFromPoint(HWND hwnd, int x) {
    auto* data = reinterpret_cast<SliderData*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
    if (!data) return;
    RECT rc{}; GetClientRect(hwnd, &rc);
    const int left = 13, right = (std::max)(left + 1, static_cast<int>(rc.right) - 13);
    x = (std::clamp)(x, left, right);
    const double ratio = static_cast<double>(x - left) / (right - left);
    data->value = data->minimum + static_cast<int>(ratio * (data->maximum - data->minimum) + 0.5);
    InvalidateRect(hwnd, nullptr, FALSE);
    RefreshSliders();
    PostMessageW(GetParent(hwnd), WM_HSCROLL, MAKEWPARAM(0, TB_THUMBTRACK), reinterpret_cast<LPARAM>(hwnd));
}

LRESULT CALLBACK SliderProc(HWND hwnd, UINT msg, WPARAM w, LPARAM l) {
    auto* data = reinterpret_cast<SliderData*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
    switch (msg) {
    case WM_ERASEBKGND: return 1;
    case WM_LBUTTONDOWN:
        if (data) data->dragging = true;
        SetCapture(hwnd); UpdateSliderFromPoint(hwnd, GET_X_LPARAM(l)); return 0;
    case WM_MOUSEMOVE:
        if (data && data->dragging && (w & MK_LBUTTON)) UpdateSliderFromPoint(hwnd, GET_X_LPARAM(l));
        return 0;
    case WM_LBUTTONUP:
        if (data) data->dragging = false;
        if (GetCapture() == hwnd) ReleaseCapture();
        UpdateSliderFromPoint(hwnd, GET_X_LPARAM(l)); return 0;
    case WM_PAINT: {
        PAINTSTRUCT ps{}; HDC dc = BeginPaint(hwnd, &ps);
        RECT rc{}; GetClientRect(hwnd, &rc);
        HDC mem = CreateCompatibleDC(dc); HBITMAP bmp = CreateCompatibleBitmap(dc, rc.right, rc.bottom);
        HBITMAP oldBmp = static_cast<HBITMAP>(SelectObject(mem, bmp));
        HBRUSH bg = CreateSolidBrush(COL_BG); FillRect(mem, &rc, bg); DeleteObject(bg);
        {
            Gp::Graphics g(mem);
            g.SetSmoothingMode(Gp::SmoothingModeAntiAlias);
            const float th = 14.0f;                                   // grosor de la barra
            const float cy = rc.bottom / 2.0f;
            const float trackL = 7.0f, trackR = static_cast<float>(rc.right) - 7.0f;
            const float left = 13.0f, right = (std::max)(left + 1.0f, static_cast<float>(rc.right) - 13.0f);
            const int value = data ? data->value : 0;
            const int minimum = data ? data->minimum : 0, maximum = data ? data->maximum : 100;
            const float ratio = static_cast<float>(value - minimum) / static_cast<float>((std::max)(1, maximum - minimum));
            const float thumbX = left + ratio * (right - left);

            // Barra de fondo
            Gp::RectF track(trackL, cy - th / 2.0f, trackR - trackL, th);
            Gp::GraphicsPath trackPath; RoundRectPath(trackPath, track, th / 2.0f);
            Gp::SolidBrush trackBrush(GdipColor(COL_TRACK));
            g.FillPath(&trackBrush, &trackPath);
            Gp::Pen trackPen(GdipColor(RGB(58, 55, 72)), 1.0f);
            g.DrawPath(&trackPen, &trackPath);

            // Relleno con degradado
            Gp::RectF fill(trackL, track.Y, (std::max)(th, thumbX - trackL), th);
            Gp::GraphicsPath fillPath; RoundRectPath(fillPath, fill, th / 2.0f);
            Gp::LinearGradientBrush fillBrush(fill, GdipColor(COL_PURPLE_DARK), GdipColor(COL_PURPLE_LIGHT),
                                              Gp::LinearGradientModeHorizontal);
            g.FillPath(&fillBrush, &fillPath);

            // Pomo: aura al arrastrar, sombra, circulo claro y punto morado
            if (data && data->dragging) {
                Gp::SolidBrush glow(GdipColor(COL_PURPLE, 70));
                g.FillEllipse(&glow, thumbX - 13.0f, cy - 13.0f, 26.0f, 26.0f);
            }
            Gp::SolidBrush shadow(GdipColor(RGB(0, 0, 0), 110));
            g.FillEllipse(&shadow, thumbX - 11.0f, cy - 10.0f, 22.0f, 22.0f);
            Gp::SolidBrush thumb(GdipColor(RGB(240, 230, 255)));
            g.FillEllipse(&thumb, thumbX - 10.0f, cy - 10.0f, 20.0f, 20.0f);
            Gp::SolidBrush dot(GdipColor(COL_PURPLE));
            g.FillEllipse(&dot, thumbX - 4.5f, cy - 4.5f, 9.0f, 9.0f);
        }
        BitBlt(dc, 0, 0, rc.right, rc.bottom, mem, 0, 0, SRCCOPY);
        SelectObject(mem, oldBmp); DeleteObject(bmp); DeleteDC(mem); EndPaint(hwnd, &ps); return 0;
    }
    }
    return DefWindowProcW(hwnd, msg, w, l);
}

void Worker() {
    using clock = std::chrono::steady_clock;
    std::mt19937 rng{ std::random_device{}() };
    SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_HIGHEST);
    PreciseTimer timer;
    bool prevDown = false;
    bool wasActive = false;
    auto next = clock::now();

    // Historial de clics (fisicos + automaticos) de el ultimo segundo
    std::deque<clock::time_point> history;
    auto trim = [&](clock::time_point t) {
        while (!history.empty() && t - history.front() >= std::chrono::milliseconds(1000))
            history.pop_front();
    };

    while (g_running.load()) {
        if (g_binding.load()) {
            for (int vk = 0x08; vk <= 0xFE; ++vk) {
                // No asignar botones del raton como tecla de toggle.
                if (vk == VK_LBUTTON || vk == VK_RBUTTON || vk == VK_MBUTTON ||
                    vk == VK_XBUTTON1 || vk == VK_XBUTTON2) continue;
                if (GetAsyncKeyState(vk) & 0x8000) {
                    g_key.store(vk == VK_ESCAPE ? 0 : vk);
                    g_binding.store(false);
                    PostMessageW(g_hwnd, WM_APP, 0, 0);
                    break;
                }
            }
            prevDown = false;
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
            continue;
        }

        int key = g_key.load();
        bool down = key != 0 && (GetAsyncKeyState(key) & 0x8000) != 0;
        if (down && !prevDown) {
            g_enabled.store(!g_enabled.load());
            PostMessageW(g_hwnd, WM_APP, 0, 0);
        }
        prevDown = down;

        const bool held = g_lmbHeld.load();   // leer ANTES que pressEval
        if (g_pressEval.exchange(false)) {
            g_pressInMenu.store(GuiOpen());
            history.push_back(clock::now());   // el clic fisico tambien cuenta para el limite
        }
        trim(clock::now());

        // Solo se puede clicar jugando: nunca en menus, inventarios, cofres, aldeanos...
        const bool menuBlocked = g_pressInMenu.load() || GuiOpen();
        const bool canClick = g_enabled.load() && held &&
                              MinecraftFocused() && !menuBlocked;
        if (!canClick) {
            wasActive = false;
            timer.SleepUntil(clock::now() + std::chrono::milliseconds(2), false);
            continue;
        }

        // CPS del slider (max 16) con variacion fija de +-0.5 CPS por clic:
        // ronda entre 15 y 16 y nunca supera 16.
        const double target = std::clamp(g_cpsX10.load() / 10.0, 1.0, kMaxCps);
        std::uniform_real_distribution<double> wobble(-0.5, 0.5);
        const double cps = std::clamp(target + wobble(rng), 1.0, kMaxCps);
        const double delay = 1000.0 / cps;

        const auto interval = std::chrono::microseconds(
            static_cast<long long>(delay * 1000.0));
        if (!wasActive) {
            wasActive = true;
            next = clock::now() + interval;   // el primer clic automatico espera un intervalo
        }

        // No ponerse al dia con clics acumulados tras una pausa del sistema:
        // cada clic debe respetar el intervalo minimo calculado.
        auto now = clock::now();
        if (now < next) {
            timer.SleepUntil(next);
            // Reconfirmar solo con comprobaciones baratas para no retrasar el clic.
            if (!g_lmbHeld.load() || !g_enabled.load() || GuiOpen()) continue;
        }

        // Limite duro: nunca mas de 16 clics (contando el fisico) en cualquier ventana de 1 s
        if (history.size() >= kMaxClicksPerSecond) {
            next = history.front() + std::chrono::milliseconds(1000);
            timer.SleepUntil(next);
            continue;
        }

        if (!g_lmbHeld.load() || !g_enabled.load() || GuiOpen()) continue;

        const auto clickStart = clock::now();
        std::uniform_int_distribution<int> hold(8, 16);
        int holdMs = std::min(hold(rng), std::max(1, static_cast<int>(delay * 0.35)));
        ClickOnce(holdMs, timer);
        history.push_back(clickStart);

        next = clickStart + interval;
        now = clock::now();
        if (next < now) next = now + interval;
    }
}

LRESULT CALLBACK WndProc(HWND h, UINT msg, WPARAM w, LPARAM l) {
    switch (msg) {
    case WM_CREATE: {
        g_hwnd = h;
        HINSTANCE hi = reinterpret_cast<LPCREATESTRUCTW>(l)->hInstance;
        HFONT font = static_cast<HFONT>(GetStockObject(DEFAULT_GUI_FONT));
        auto mk = [&](const wchar_t* cls, const wchar_t* txt, DWORD style,
                      int x, int y, int cx, int cy, int id) -> HWND {
            HWND c = CreateWindowW(cls, txt, WS_CHILD | WS_VISIBLE | style,
                x, y, cx, cy, h, reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)), hi, nullptr);
            if (c) SendMessageW(c, WM_SETFONT, reinterpret_cast<WPARAM>(font), TRUE);
            return c;
        };

        mk(L"STATIC", L"TECLA DE ACTIVACIÓN", 0, 20, 83, 240, 20, 0);
        g_btnBind = mk(L"BUTTON", L"Ninguna", BS_OWNERDRAW, 330, 75, 130, 34, ID_BIND);
        mk(L"STATIC", L"CLICKS POR SEGUNDO", 0, 20, 127, 220, 20, 0);
        g_lblCps = mk(L"STATIC", L"15.5", SS_RIGHT, 390, 124, 65, 25, ID_CPSVAL);
        g_sldCps = mk(L"AstralSlider", L"", 0, 20, 146, 440, 34, ID_CPS);
        SetWindowLongPtrW(g_sldCps, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(&g_cpsData));

        g_lblStatus = mk(L"STATIC", L"●  DESACTIVADO", 0, 20, 200, 420, 22, ID_STATUS);

        RefreshSliders();
        RefreshUI();
        return 0;
    }
    case WM_ERASEBKGND:
        return 1;
    case WM_PAINT: {
        PAINTSTRUCT ps{};
        HDC dc = BeginPaint(h, &ps);
        RECT rc{};
        GetClientRect(h, &rc);
        HBRUSH bg = g_darkBrush ? g_darkBrush : CreateSolidBrush(COL_BG);
        FillRect(dc, &rc, bg);
        if (!g_darkBrush) DeleteObject(bg);

        // Cabecera sin imagen externa: solo título y dirección del servidor.
        SetBkMode(dc, TRANSPARENT);
        HFONT titleFont = CreateFontW(23, 0, 0, 0, FW_BOLD, FALSE, FALSE, FALSE,
            DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
            DEFAULT_PITCH | FF_SWISS, L"Segoe UI");
        HFONT oldFont = (HFONT)SelectObject(dc, titleFont);
        SetTextColor(dc, RGB(246, 241, 255));
        RECT titleRc{20, 7, rc.right - 15, 36};
        DrawTextW(dc, L"AstralMC Clicker", -1, &titleRc, DT_LEFT | DT_VCENTER | DT_SINGLELINE);
        SelectObject(dc, oldFont);
        DeleteObject(titleFont);
        HFONT subFont = CreateFontW(12, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
            DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
            DEFAULT_PITCH | FF_SWISS, L"Segoe UI");
        oldFont = (HFONT)SelectObject(dc, subFont);
        SetTextColor(dc, COL_PURPLE_LIGHT);
        RECT subRc{21, 34, rc.right - 15, 53};
        DrawTextW(dc, L"Astralmc.cc", -1, &subRc, DT_LEFT | DT_VCENTER | DT_SINGLELINE);
        SelectObject(dc, oldFont);
        DeleteObject(subFont);
        {
            Gp::Graphics g(dc);
            Gp::RectF line(20.0f, 62.0f, static_cast<float>(rc.right) - 40.0f, 2.0f);
            Gp::LinearGradientBrush lb(line, GdipColor(COL_PURPLE), GdipColor(COL_BG),
                                        Gp::LinearGradientModeHorizontal);
            g.FillRectangle(&lb, line);
        }
        EndPaint(h, &ps);
        return 0;
    }
    case WM_CTLCOLORSTATIC: {
        HDC dc = (HDC)w;
        SetTextColor(dc, GetDlgCtrlID(reinterpret_cast<HWND>(l)) == ID_NOTE ? COL_MUTED : COL_TEXT);
        SetBkColor(dc, COL_BG);
        SetBkMode(dc, TRANSPARENT);
        return (LRESULT)(g_darkBrush ? g_darkBrush : GetStockObject(BLACK_BRUSH));
    }
    case WM_CTLCOLOREDIT: {
        HDC dc = (HDC)w;
        SetTextColor(dc, COL_PURPLE_LIGHT);
        SetBkColor(dc, COL_PANEL);
        return (LRESULT)(g_darkBrush ? g_darkBrush : GetStockObject(BLACK_BRUSH));
    }
    case WM_CTLCOLORBTN: {
        HDC dc = (HDC)w;
        SetTextColor(dc, COL_TEXT);
        SetBkColor(dc, COL_PANEL);
        return (LRESULT)(g_darkBrush ? g_darkBrush : GetStockObject(BLACK_BRUSH));
    }
    case WM_DRAWITEM: {
        auto* dis = reinterpret_cast<DRAWITEMSTRUCT*>(l);
        if (dis && dis->CtlID == ID_BIND) {
            const bool pressed = (dis->itemState & ODS_SELECTED) != 0;
            const bool active = g_binding.load();
            const int W = dis->rcItem.right - dis->rcItem.left;
            const int H = dis->rcItem.bottom - dis->rcItem.top;

            HDC mem = CreateCompatibleDC(dis->hDC);
            HBITMAP bmp = CreateCompatibleBitmap(dis->hDC, W, H);
            HBITMAP oldBmp = static_cast<HBITMAP>(SelectObject(mem, bmp));
            RECT full{0, 0, W, H};
            HBRUSH bg = CreateSolidBrush(COL_BG); FillRect(mem, &full, bg); DeleteObject(bg);

            {
                Gp::Graphics g(mem);
                g.SetSmoothingMode(Gp::SmoothingModeAntiAlias);
                Gp::RectF pill(1.5f, 1.5f, W - 3.0f, H - 3.0f);
                Gp::GraphicsPath path;
                RoundRectPath(path, pill, 10.0f);
                Gp::Color c1 = active ? GdipColor(RGB(91, 55, 137)) : GdipColor(pressed ? RGB(47, 39, 61) : RGB(34, 33, 43));
                Gp::Color c2 = active ? GdipColor(RGB(151, 94, 219)) : GdipColor(RGB(43, 41, 53));
                Gp::LinearGradientBrush br(pill, c1, c2, Gp::LinearGradientModeHorizontal);
                g.FillPath(&br, &path);
                Gp::Pen pen(active ? GdipColor(COL_PURPLE_LIGHT) : GdipColor(RGB(66, 62, 78)), 1.3f);
                g.DrawPath(&pen, &path);
            }

            // Texto (GDI, ClearType)
            SetBkMode(mem, TRANSPARENT);
            wchar_t text[128]{}; GetWindowTextW(dis->hwndItem, text, 128);
            HFONT f = MakeFont(15, FW_SEMIBOLD);
            HFONT oldF = static_cast<HFONT>(SelectObject(mem, f));
            SetTextColor(mem, active ? RGB(255, 250, 255) : COL_TEXT);
            RECT tr = full;
            DrawTextW(mem, text, -1, &tr, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
            SelectObject(mem, oldF); DeleteObject(f);

            BitBlt(dis->hDC, dis->rcItem.left, dis->rcItem.top, W, H, mem, 0, 0, SRCCOPY);
            SelectObject(mem, oldBmp); DeleteObject(bmp); DeleteDC(mem);
            return TRUE;
        }
        break;
    }
    case WM_HSCROLL:
        RefreshSliders();
        return 0;
    case WM_COMMAND:
        if (LOWORD(w) == ID_BIND) {
            g_binding.store(true);
            RefreshUI();
        }
        return 0;
    case WM_APP:
        RefreshUI();
        return 0;
    case WM_DESTROY:
        g_running.store(false);
        PostQuitMessage(0);
        return 0;
    }
    return DefWindowProcW(h, msg, w, l);
}

int WINAPI wWinMain(HINSTANCE hi, HINSTANCE, PWSTR, int show) {
    INITCOMMONCONTROLSEX icc{};
    icc.dwSize = sizeof(icc);
    icc.dwICC = ICC_BAR_CLASSES;
    if (!InitCommonControlsEx(&icc)) {
        MessageBoxW(nullptr, L"No se pudieron inicializar los controles de Windows.",
                    L"Error", MB_ICONERROR);
        return 1;
    }
    timeBeginPeriod(1);
    // Evitar que Windows 11 limite la precision del temporizador cuando la
    // ventana queda tapada por Minecraft, y dar prioridad a los hilos de entrada.
    SetPriorityClass(GetCurrentProcess(), ABOVE_NORMAL_PRIORITY_CLASS);
    SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_HIGHEST);
    {
        PROCESS_POWER_THROTTLING_STATE pt{};
        pt.Version = PROCESS_POWER_THROTTLING_CURRENT_VERSION;
        pt.ControlMask = PROCESS_POWER_THROTTLING_EXECUTION_SPEED |
                         PROCESS_POWER_THROTTLING_IGNORE_TIMER_RESOLUTION;
        pt.StateMask = 0;
        SetProcessInformation(GetCurrentProcess(), ProcessPowerThrottling, &pt, sizeof(pt));
    }
    Gp::GdiplusStartupInput gdipInput;
    ULONG_PTR gdipToken = 0;
    Gp::GdiplusStartup(&gdipToken, &gdipInput, nullptr);

    WNDCLASSW sliderClass{};
    sliderClass.lpfnWndProc = SliderProc;
    sliderClass.hInstance = hi;
    sliderClass.hCursor = LoadCursorW(nullptr, IDC_HAND);
    sliderClass.hbrBackground = nullptr;
    sliderClass.lpszClassName = L"AstralSlider";
    if (!RegisterClassW(&sliderClass) && GetLastError() != ERROR_CLASS_ALREADY_EXISTS) {
        timeEndPeriod(1);
        MessageBoxW(nullptr, L"No se pudo registrar el control deslizante.", L"Error", MB_ICONERROR);
        return 1;
    }

    WNDCLASSW wc{};
    wc.lpfnWndProc = WndProc;
    wc.hInstance = hi;
    wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    wc.hbrBackground = reinterpret_cast<HBRUSH>(COLOR_BTNFACE + 1);
    wc.lpszClassName = L"AstralClickerWindow";
    if (!RegisterClassW(&wc) && GetLastError() != ERROR_CLASS_ALREADY_EXISTS) {
        timeEndPeriod(1);
        MessageBoxW(nullptr, L"No se pudo registrar la ventana.", L"Error", MB_ICONERROR);
        return 1;
    }

    g_darkBrush = CreateSolidBrush(COL_BG);
    HWND h = CreateWindowW(wc.lpszClassName, L"AstralMC Clicker",
        WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX,
        CW_USEDEFAULT, CW_USEDEFAULT, 500, 300, nullptr, nullptr, hi, nullptr);
    if (!h) {
        timeEndPeriod(1);
        MessageBoxW(nullptr, L"No se pudo crear la ventana.", L"Error", MB_ICONERROR);
        return 1;
    }
    ShowWindow(h, show);
    UpdateWindow(h);

    g_mouseHook = SetWindowsHookExW(WH_MOUSE_LL, MouseProc, GetModuleHandleW(nullptr), 0);
    if (!g_mouseHook) {
        wchar_t msg[160]{};
        swprintf_s(msg, L"No se pudo instalar el hook del raton. Error de Windows: %lu",
                   GetLastError());
        MessageBoxW(h, msg, L"Error de raton", MB_ICONERROR);
        DestroyWindow(h);
        timeEndPeriod(1);
        return 1;
    }

    std::thread monitor(GuiMonitor);
    std::thread worker(Worker);
    MSG msg{};
    BOOL result;
    while ((result = GetMessageW(&msg, nullptr, 0, 0)) > 0) {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }

    g_running.store(false);
    if (worker.joinable()) worker.join();
    if (monitor.joinable()) monitor.join();
    if (g_mouseHook) UnhookWindowsHookEx(g_mouseHook);
    timeEndPeriod(1);
    if (g_darkBrush) DeleteObject(g_darkBrush);
    Gp::GdiplusShutdown(gdipToken);
    return result == -1 ? 1 : 0;
}
