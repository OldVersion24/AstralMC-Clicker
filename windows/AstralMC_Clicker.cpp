// AutoClicker para Minecraft Java - Win32 nativo, un solo archivo.
// Compilar con MSYS2 UCRT64 / MinGW-w64:
// g++ -std=c++17 -O2 -s -municode -mwindows -static -static-libgcc -static-libstdc++ AstralMC_Clicker.cpp -o AstralClicker.exe -lcomctl32 -lwinmm -luxtheme -lgdiplus -luser32 -lgdi32
// Compilar con Developer Command Prompt for Visual Studio:
// cl /EHsc /O2 /MT /std:c++17 /utf-8 AstralMC_Clicker.cpp comctl32.lib winmm.lib user32.lib gdi32.lib gdiplus.lib

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

enum { ID_CPS = 1 };

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

// Hotbar: slot seleccionado (0-8) y mascara de slots permitidos (bit i = slot i+1).
std::atomic<int>  g_slot{ 0 };
std::atomic<int>  g_slotMask{ 0x1FF };     // por defecto los 9 slots activos
std::atomic<int>  g_wheelAccum{ 0 };       // delta de rueda pendiente (eventos fisicos)

HWND g_hwnd = nullptr, g_sldCps = nullptr;
HHOOK g_mouseHook = nullptr;
HFONT g_fTitle = nullptr, g_fSub = nullptr, g_fGroup = nullptr, g_fLabel = nullptr, g_fSmall = nullptr;

constexpr COLORREF COL_BG = RGB(20, 21, 27);
constexpr COLORREF COL_PANEL = RGB(29, 30, 39);
constexpr COLORREF COL_BORDER = RGB(52, 50, 64);
constexpr COLORREF COL_PURPLE = RGB(145, 91, 220);
constexpr COLORREF COL_PURPLE_LIGHT = RGB(190, 150, 255);
constexpr COLORREF COL_TEXT = RGB(235, 233, 242);
constexpr COLORREF COL_MUTED = RGB(157, 157, 177);
constexpr COLORREF COL_PURPLE_DARK = RGB(83, 48, 125);
constexpr COLORREF COL_TRACK = RGB(44, 42, 53);
constexpr COLORREF COL_GREEN = RGB(110, 214, 140);
constexpr COLORREF COL_RED = RGB(224, 101, 122);

// ---- Layout (coordenadas de cliente) ----
constexpr int kClientW = 480, kClientH = 392;
constexpr int kPanelL = 16, kPanelT = 74, kPanelR = kClientW - 16, kPanelB = 352;
constexpr int kIX = 32, kIR = kClientW - 32;            // margen interior
const RECT kRcEnabled   { kIX, 112, kIX + 160, 130 };
const RECT kRcCpsLabel  { kIX, 140, kIR, 158 };
constexpr int kSliderY = 158, kSliderH = 32;
const RECT kRcInventory { kIX, 200, kIX + 200, 218 };
const RECT kRcSlotsLbl  { kIX, 230, kIR, 248 };
constexpr int kSlotY = 254, kSlotSize = 26;
const RECT kRcBind      { kIX, 298, kIR, 334 };
const RECT kRcStatus    { kPanelL + 4, 360, kPanelR, 384 };

RECT SlotRect(int i) {
    const int step = (kIR - kIX - kSlotSize) / 8;
    const int x = kIX + i * step;
    return RECT{ x, kSlotY, x + kSlotSize, kSlotY + kSlotSize };
}

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
            } else if (w == WM_MOUSEWHEEL) {
                // Solo se guarda el giro de la rueda para seguir el slot de la hotbar.
                g_wheelAccum.fetch_add(static_cast<short>(HIWORD(ms->mouseData)));
            }
        }
    }
    return CallNextHookEx(g_mouseHook, n, w, l);
}

// Reconoce Minecraft por el titulo o por el proceso javaw.exe.
bool MinecraftFocused() {
    HWND fg = GetForegroundWindow();
    if (!fg) return false;

    // Ventana de juego de Minecraft: LWJGL 2 (1.8.9) o GLFW (versiones nuevas).
    // No se usa el titulo: un navegador o un chat con "Minecraft" en el titulo no cuenta.
    wchar_t cls[64]{};
    GetClassNameW(fg, cls, static_cast<int>(std::size(cls)));
    if (wcscmp(cls, L"LWJGL") == 0 || wcsncmp(cls, L"GLFW", 4) == 0) return true;

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
// en cada frame. Solo se considera GUI abierta si el cursor lleva mas de
// GUI_GRACE_MS sin pasar por el centro.
constexpr long long GUI_GRACE_MS = 60;
std::atomic<long long> g_lastInGameMs{ 0 };

// Senal rapida para 1.8.9 (LWJGL 2): jugando, Minecraft pone un cursor transparente
// propio; con una GUI abierta vuelve a un cursor estandar de Windows.
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

bool GuiOpen() {
    if (g_cursorSignalValid.load() && g_cursorStd.load()) return true;   // instantaneo
    return NowMs() - g_lastInGameMs.load() > GUI_GRACE_MS;               // respaldo
}

void GuiMonitor() {
    SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_HIGHEST);
    PreciseTimer timer;
    constexpr LONG tolerance = 5;
    bool focused = false;
    auto nextFocusCheck = Clock::now();
    auto tick = Clock::now();
    g_lastInGameMs.store(NowMs());

    // Seguimiento del slot de la hotbar: teclas 1-9 y rueda del raton, solo jugando.
    bool prevNum[9]{};
    int wheelRemainder = 0;

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
            if (centered && !stdCursor) g_cursorSignalValid.store(true);
        }
        g_cursorStd.store(stdCursor);

        // Sin Minecraft enfocado no hay GUI que bloquear.
        if (!focused || centered) g_lastInGameMs.store(NowMs());

        // ---- Slot de la hotbar ----
        const bool inGame = focused && !GuiOpen();
        const int wheel = g_wheelAccum.exchange(0);
        int slot = g_slot.load();
        const int before = slot;
        if (inGame) {
            // Rueda hacia abajo = slot siguiente (como en Minecraft).
            wheelRemainder += wheel;
            while (wheelRemainder <= -WHEEL_DELTA) { slot = (slot + 1) % 9; wheelRemainder += WHEEL_DELTA; }
            while (wheelRemainder >=  WHEEL_DELTA) { slot = (slot + 8) % 9; wheelRemainder -= WHEEL_DELTA; }
        } else {
            wheelRemainder = 0;
        }
        for (int k = 0; k < 9; ++k) {
            const bool down = (GetAsyncKeyState('1' + k) & 0x8000) != 0;
            if (inGame && down && !prevNum[k]) slot = k;
            prevNum[k] = down;
        }
        if (slot != before) {
            g_slot.store(slot);
            PostMessageW(g_hwnd, WM_APP, 0, 0);
        }

        tick += std::chrono::milliseconds(1);
        if (tick < Clock::now()) tick = Clock::now() + std::chrono::milliseconds(1);
        timer.SleepUntil(tick, false);
    }
}

bool SlotAllowed() {
    return (g_slotMask.load() >> g_slot.load()) & 1;
}

void ClickOnce(int holdMs, PreciseTimer& timer) {
    INPUT input[2]{};
    input[0].type = INPUT_MOUSE;
    input[0].mi.dwFlags = MOUSEEVENTF_LEFTDOWN;
    input[1].type = INPUT_MOUSE;
    input[1].mi.dwFlags = MOUSEEVENTF_LEFTUP;
    SendInput(1, &input[0], sizeof(INPUT));
    timer.SleepUntil(Clock::now() + std::chrono::milliseconds(holdMs), false);
    SendInput(1, &input[1], sizeof(INPUT));
}

void Redraw() { if (g_hwnd) InvalidateRect(g_hwnd, nullptr, FALSE); }

void RefreshSliders() {
    auto* cpsData = reinterpret_cast<SliderData*>(GetWindowLongPtrW(g_sldCps, GWLP_USERDATA));
    if (!cpsData) return;
    g_cpsX10.store(cpsData->value);
    if (g_hwnd) InvalidateRect(g_hwnd, &kRcCpsLabel, FALSE);
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
        HBRUSH bg = CreateSolidBrush(COL_PANEL); FillRect(mem, &rc, bg); DeleteObject(bg);
        {
            Gp::Graphics g(mem);
            g.SetSmoothingMode(Gp::SmoothingModeAntiAlias);
            const float th = 12.0f;
            const float cy = rc.bottom / 2.0f;
            const float trackL = 7.0f, trackR = static_cast<float>(rc.right) - 7.0f;
            const float left = 13.0f, right = (std::max)(left + 1.0f, static_cast<float>(rc.right) - 13.0f);
            const int value = data ? data->value : 0;
            const int minimum = data ? data->minimum : 0, maximum = data ? data->maximum : 100;
            const float ratio = static_cast<float>(value - minimum) / static_cast<float>((std::max)(1, maximum - minimum));
            const float thumbX = left + ratio * (right - left);

            Gp::RectF track(trackL, cy - th / 2.0f, trackR - trackL, th);
            Gp::GraphicsPath trackPath; RoundRectPath(trackPath, track, th / 2.0f);
            Gp::SolidBrush trackBrush(GdipColor(COL_TRACK));
            g.FillPath(&trackBrush, &trackPath);
            Gp::Pen trackPen(GdipColor(RGB(58, 55, 72)), 1.0f);
            g.DrawPath(&trackPen, &trackPath);

            Gp::RectF fill(trackL, track.Y, (std::max)(th, thumbX - trackL), th);
            Gp::GraphicsPath fillPath; RoundRectPath(fillPath, fill, th / 2.0f);
            Gp::LinearGradientBrush fillBrush(fill, GdipColor(COL_PURPLE_DARK), GdipColor(COL_PURPLE_LIGHT),
                                              Gp::LinearGradientModeHorizontal);
            g.FillPath(&fillBrush, &fillPath);

            if (data && data->dragging) {
                Gp::SolidBrush glow(GdipColor(COL_PURPLE, 70));
                g.FillEllipse(&glow, thumbX - 12.0f, cy - 12.0f, 24.0f, 24.0f);
            }
            Gp::SolidBrush shadow(GdipColor(RGB(0, 0, 0), 110));
            g.FillEllipse(&shadow, thumbX - 10.0f, cy - 9.0f, 20.0f, 20.0f);
            Gp::SolidBrush thumb(GdipColor(RGB(240, 230, 255)));
            g.FillEllipse(&thumb, thumbX - 9.0f, cy - 9.0f, 18.0f, 18.0f);
            Gp::SolidBrush dot(GdipColor(COL_PURPLE));
            g.FillEllipse(&dot, thumbX - 4.0f, cy - 4.0f, 8.0f, 8.0f);
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

    // Historial de clics (fisicos + automaticos) del ultimo segundo
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
            prevDown = true;   // no activar con la misma pulsacion que asigna la tecla
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
        // y solo con un slot de la hotbar permitido.
        const bool menuBlocked = g_pressInMenu.load() || GuiOpen();
        const bool canClick = g_enabled.load() && held && SlotAllowed() &&
                              MinecraftFocused() && !menuBlocked;
        if (!canClick) {
            wasActive = false;
            timer.SleepUntil(clock::now() + std::chrono::milliseconds(2), false);
            continue;
        }

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

        auto now = clock::now();
        if (now < next) {
            timer.SleepUntil(next);
            if (!g_lmbHeld.load() || !g_enabled.load() || GuiOpen() || !SlotAllowed()) continue;
        }

        // Limite duro: nunca mas de 16 clics (contando el fisico) en cualquier ventana de 1 s
        if (history.size() >= kMaxClicksPerSecond) {
            next = history.front() + std::chrono::milliseconds(1000);
            timer.SleepUntil(next);
            continue;
        }

        if (!g_lmbHeld.load() || !g_enabled.load() || GuiOpen() || !SlotAllowed()) continue;

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

// ---- Dibujo de la ventana principal ----
void DrawLabel(HDC dc, const wchar_t* s, RECT r, HFONT f, COLORREF c, UINT fmt = DT_LEFT) {
    HFONT old = static_cast<HFONT>(SelectObject(dc, f));
    SetTextColor(dc, c);
    DrawTextW(dc, s, -1, &r, fmt | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
    SelectObject(dc, old);
}

void DrawCheckbox(HDC dc, Gp::Graphics& g, const RECT& r, const wchar_t* label, bool checked) {
    const float y = static_cast<float>(r.top + (r.bottom - r.top - 16) / 2);
    Gp::RectF box(static_cast<float>(r.left) + 0.5f, y + 0.5f, 15.0f, 15.0f);
    Gp::GraphicsPath p; RoundRectPath(p, box, 3.0f);
    if (checked) {
        Gp::LinearGradientBrush br(box, GdipColor(COL_PURPLE_DARK), GdipColor(COL_PURPLE_LIGHT),
                                   Gp::LinearGradientModeForwardDiagonal);
        g.FillPath(&br, &p);
        Gp::Pen tick(GdipColor(RGB(255, 255, 255)), 2.0f);
        tick.SetStartCap(Gp::LineCapRound); tick.SetEndCap(Gp::LineCapRound);
        Gp::PointF pts[3] = { {box.X + 3.5f, box.Y + 8.0f}, {box.X + 6.5f, box.Y + 11.0f}, {box.X + 12.0f, box.Y + 4.5f} };
        g.DrawLines(&tick, pts, 3);
    } else {
        Gp::SolidBrush br(GdipColor(COL_TRACK));
        g.FillPath(&br, &p);
        Gp::Pen pen(GdipColor(RGB(70, 66, 84)), 1.0f);
        g.DrawPath(&pen, &p);
    }
    RECT tr{ r.left + 26, r.top, r.right + 200, r.bottom };
    DrawLabel(dc, label, tr, g_fLabel, COL_TEXT);
}

void PaintMain(HWND h, HDC dc) {
    RECT rc{}; GetClientRect(h, &rc);
    HBRUSH bg = CreateSolidBrush(COL_BG); FillRect(dc, &rc, bg); DeleteObject(bg);
    SetBkMode(dc, TRANSPARENT);

    // Cabecera
    DrawLabel(dc, L"AstralMC Clicker", RECT{20, 7, rc.right - 15, 36}, g_fTitle, RGB(246, 241, 255));
    DrawLabel(dc, L"Astralmc.cc", RECT{21, 34, rc.right - 15, 53}, g_fSub, COL_PURPLE_LIGHT);

    Gp::Graphics g(dc);
    g.SetSmoothingMode(Gp::SmoothingModeAntiAlias);
    {
        Gp::RectF line(20.0f, 60.0f, static_cast<float>(rc.right) - 40.0f, 2.0f);
        Gp::LinearGradientBrush lb(line, GdipColor(COL_PURPLE), GdipColor(COL_BG), Gp::LinearGradientModeHorizontal);
        g.FillRectangle(&lb, line);
    }

    // Panel "Left clicker"
    {
        Gp::RectF panel(kPanelL + 0.5f, kPanelT + 0.5f, kPanelR - kPanelL - 1.0f, kPanelB - kPanelT - 1.0f);
        Gp::GraphicsPath p; RoundRectPath(p, panel, 10.0f);
        Gp::SolidBrush br(GdipColor(COL_PANEL));
        g.FillPath(&br, &p);
        Gp::Pen pen(GdipColor(COL_BORDER), 1.0f);
        g.DrawPath(&pen, &p);
    }
    DrawLabel(dc, L"Left clicker", RECT{kIX, 82, kIR, 104}, g_fGroup, COL_PURPLE_LIGHT);

    DrawCheckbox(dc, g, kRcEnabled, L"Enabled", g_enabled.load());

    DrawLabel(dc, L"Clicks per second", kRcCpsLabel, g_fLabel, COL_TEXT);
    wchar_t cps[16]{}; swprintf_s(cps, L"%.1f", g_cpsX10.load() / 10.0);
    DrawLabel(dc, cps, kRcCpsLabel, g_fLabel, COL_PURPLE_LIGHT, DT_RIGHT);

    DrawCheckbox(dc, g, kRcInventory, L"Click on inventory", false);

    DrawLabel(dc, L"Whitelisted hotbar slots", kRcSlotsLbl, g_fLabel, COL_TEXT);
    const int mask = g_slotMask.load(), cur = g_slot.load();
    for (int i = 0; i < 9; ++i) {
        const RECT r = SlotRect(i);
        const bool on = (mask >> i) & 1;
        // Casilla con tick: morada y marcada = activa; vacia y gris = desactivada.
        const float s = 22.0f;
        const float x0 = r.left + (kSlotSize - s) / 2.0f, y0 = r.top + 0.5f;
        Gp::RectF sq(x0, y0, s, s);
        Gp::GraphicsPath p; RoundRectPath(p, sq, 4.0f);
        if (on) {
            Gp::LinearGradientBrush br(sq, GdipColor(COL_PURPLE_LIGHT), GdipColor(COL_PURPLE), Gp::LinearGradientModeForwardDiagonal);
            g.FillPath(&br, &p);
            Gp::Pen tick(GdipColor(RGB(255, 255, 255)), 2.6f);
            tick.SetStartCap(Gp::LineCapRound); tick.SetEndCap(Gp::LineCapRound);
            tick.SetLineJoin(Gp::LineJoinRound);
            Gp::PointF pts[3] = { {x0 + 5.0f, y0 + 11.5f}, {x0 + 9.5f, y0 + 16.0f}, {x0 + 17.0f, y0 + 6.5f} };
            g.DrawLines(&tick, pts, 3);
        } else {
            Gp::SolidBrush br(GdipColor(RGB(24, 24, 31)));
            g.FillPath(&br, &p);
            Gp::Pen pen(GdipColor(RGB(88, 84, 104)), 1.5f);
            g.DrawPath(&pen, &p);
        }
        // Slot que tienes en la mano: rayita blanca debajo.
        if (i == cur) {
            Gp::SolidBrush mark(GdipColor(RGB(255, 255, 255)));
            g.FillRectangle(&mark, x0 + 5.0f, y0 + s + 4.0f, s - 10.0f, 2.5f);
        }
    }

    // Boton "Bind"
    {
        const bool active = g_binding.load();
        Gp::RectF b(kRcBind.left + 0.5f, kRcBind.top + 0.5f, kRcBind.right - kRcBind.left - 1.0f, kRcBind.bottom - kRcBind.top - 1.0f);
        Gp::GraphicsPath p; RoundRectPath(p, b, 8.0f);
        Gp::LinearGradientBrush br(b, active ? GdipColor(RGB(91, 55, 137)) : GdipColor(RGB(36, 35, 46)),
                                   active ? GdipColor(RGB(151, 94, 219)) : GdipColor(RGB(43, 41, 53)),
                                   Gp::LinearGradientModeHorizontal);
        g.FillPath(&br, &p);
        Gp::Pen pen(active ? GdipColor(COL_PURPLE_LIGHT) : GdipColor(RGB(66, 62, 78)), 1.0f);
        g.DrawPath(&pen, &p);

        wchar_t text[128]{};
        const int vk = g_key.load();
        if (active) {
            wcscpy_s(text, L"Bind: press a key...  (Esc = none)");
        } else if (!vk) {
            wcscpy_s(text, L"Bind: None");
        } else {
            wchar_t name[64]{};
            UINT sc = MapVirtualKeyW(static_cast<UINT>(vk), MAPVK_VK_TO_VSC);
            if (!GetKeyNameTextW(static_cast<LONG>(sc << 16), name, static_cast<int>(std::size(name))))
                swprintf_s(name, L"VK %d", vk);
            swprintf_s(text, L"Bind: %s", name);
        }
        DrawLabel(dc, text, kRcBind, g_fLabel, COL_TEXT, DT_CENTER);
    }

    // Estado
    const bool on = g_enabled.load();
    DrawLabel(dc, on ? L"●  ON" : L"●  OFF", kRcStatus, g_fLabel, on ? COL_GREEN : COL_RED);
    wchar_t slotTxt[48]{};
    swprintf_s(slotTxt, L"Current slot: %d%s", cur + 1, SlotAllowed() ? L"" : L"  (blocked)");
    DrawLabel(dc, slotTxt, kRcStatus, g_fSmall, COL_MUTED, DT_RIGHT);
}

bool InRect(const RECT& r, int x, int y, int padRight = 0) {
    return x >= r.left && x < r.right + padRight && y >= r.top && y < r.bottom;
}

LRESULT CALLBACK WndProc(HWND h, UINT msg, WPARAM w, LPARAM l) {
    switch (msg) {
    case WM_CREATE: {
        g_hwnd = h;
        HINSTANCE hi = reinterpret_cast<LPCREATESTRUCTW>(l)->hInstance;
        g_sldCps = CreateWindowW(L"AstralSlider", L"", WS_CHILD | WS_VISIBLE,
            kIX - 7, kSliderY, kIR - kIX + 14, kSliderH, h,
            reinterpret_cast<HMENU>(static_cast<INT_PTR>(ID_CPS)), hi, nullptr);
        SetWindowLongPtrW(g_sldCps, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(&g_cpsData));
        RefreshSliders();
        return 0;
    }
    case WM_ERASEBKGND:
        return 1;
    case WM_PAINT: {
        PAINTSTRUCT ps{};
        HDC dc = BeginPaint(h, &ps);
        RECT rc{}; GetClientRect(h, &rc);
        HDC mem = CreateCompatibleDC(dc);
        HBITMAP bmp = CreateCompatibleBitmap(dc, rc.right, rc.bottom);
        HBITMAP oldBmp = static_cast<HBITMAP>(SelectObject(mem, bmp));
        PaintMain(h, mem);
        BitBlt(dc, 0, 0, rc.right, rc.bottom, mem, 0, 0, SRCCOPY);
        SelectObject(mem, oldBmp); DeleteObject(bmp); DeleteDC(mem);
        EndPaint(h, &ps);
        return 0;
    }
    case WM_SETCURSOR: {
        POINT pt{}; GetCursorPos(&pt); ScreenToClient(h, &pt);
        bool hot = InRect(kRcEnabled, pt.x, pt.y) || InRect(kRcInventory, pt.x, pt.y) || InRect(kRcBind, pt.x, pt.y);
        for (int i = 0; i < 9 && !hot; ++i) hot = InRect(SlotRect(i), pt.x, pt.y);
        if (hot && LOWORD(l) == HTCLIENT) { SetCursor(LoadCursorW(nullptr, IDC_HAND)); return TRUE; }
        break;
    }
    case WM_LBUTTONDOWN: {
        const int x = GET_X_LPARAM(l), y = GET_Y_LPARAM(l);
        if (InRect(kRcEnabled, x, y)) {
            g_enabled.store(!g_enabled.load());
            Redraw();
        } else if (InRect(kRcInventory, x, y)) {
            // Politica del servidor: no se permite autoclick en inventarios (refill = baneable).
            MessageBoxW(h,
                L"Autoclicker for click on inventory is not allowed on AstralMC.\n"
                L"Just for PvP.",
                L"AstralMC", MB_ICONWARNING | MB_OK);
        } else if (InRect(kRcBind, x, y)) {
            g_binding.store(true);
            Redraw();
        } else {
            for (int i = 0; i < 9; ++i) {
                if (InRect(SlotRect(i), x, y)) {
                    g_slotMask.store(g_slotMask.load() ^ (1 << i));
                    Redraw();
                    break;
                }
            }
        }
        return 0;
    }
    case WM_APP:
        Redraw();
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

    g_fTitle = MakeFont(23, FW_BOLD);
    g_fSub   = MakeFont(12, FW_NORMAL);
    g_fGroup = MakeFont(16, FW_BOLD);
    g_fLabel = MakeFont(14, FW_SEMIBOLD);
    g_fSmall = MakeFont(12, FW_SEMIBOLD);

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
    wc.hbrBackground = nullptr;
    wc.lpszClassName = L"AstralClickerWindow";
    if (!RegisterClassW(&wc) && GetLastError() != ERROR_CLASS_ALREADY_EXISTS) {
        timeEndPeriod(1);
        MessageBoxW(nullptr, L"No se pudo registrar la ventana.", L"Error", MB_ICONERROR);
        return 1;
    }

    const DWORD style = WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX | WS_CLIPCHILDREN;
    RECT wr{ 0, 0, kClientW, kClientH };
    AdjustWindowRect(&wr, style, FALSE);
    HWND h = CreateWindowW(wc.lpszClassName, L"AstralMC Clicker", style,
        CW_USEDEFAULT, CW_USEDEFAULT, wr.right - wr.left, wr.bottom - wr.top, nullptr, nullptr, hi, nullptr);
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
    for (HFONT f : { g_fTitle, g_fSub, g_fGroup, g_fLabel, g_fSmall }) if (f) DeleteObject(f);
    Gp::GdiplusShutdown(gdipToken);
    return result == -1 ? 1 : 0;
}
