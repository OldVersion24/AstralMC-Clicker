// AstralMC Clicker - version Linux (X11 / XWayland). Un solo archivo.
//
// Misma logica que la version de Windows:
//   - maximo 16 CPS (limite duro de 16 clics en cualquier ventana de 1 s, contando el clic real)
//   - solo clica con la ventana de Minecraft enfocada y nunca en menus / inventarios
//   - solo en los slots de la hotbar marcados
//   - "Click on inventory" no esta permitido en AstralMC
//
// Dependencias (vienen con cualquier escritorio Linux): libX11, libXi, libXfixes, libcairo.
// Compilar (Debian/Ubuntu: sudo apt install g++ pkg-config libx11-dev libxi-dev libxfixes-dev libcairo2-dev):
//   g++ -std=c++17 -O2 -s astral_clicker.cpp -o AstralClicker-linux $(pkg-config --cflags --libs x11 xi xfixes cairo) -pthread -static-libstdc++ -static-libgcc
//
// Wayland: funciona con Minecraft normal (corre por XWayland). No funciona si Minecraft
// se fuerza a Wayland nativo, porque Wayland no deja a otros programas clicar en el.

#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include <X11/Xatom.h>
#include <X11/XKBlib.h>
#include <X11/keysym.h>
#include <X11/cursorfont.h>
#include <X11/extensions/XInput2.h>
#include <X11/extensions/Xfixes.h>
#include <cairo/cairo.h>
#include <cairo/cairo-xlib.h>

#include <algorithm>
#include <cctype>
#include <cerrno>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <deque>
#include <random>
#include <string>
#include <thread>
#include <vector>
#include <poll.h>
#include <time.h>
#include <unistd.h>

// ---- Limites impuestos por los owners ----
constexpr double kMaxCps = 16.0;
constexpr size_t kMaxClicksPerSecond = 16;

std::atomic<bool> g_enabled{ false };
std::atomic<bool> g_binding{ false };
std::atomic<bool> g_running{ true };
std::atomic<bool> g_lmbHeld{ false };
std::atomic<bool> g_pressEval{ false };
std::atomic<bool> g_pressInMenu{ false };
std::atomic<int>  g_cpsX10{ 155 };          // 1.0 - 16.0 CPS (defecto 15.5)
std::atomic<int>  g_keycode{ 0 };           // tecla de activacion (keycode X11), 0 = ninguna
std::atomic<int>  g_slot{ 0 };              // slot actual de la hotbar (0-8)
std::atomic<int>  g_slotMask{ 0x1FF };      // slots permitidos (bit i = slot i+1)
std::atomic<bool> g_breakBlocks{ true };    // "Break blocks": se puede romper bloques sin apagar el clicker
std::atomic<int>  g_wheelAccum{ 0 };        // giro de rueda pendiente (+120 arriba, -120 abajo)
std::atomic<int>  g_pendingDigit{ 0 };      // tecla 1-9 pulsada pendiente (0 = ninguna)
std::atomic<bool> g_dirty{ true };          // hay que redibujar la ventana
std::string g_fatal;                        // error a mostrar en la ventana

constexpr int kSliderMin = 10, kSliderMax = 160;

// ---- Colores (mismos que en Windows) ----
struct Rgb { double r, g, b; };
constexpr Rgb rgb(int r, int g, int b) { return { r / 255.0, g / 255.0, b / 255.0 }; }
constexpr Rgb COL_BG = rgb(20, 21, 27);
constexpr Rgb COL_PANEL = rgb(29, 30, 39);
constexpr Rgb COL_BORDER = rgb(52, 50, 64);
constexpr Rgb COL_PURPLE = rgb(145, 91, 220);
constexpr Rgb COL_PURPLE_LIGHT = rgb(190, 150, 255);
constexpr Rgb COL_PURPLE_DARK = rgb(83, 48, 125);
constexpr Rgb COL_TEXT = rgb(235, 233, 242);
constexpr Rgb COL_MUTED = rgb(157, 157, 177);
constexpr Rgb COL_TRACK = rgb(44, 42, 53);
constexpr Rgb COL_GREEN = rgb(110, 214, 140);
constexpr Rgb COL_RED = rgb(224, 101, 122);
constexpr Rgb COL_WHITE = rgb(255, 255, 255);

// ---- Layout (igual que en Windows) ----
struct Rect { int l, t, r, b; bool has(int x, int y) const { return x >= l && x < r && y >= t && y < b; } };
constexpr int kClientW = 480, kClientH = 420;
constexpr int kPanelL = 16, kPanelT = 74, kPanelR = kClientW - 16, kPanelB = 380;
constexpr int kIX = 32, kIR = kClientW - 32;
constexpr Rect kRcEnabled   { kIX, 112, kIX + 160, 130 };
constexpr Rect kRcCpsLabel  { kIX, 140, kIR, 158 };
constexpr Rect kRcSlider    { kIX - 7, 158, kIR + 7, 190 };
constexpr Rect kRcBreak     { kIX, 200, kIX + 200, 218 };
constexpr Rect kRcInventory { kIX, 228, kIX + 200, 246 };
constexpr Rect kRcSlotsLbl  { kIX, 258, kIR, 276 };
constexpr int kSlotY = 282, kSlotSize = 26;
constexpr Rect kRcBind      { kIX, 326, kIR, 362 };
constexpr Rect kRcStatus    { kPanelL + 4, 388, kPanelR, 412 };
// Aviso de inventario (dentro de la ventana)
constexpr Rect kRcDialog    { 40, 134, kClientW - 40, 294 };
constexpr Rect kRcDialogOk  { kClientW / 2 - 50, 246, kClientW / 2 + 50, 278 };

Rect SlotRect(int i) {
    const int step = (kIR - kIX - kSlotSize) / 8;
    const int x = kIX + i * step;
    return Rect{ x, kSlotY, x + kSlotSize, kSlotY + kSlotSize };
}

// ---- Tiempo ----
using Clock = std::chrono::steady_clock;   // CLOCK_MONOTONIC en Linux

long long NowMs() {
    return std::chrono::duration_cast<std::chrono::milliseconds>(Clock::now().time_since_epoch()).count();
}

void SleepUntil(Clock::time_point t) {
    const auto ns = std::chrono::duration_cast<std::chrono::nanoseconds>(t.time_since_epoch()).count();
    timespec ts{ static_cast<time_t>(ns / 1000000000LL), static_cast<long>(ns % 1000000000LL) };
    while (clock_nanosleep(CLOCK_MONOTONIC, TIMER_ABSTIME, &ts, nullptr) == EINTR) {}
}

// Las ventanas pueden desaparecer entre llamadas: se ignoran los errores X (BadWindow, etc.).
int IgnoreXErrors(Display*, XErrorEvent*) { return 0; }

// ---- Deteccion de Minecraft ----
Window ActiveWindow(Display* d) {
    static thread_local Atom netActive = XInternAtom(d, "_NET_ACTIVE_WINDOW", False);
    Atom type; int format; unsigned long n, after; unsigned char* data = nullptr;
    Window w = 0;
    if (XGetWindowProperty(d, DefaultRootWindow(d), netActive, 0, 1, False, XA_WINDOW,
                           &type, &format, &n, &after, &data) == Success && data) {
        if (n == 1 && format == 32) w = *reinterpret_cast<Window*>(data);
        XFree(data);
    }
    return w;
}

bool ContainsNoCase(const char* s, const char* needle) {
    if (!s) return false;
    std::string a(s), b(needle);
    std::transform(a.begin(), a.end(), a.begin(), ::tolower);
    return a.find(b) != std::string::npos;
}

// Minecraft = ventana con clase "Minecraft"/"LWJGL" o cuyo proceso es java.
// No se mira el titulo: un navegador con "Minecraft" en el titulo no cuenta.
bool IsMinecraftWindow(Display* d, Window w) {
    if (!w) return false;
    XClassHint ch{};
    if (XGetClassHint(d, w, &ch)) {
        const bool mc = ContainsNoCase(ch.res_class, "minecraft") || ContainsNoCase(ch.res_name, "minecraft") ||
                        ContainsNoCase(ch.res_class, "lwjgl") || ContainsNoCase(ch.res_name, "lwjgl");
        if (ch.res_class) XFree(ch.res_class);
        if (ch.res_name) XFree(ch.res_name);
        if (mc) return true;
    }
    static thread_local Atom netPid = XInternAtom(d, "_NET_WM_PID", False);
    Atom type; int format; unsigned long n, after; unsigned char* data = nullptr;
    long pid = 0;
    if (XGetWindowProperty(d, w, netPid, 0, 1, False, XA_CARDINAL, &type, &format, &n, &after, &data) == Success && data) {
        if (n == 1 && format == 32) pid = *reinterpret_cast<long*>(data);
        XFree(data);
    }
    if (pid <= 0) return false;
    char path[64]; std::snprintf(path, sizeof(path), "/proc/%ld/comm", pid);
    FILE* f = std::fopen(path, "r");
    if (!f) return false;
    char comm[64]{};
    const bool ok = std::fgets(comm, sizeof(comm), f) != nullptr;
    std::fclose(f);
    if (!ok) return false;
    comm[std::strcspn(comm, "\n")] = 0;
    return std::strcmp(comm, "java") == 0 || std::strcmp(comm, "javaw") == 0;
}

// ---- Deteccion de menus (GUI abierta) ----
// Jugando, Minecraft oculta el cursor y lo recoloca en el centro de la ventana en cada frame.
// Con un menu abierto el cursor es visible y se mueve libre.
constexpr long long GUI_GRACE_MS = 60;
std::atomic<long long> g_lastInGameMs{ 0 };
std::atomic<bool> g_cursorVisible{ false };
std::atomic<bool> g_cursorSignalValid{ false };
std::atomic<bool> g_mcFocused{ false };
std::atomic<unsigned long> g_mcWindow{ 0 };   // ventana de Minecraft enfocada

bool GuiOpen() {
    if (g_cursorSignalValid.load() && g_cursorVisible.load()) return true;   // instantaneo
    return NowMs() - g_lastInGameMs.load() > GUI_GRACE_MS;                   // respaldo
}

bool SlotAllowed() { return (g_slotMask.load() >> g_slot.load()) & 1; }

// Solo para la prueba automatica de GitHub Actions (se compila aparte con -DASTRAL_TEST):
// simula el clic izquierdo mantenido. El programa publicado no lleva esto.
#ifdef ASTRAL_TEST
constexpr bool kTestHold = true;
#else
constexpr bool kTestHold = false;
#endif
bool LmbHeld() { return kTestHold || g_lmbHeld.load(); }

bool CursorImageVisible(Display* d) {
    XFixesCursorImage* img = XFixesGetCursorImage(d);
    if (!img) return true;
    bool visible = false;
    const size_t count = static_cast<size_t>(img->width) * img->height;
    for (size_t i = 0; i < count && !visible; ++i)
        if ((img->pixels[i] >> 24) & 0xFF) visible = true;   // algun pixel no transparente
    XFree(img);
    return visible;
}

void GuiMonitor() {
    Display* d = XOpenDisplay(nullptr);
    if (!d) return;
    constexpr int tolerance = 5;
    bool focused = false;
    Window mc = 0;
    auto nextFocusCheck = Clock::now();
    auto tick = Clock::now();
    int wheelRemainder = 0;
    unsigned counter = 0;
    g_lastInGameMs.store(NowMs());

    while (g_running.load()) {
        const auto now = Clock::now();
        if (now >= nextFocusCheck) {
            const Window w = ActiveWindow(d);
            focused = IsMinecraftWindow(d, w);
            mc = focused ? w : 0;
            g_mcWindow.store(mc);
            g_mcFocused.store(focused);
            nextFocusCheck = now + std::chrono::milliseconds(40);
        }

        bool centered = false;
        if (focused) {
            XWindowAttributes wa{};
            Window rootRet, childRet; int rx, ry, wx, wy; unsigned mask;
            if (XGetWindowAttributes(d, mc, &wa) &&
                XQueryPointer(d, mc, &rootRet, &childRet, &rx, &ry, &wx, &wy, &mask)) {
                centered = std::abs(wx - wa.width / 2) <= tolerance && std::abs(wy - wa.height / 2) <= tolerance;
            }
        }
        // El cursor se comprueba cada 4 ms (es mas costoso).
        if (focused && (++counter % 4 == 0)) {
            const bool visible = CursorImageVisible(d);
            if (centered && !visible) g_cursorSignalValid.store(true);
            g_cursorVisible.store(visible);
        } else if (!focused) {
            g_cursorVisible.store(false);
        }

        if (!focused || centered) g_lastInGameMs.store(NowMs());

        // ---- Slot de la hotbar (teclas 1-9 y rueda, solo jugando) ----
        const bool inGame = focused && !GuiOpen();
        const int wheel = g_wheelAccum.exchange(0);
        const int digit = g_pendingDigit.exchange(0);
        int slot = g_slot.load();
        const int before = slot;
        if (inGame) {
            wheelRemainder += wheel;
            while (wheelRemainder <= -120) { slot = (slot + 1) % 9; wheelRemainder += 120; }   // abajo = siguiente
            while (wheelRemainder >= 120)  { slot = (slot + 8) % 9; wheelRemainder -= 120; }
            if (digit) slot = digit - 1;
        } else {
            wheelRemainder = 0;
        }
        if (slot != before) { g_slot.store(slot); g_dirty.store(true); }

        tick += std::chrono::milliseconds(1);
        if (tick < Clock::now()) tick = Clock::now() + std::chrono::milliseconds(1);
        SleepUntil(tick);
    }
    XCloseDisplay(d);
}

// ---- Entrada fisica (XInput2 raw events) ----
// Solo se usan: boton izquierdo, rueda, la tecla de activacion y las teclas 1-9.
// Los eventos que genera el propio programa (dispositivo XTEST) se ignoran.
void InputThread() {
    Display* d = XOpenDisplay(nullptr);
    if (!d) return;
    int xiOpcode, ev, err;
    if (!XQueryExtension(d, "XInputExtension", &xiOpcode, &ev, &err)) { XCloseDisplay(d); return; }
    // Anunciar XI 2.2: asi los raw events llegan aunque Minecraft tenga el raton capturado.
    int xiMajor = 2, xiMinor = 2;
    if (XIQueryVersion(d, &xiMajor, &xiMinor) != Success) { XCloseDisplay(d); return; }

    std::vector<int> xtestIds;
    int n = 0;
    if (XIDeviceInfo* info = XIQueryDevice(d, XIAllDevices, &n)) {
        for (int i = 0; i < n; ++i)
            if (info[i].name && std::strstr(info[i].name, "XTEST")) xtestIds.push_back(info[i].deviceid);
        XIFreeDeviceInfo(info);
    }
    auto injected = [&](int src) { return std::find(xtestIds.begin(), xtestIds.end(), src) != xtestIds.end(); };

    unsigned char bits[XIMaskLen(XI_LASTEVENT)] = { 0 };
    XISetMask(bits, XI_RawButtonPress);
    XISetMask(bits, XI_RawButtonRelease);
    XISetMask(bits, XI_RawKeyPress);
    XISetMask(bits, XI_RawKeyRelease);
    XIEventMask mask{ XIAllMasterDevices, static_cast<int>(sizeof(bits)), bits };
    XISelectEvents(d, DefaultRootWindow(d), &mask, 1);
    XFlush(d);

    bool toggleDown = false;
    pollfd pfd{ ConnectionNumber(d), POLLIN, 0 };
    while (g_running.load()) {
        if (!XPending(d)) { poll(&pfd, 1, 100); continue; }
        XEvent e;
        XNextEvent(d, &e);
        if (e.type != GenericEvent || e.xcookie.extension != xiOpcode || !XGetEventData(d, &e.xcookie)) continue;
        const auto* re = static_cast<const XIRawEvent*>(e.xcookie.data);
        if (!injected(re->sourceid)) {
            switch (e.xcookie.evtype) {
            case XI_RawButtonPress:
                if (re->detail == 1) { g_pressEval.store(true); g_lmbHeld.store(true); }
                else if (re->detail == 4) g_wheelAccum.fetch_add(120);    // rueda arriba
                else if (re->detail == 5) g_wheelAccum.fetch_add(-120);   // rueda abajo
                break;
            case XI_RawButtonRelease:
                if (re->detail == 1) { g_lmbHeld.store(false); g_pressEval.store(false); g_pressInMenu.store(false); }
                break;
            case XI_RawKeyPress: {
                const int kc = re->detail;
                if (!g_binding.load() && kc != 0 && kc == g_keycode.load() && !toggleDown) {
                    toggleDown = true;
                    g_enabled.store(!g_enabled.load());
                    g_dirty.store(true);
                }
                const KeySym ks = XkbKeycodeToKeysym(d, static_cast<KeyCode>(kc), 0, 0);
                if (ks >= XK_1 && ks <= XK_9 && g_mcFocused.load()) g_pendingDigit.store(static_cast<int>(ks - XK_1) + 1);
                break;
            }
            case XI_RawKeyRelease:
                if (re->detail == g_keycode.load()) toggleDown = false;
                break;
            }
        }
        XFreeEventData(d, &e.xcookie);
    }
    XCloseDisplay(d);
}

// ---- Clicador ----
// Los clics se envian directamente a la ventana de Minecraft (XSendEvent).
// No se usa XTEST: en X11, si mantienes el boton real pulsado, el servidor ignora
// las pulsaciones de XTEST del mismo boton y Minecraft no recibiria ningun clic.
void SendLeft(Display* d, bool down) {
    const Window w = static_cast<Window>(g_mcWindow.load());
    if (!w) return;
    XWindowAttributes wa{};
    if (!XGetWindowAttributes(d, w, &wa)) return;
    const int cx = wa.width / 2, cy = wa.height / 2;
    int rx = cx, ry = cy;
    Window child = 0;
    XTranslateCoordinates(d, w, wa.root, cx, cy, &rx, &ry, &child);
    XEvent e{};
    e.xbutton.type = down ? ButtonPress : ButtonRelease;
    e.xbutton.display = d;
    e.xbutton.window = w;
    e.xbutton.root = wa.root;
    e.xbutton.subwindow = None;
    e.xbutton.time = CurrentTime;
    e.xbutton.x = cx;
    e.xbutton.y = cy;
    e.xbutton.x_root = rx;
    e.xbutton.y_root = ry;
    e.xbutton.state = down ? 0 : Button1Mask;
    e.xbutton.button = Button1;
    e.xbutton.same_screen = True;
    XSendEvent(d, w, True, down ? ButtonPressMask : ButtonReleaseMask, &e);
    XFlush(d);
}

void ClickOnce(Display* d, int holdMs) {
    SendLeft(d, true);
    SleepUntil(Clock::now() + std::chrono::milliseconds(holdMs));
    SendLeft(d, false);
}

// Modo "Break blocks": en vez de pulsar y soltar, suelta y vuelve a pulsar casi a la vez.
// Asi el boton queda pulsado entre clics: Minecraft sigue rompiendo el bloque que miras
// y cada nueva pulsacion cuenta como un clic para pegar.
void ClickBreak(Display* d) {
    SendLeft(d, false);
    SleepUntil(Clock::now() + std::chrono::milliseconds(1));
    SendLeft(d, true);
}

void Worker() {
    Display* d = XOpenDisplay(nullptr);
    if (!d) return;
    std::mt19937 rng{ std::random_device{}() };
    bool wasActive = false;
    bool ourDown = false;   // la ultima pulsacion enviada fue nuestra (modo Break blocks)
    auto next = Clock::now();
    std::deque<Clock::time_point> history;   // clics (fisicos + automaticos) del ultimo segundo
    auto trim = [&](Clock::time_point t) {
        while (!history.empty() && t - history.front() >= std::chrono::milliseconds(1000)) history.pop_front();
    };

    while (g_running.load()) {
        const bool held = LmbHeld();
        if (g_pressEval.exchange(false)) {
            g_pressInMenu.store(GuiOpen());
            history.push_back(Clock::now());   // el clic fisico tambien cuenta para el limite
        }
        trim(Clock::now());

        const bool menuBlocked = g_pressInMenu.load() || GuiOpen();
        const bool canClick = g_enabled.load() && held && SlotAllowed() && g_mcFocused.load() && !menuBlocked;
        if (!canClick) {
            // Si soltaste el raton justo despues de nuestra pulsacion, se suelta tambien
            // para que el boton no se quede pulsado.
            if (ourDown && !held) { SendLeft(d, false); ourDown = false; }
            wasActive = false;
            SleepUntil(Clock::now() + std::chrono::milliseconds(2));
            continue;
        }

        const double target = std::clamp(g_cpsX10.load() / 10.0, 1.0, kMaxCps);
        std::uniform_real_distribution<double> wobble(-0.5, 0.5);
        const double cps = std::clamp(target + wobble(rng), 1.0, kMaxCps);
        const double delay = 1000.0 / cps;
        const auto interval = std::chrono::microseconds(static_cast<long long>(delay * 1000.0));
        if (!wasActive) { wasActive = true; next = Clock::now() + interval; }

        if (Clock::now() < next) {
            SleepUntil(next);
            if (!LmbHeld() || !g_enabled.load() || GuiOpen() || !SlotAllowed()) continue;
        }
        if (history.size() >= kMaxClicksPerSecond) {   // limite duro: 16 clics por segundo
            next = history.front() + std::chrono::milliseconds(1000);
            SleepUntil(next);
            continue;
        }
        if (!LmbHeld() || !g_enabled.load() || GuiOpen() || !SlotAllowed()) continue;

        const auto clickStart = Clock::now();
        std::uniform_int_distribution<int> hold(8, 16);
        const int holdMs = std::min(hold(rng), std::max(1, static_cast<int>(delay * 0.35)));
        if (g_breakBlocks.load()) {
            ClickBreak(d);
            ourDown = true;
            if (!LmbHeld()) { SendLeft(d, false); ourDown = false; }
        } else {
            ClickOnce(d, holdMs);
            ourDown = false;
        }
        history.push_back(clickStart);

        next = clickStart + interval;
        if (next < Clock::now()) next = Clock::now() + interval;
    }
    XCloseDisplay(d);
}

// ---- Dibujo (cairo) ----
bool g_showWarning = false;
bool g_dragging = false;

void SetColor(cairo_t* cr, Rgb c, double a = 1.0) { cairo_set_source_rgba(cr, c.r, c.g, c.b, a); }

void RoundRect(cairo_t* cr, double x, double y, double w, double h, double r) {
    cairo_new_sub_path(cr);
    cairo_arc(cr, x + w - r, y + r, r, -M_PI / 2, 0);
    cairo_arc(cr, x + w - r, y + h - r, r, 0, M_PI / 2);
    cairo_arc(cr, x + r, y + h - r, r, M_PI / 2, M_PI);
    cairo_arc(cr, x + r, y + r, r, M_PI, 3 * M_PI / 2);
    cairo_close_path(cr);
}

enum Align { LEFT, CENTER, RIGHT };
void Text(cairo_t* cr, const char* s, Rect r, double size, bool bold, Rgb c, Align a = LEFT) {
    cairo_select_font_face(cr, "Sans", CAIRO_FONT_SLANT_NORMAL, bold ? CAIRO_FONT_WEIGHT_BOLD : CAIRO_FONT_WEIGHT_NORMAL);
    cairo_set_font_size(cr, size);
    cairo_text_extents_t te; cairo_text_extents(cr, s, &te);
    cairo_font_extents_t fe; cairo_font_extents(cr, &fe);
    double x = r.l;
    if (a == RIGHT) x = r.r - te.x_advance;
    else if (a == CENTER) x = r.l + (r.r - r.l - te.x_advance) / 2.0;
    const double y = r.t + (r.b - r.t - (fe.ascent + fe.descent)) / 2.0 + fe.ascent;
    SetColor(cr, c);
    cairo_move_to(cr, x, y);
    cairo_show_text(cr, s);
}

void Tick(cairo_t* cr, double x0, double y0, double s, double width) {
    cairo_set_line_width(cr, width);
    cairo_set_line_cap(cr, CAIRO_LINE_CAP_ROUND);
    cairo_set_line_join(cr, CAIRO_LINE_JOIN_ROUND);
    SetColor(cr, COL_WHITE);
    cairo_move_to(cr, x0 + s * 0.23, y0 + s * 0.52);
    cairo_line_to(cr, x0 + s * 0.43, y0 + s * 0.73);
    cairo_line_to(cr, x0 + s * 0.77, y0 + s * 0.30);
    cairo_stroke(cr);
}

void Checkbox(cairo_t* cr, Rect r, const char* label, bool checked) {
    const double x = r.l + 0.5, y = r.t + (r.b - r.t - 16) / 2.0 + 0.5;
    RoundRect(cr, x, y, 15, 15, 3);
    if (checked) {
        cairo_pattern_t* p = cairo_pattern_create_linear(x, y, x + 15, y + 15);
        cairo_pattern_add_color_stop_rgb(p, 0, COL_PURPLE_DARK.r, COL_PURPLE_DARK.g, COL_PURPLE_DARK.b);
        cairo_pattern_add_color_stop_rgb(p, 1, COL_PURPLE_LIGHT.r, COL_PURPLE_LIGHT.g, COL_PURPLE_LIGHT.b);
        cairo_set_source(cr, p); cairo_fill(cr); cairo_pattern_destroy(p);
        Tick(cr, x, y, 15, 2.0);
    } else {
        SetColor(cr, COL_TRACK); cairo_fill_preserve(cr);
        SetColor(cr, rgb(70, 66, 84)); cairo_set_line_width(cr, 1); cairo_stroke(cr);
    }
    Text(cr, label, Rect{ r.l + 26, r.t, r.r + 200, r.b }, 14, true, COL_TEXT);
}

std::string KeyName(Display* d, int kc) {
    if (!kc) return "None";
    const KeySym ks = XkbKeycodeToKeysym(d, static_cast<KeyCode>(kc), 0, 0);
    const char* s = ks ? XKeysymToString(ks) : nullptr;
    if (!s) { char b[16]; std::snprintf(b, sizeof(b), "Key %d", kc); return b; }
    std::string name(s);
    if (name.size() == 1) name[0] = static_cast<char>(std::toupper(static_cast<unsigned char>(name[0])));
    return name;
}

void Paint(Display* d, cairo_t* cr) {
    SetColor(cr, COL_BG); cairo_paint(cr);

    Text(cr, "AstralMC Clicker", Rect{ 20, 7, kClientW - 15, 36 }, 20, true, rgb(246, 241, 255));
    Text(cr, "Astralmc.cc", Rect{ 21, 34, kClientW - 15, 53 }, 11.5, false, COL_PURPLE_LIGHT);
    {
        cairo_pattern_t* p = cairo_pattern_create_linear(20, 0, kClientW - 20, 0);
        cairo_pattern_add_color_stop_rgb(p, 0, COL_PURPLE.r, COL_PURPLE.g, COL_PURPLE.b);
        cairo_pattern_add_color_stop_rgb(p, 1, COL_BG.r, COL_BG.g, COL_BG.b);
        cairo_rectangle(cr, 20, 60, kClientW - 40, 2);
        cairo_set_source(cr, p); cairo_fill(cr); cairo_pattern_destroy(p);
    }

    // Panel "Left clicker"
    RoundRect(cr, kPanelL + 0.5, kPanelT + 0.5, kPanelR - kPanelL - 1, kPanelB - kPanelT - 1, 10);
    SetColor(cr, COL_PANEL); cairo_fill_preserve(cr);
    SetColor(cr, COL_BORDER); cairo_set_line_width(cr, 1); cairo_stroke(cr);
    Text(cr, "Left clicker", Rect{ kIX, 82, kIR, 104 }, 15, true, COL_PURPLE_LIGHT);

    Checkbox(cr, kRcEnabled, "Enabled", g_enabled.load());

    Text(cr, "Clicks per second", kRcCpsLabel, 14, true, COL_TEXT);
    char cps[16]; std::snprintf(cps, sizeof(cps), "%.1f", g_cpsX10.load() / 10.0);
    Text(cr, cps, kRcCpsLabel, 14, true, COL_PURPLE_LIGHT, RIGHT);

    // Slider
    {
        const double th = 12, cy = (kRcSlider.t + kRcSlider.b) / 2.0;
        const double trackL = kRcSlider.l + 7, trackR = kRcSlider.r - 7;
        const double left = kRcSlider.l + 13, right = kRcSlider.r - 13;
        const double ratio = (g_cpsX10.load() - kSliderMin) / double(kSliderMax - kSliderMin);
        const double thumbX = left + ratio * (right - left);
        RoundRect(cr, trackL, cy - th / 2, trackR - trackL, th, th / 2);
        SetColor(cr, COL_TRACK); cairo_fill_preserve(cr);
        SetColor(cr, rgb(58, 55, 72)); cairo_set_line_width(cr, 1); cairo_stroke(cr);
        cairo_pattern_t* p = cairo_pattern_create_linear(trackL, 0, trackL + std::max(th, thumbX - trackL), 0);
        cairo_pattern_add_color_stop_rgb(p, 0, COL_PURPLE_DARK.r, COL_PURPLE_DARK.g, COL_PURPLE_DARK.b);
        cairo_pattern_add_color_stop_rgb(p, 1, COL_PURPLE_LIGHT.r, COL_PURPLE_LIGHT.g, COL_PURPLE_LIGHT.b);
        RoundRect(cr, trackL, cy - th / 2, std::max(th, thumbX - trackL), th, th / 2);
        cairo_set_source(cr, p); cairo_fill(cr); cairo_pattern_destroy(p);
        if (g_dragging) { SetColor(cr, COL_PURPLE, 70 / 255.0); cairo_arc(cr, thumbX, cy, 12, 0, 2 * M_PI); cairo_fill(cr); }
        SetColor(cr, rgb(0, 0, 0), 110 / 255.0); cairo_arc(cr, thumbX, cy + 1, 10, 0, 2 * M_PI); cairo_fill(cr);
        SetColor(cr, rgb(240, 230, 255)); cairo_arc(cr, thumbX, cy, 9, 0, 2 * M_PI); cairo_fill(cr);
        SetColor(cr, COL_PURPLE); cairo_arc(cr, thumbX, cy, 4, 0, 2 * M_PI); cairo_fill(cr);
    }

    Checkbox(cr, kRcBreak, "Break blocks", g_breakBlocks.load());
    Checkbox(cr, kRcInventory, "Click on inventory", false);

    // Slots de la hotbar
    Text(cr, "Whitelisted hotbar slots", kRcSlotsLbl, 14, true, COL_TEXT);
    const int mask = g_slotMask.load(), cur = g_slot.load();
    for (int i = 0; i < 9; ++i) {
        const Rect r = SlotRect(i);
        const bool on = (mask >> i) & 1;
        const double s = 22, x0 = r.l + (kSlotSize - s) / 2.0, y0 = r.t + 0.5;
        RoundRect(cr, x0, y0, s, s, 4);
        if (on) {
            cairo_pattern_t* p = cairo_pattern_create_linear(x0, y0, x0 + s, y0 + s);
            cairo_pattern_add_color_stop_rgb(p, 0, COL_PURPLE_LIGHT.r, COL_PURPLE_LIGHT.g, COL_PURPLE_LIGHT.b);
            cairo_pattern_add_color_stop_rgb(p, 1, COL_PURPLE.r, COL_PURPLE.g, COL_PURPLE.b);
            cairo_set_source(cr, p); cairo_fill(cr); cairo_pattern_destroy(p);
            Tick(cr, x0, y0, s, 2.6);
        } else {
            SetColor(cr, rgb(24, 24, 31)); cairo_fill_preserve(cr);
            SetColor(cr, rgb(88, 84, 104)); cairo_set_line_width(cr, 1.5); cairo_stroke(cr);
        }
        if (i == cur) { SetColor(cr, COL_WHITE); cairo_rectangle(cr, x0 + 5, y0 + s + 4, s - 10, 2.5); cairo_fill(cr); }
    }

    // Boton Bind
    {
        const bool active = g_binding.load();
        const Rect b = kRcBind;
        RoundRect(cr, b.l + 0.5, b.t + 0.5, b.r - b.l - 1, b.b - b.t - 1, 8);
        cairo_pattern_t* p = cairo_pattern_create_linear(b.l, 0, b.r, 0);
        const Rgb c1 = active ? rgb(91, 55, 137) : rgb(36, 35, 46), c2 = active ? rgb(151, 94, 219) : rgb(43, 41, 53);
        cairo_pattern_add_color_stop_rgb(p, 0, c1.r, c1.g, c1.b);
        cairo_pattern_add_color_stop_rgb(p, 1, c2.r, c2.g, c2.b);
        cairo_set_source(cr, p); cairo_fill_preserve(cr); cairo_pattern_destroy(p);
        SetColor(cr, active ? COL_PURPLE_LIGHT : rgb(66, 62, 78)); cairo_set_line_width(cr, 1); cairo_stroke(cr);
        const std::string label = active ? "Bind: press a key...  (Esc = none)" : "Bind: " + KeyName(d, g_keycode.load());
        Text(cr, label.c_str(), b, 14, true, COL_TEXT, CENTER);
    }

    // Estado
    if (!g_fatal.empty()) {
        Text(cr, g_fatal.c_str(), kRcStatus, 12, true, COL_RED);
    } else {
        const bool on = g_enabled.load();
        const double cy = (kRcStatus.t + kRcStatus.b) / 2.0;
        SetColor(cr, on ? COL_GREEN : COL_RED); cairo_arc(cr, kRcStatus.l + 4, cy, 3.5, 0, 2 * M_PI); cairo_fill(cr);
        Text(cr, on ? "ON" : "OFF", Rect{ kRcStatus.l + 18, kRcStatus.t, kRcStatus.r, kRcStatus.b }, 14, true, on ? COL_GREEN : COL_RED);
        char slotTxt[48];
        std::snprintf(slotTxt, sizeof(slotTxt), "Current slot: %d%s", cur + 1, SlotAllowed() ? "" : "  (blocked)");
        Text(cr, slotTxt, kRcStatus, 12, true, COL_MUTED, RIGHT);
    }

    // Aviso "Click on inventory"
    if (g_showWarning) {
        SetColor(cr, rgb(0, 0, 0), 0.6); cairo_paint(cr);
        const Rect r = kRcDialog;
        RoundRect(cr, r.l + 0.5, r.t + 0.5, r.r - r.l - 1, r.b - r.t - 1, 12);
        SetColor(cr, COL_PANEL); cairo_fill_preserve(cr);
        SetColor(cr, rgb(224, 157, 77)); cairo_set_line_width(cr, 1.5); cairo_stroke(cr);
        Text(cr, "AstralMC", Rect{ r.l + 20, r.t + 14, r.r - 20, r.t + 38 }, 15, true, rgb(224, 157, 77));
        Text(cr, "Autoclicker for click on inventory", Rect{ r.l + 20, r.t + 46, r.r - 20, r.t + 66 }, 14, true, COL_TEXT);
        Text(cr, "is not allowed on AstralMC.", Rect{ r.l + 20, r.t + 66, r.r - 20, r.t + 86 }, 14, true, COL_TEXT);
        Text(cr, "Just for PvP.", Rect{ r.l + 20, r.t + 88, r.r - 20, r.t + 106 }, 13, false, COL_MUTED);
        const Rect ok = kRcDialogOk;
        RoundRect(cr, ok.l, ok.t, ok.r - ok.l, ok.b - ok.t, 8);
        SetColor(cr, COL_PURPLE); cairo_fill(cr);
        Text(cr, "OK", ok, 14, true, COL_WHITE, CENTER);
    }
}

void SetSliderFromX(int x) {
    const int left = kRcSlider.l + 13, right = kRcSlider.r - 13;
    x = std::clamp(x, left, right);
    const double ratio = double(x - left) / (right - left);
    g_cpsX10.store(kSliderMin + static_cast<int>(ratio * (kSliderMax - kSliderMin) + 0.5));
    g_dirty.store(true);
}

bool IsHot(int x, int y) {
    if (g_showWarning) return kRcDialogOk.has(x, y);
    if (kRcEnabled.has(x, y) || kRcBreak.has(x, y) || kRcInventory.has(x, y) || kRcBind.has(x, y) || kRcSlider.has(x, y)) return true;
    for (int i = 0; i < 9; ++i) if (SlotRect(i).has(x, y)) return true;
    return false;
}

void OnClick(int x, int y) {
    if (g_showWarning) { if (kRcDialogOk.has(x, y)) g_showWarning = false; g_dirty.store(true); return; }
    if (kRcEnabled.has(x, y)) { g_enabled.store(!g_enabled.load()); }
    else if (kRcBreak.has(x, y)) { g_breakBlocks.store(!g_breakBlocks.load()); }
    else if (kRcInventory.has(x, y)) { g_showWarning = true; }   // no permitido: refill = baneable
    else if (kRcBind.has(x, y)) { g_binding.store(true); }
    else if (kRcSlider.has(x, y)) { g_dragging = true; SetSliderFromX(x); }
    else for (int i = 0; i < 9; ++i) if (SlotRect(i).has(x, y)) { g_slotMask.store(g_slotMask.load() ^ (1 << i)); break; }
    g_dirty.store(true);
}

int main() {
    XInitThreads();
    XSetErrorHandler(IgnoreXErrors);
    Display* d = XOpenDisplay(nullptr);
    if (!d) {
        std::fprintf(stderr,
            "AstralMC Clicker: no se pudo abrir la pantalla X11.\n"
            "Necesitas una sesion X11 o XWayland (lo normal en GNOME/KDE).\n");
        return 1;
    }

    int op, ev, er, xiMajor = 2, xiMinor = 2;
    if (!XQueryExtension(d, "XInputExtension", &op, &ev, &er) || XIQueryVersion(d, &xiMajor, &xiMinor) != Success)
        g_fatal = "Error: falta XInput2 en este sistema.";
    else if (!XQueryExtension(d, "XFIXES", &op, &ev, &er))
        g_fatal = "Error: falta la extension XFIXES en este sistema.";

    const int screen = DefaultScreen(d);
    Window win = XCreateSimpleWindow(d, RootWindow(d, screen), 0, 0, kClientW, kClientH, 0, 0, 0x14151b);

    // Titulo, clase y tamano fijo
    XStoreName(d, win, "AstralMC Clicker");
    const char* title = "AstralMC Clicker";
    XChangeProperty(d, win, XInternAtom(d, "_NET_WM_NAME", False), XInternAtom(d, "UTF8_STRING", False), 8,
                    PropModeReplace, reinterpret_cast<const unsigned char*>(title), static_cast<int>(std::strlen(title)));
    XClassHint ch{ const_cast<char*>("astral-clicker"), const_cast<char*>("AstralClicker") };
    XSetClassHint(d, win, &ch);
    XSizeHints* sh = XAllocSizeHints();
    sh->flags = PMinSize | PMaxSize;
    sh->min_width = sh->max_width = kClientW;
    sh->min_height = sh->max_height = kClientH;
    XSetWMNormalHints(d, win, sh);
    XFree(sh);
    Atom wmDelete = XInternAtom(d, "WM_DELETE_WINDOW", False);
    XSetWMProtocols(d, win, &wmDelete, 1);

    XSelectInput(d, win, ExposureMask | ButtonPressMask | ButtonReleaseMask | PointerMotionMask | KeyPressMask | StructureNotifyMask);
    XMapWindow(d, win);

    Cursor handCursor = XCreateFontCursor(d, XC_hand2);
    bool handShown = false;

    cairo_surface_t* winSurf = cairo_xlib_surface_create(d, win, DefaultVisual(d, screen), kClientW, kClientH);
    cairo_surface_t* back = cairo_image_surface_create(CAIRO_FORMAT_RGB24, kClientW, kClientH);   // doble buffer

    if (kTestHold) {
        g_enabled.store(true);
        const char* b = std::getenv("ASTRAL_TEST_BREAK");
        if (b && b[0] == '0') g_breakBlocks.store(false);
    }
    std::thread monitor, input, worker;
    if (g_fatal.empty()) {
        monitor = std::thread(GuiMonitor);
        input = std::thread(InputThread);
        worker = std::thread(Worker);
    }

    pollfd pfd{ ConnectionNumber(d), POLLIN, 0 };
    while (g_running.load()) {
        while (XPending(d)) {
            XEvent e;
            XNextEvent(d, &e);
            switch (e.type) {
            case Expose: g_dirty.store(true); break;
            case ButtonPress:
                if (e.xbutton.button == Button1) OnClick(e.xbutton.x, e.xbutton.y);
                break;
            case ButtonRelease:
                if (e.xbutton.button == Button1 && g_dragging) { g_dragging = false; g_dirty.store(true); }
                break;
            case MotionNotify: {
                if (g_dragging) SetSliderFromX(e.xmotion.x);
                const bool hot = g_dragging || IsHot(e.xmotion.x, e.xmotion.y);
                if (hot != handShown) {
                    if (hot) XDefineCursor(d, win, handCursor); else XUndefineCursor(d, win);
                    handShown = hot;
                }
                break;
            }
            case KeyPress: {
                const KeySym ks = XLookupKeysym(&e.xkey, 0);
                if (g_showWarning) {
                    if (ks == XK_Return || ks == XK_Escape || ks == XK_KP_Enter || ks == XK_space) { g_showWarning = false; g_dirty.store(true); }
                } else if (g_binding.load()) {
                    // La tecla se elige con esta ventana enfocada (no se lee el teclado de otros programas).
                    g_keycode.store(ks == XK_Escape ? 0 : static_cast<int>(e.xkey.keycode));
                    g_binding.store(false);
                    g_dirty.store(true);
                }
                break;
            }
            case ClientMessage:
                if (static_cast<Atom>(e.xclient.data.l[0]) == wmDelete) g_running.store(false);
                break;
            }
        }
        if (g_dirty.exchange(false)) {
            cairo_t* cr = cairo_create(back);
            Paint(d, cr);
            cairo_destroy(cr);
            cairo_t* wc = cairo_create(winSurf);
            cairo_set_source_surface(wc, back, 0, 0);
            cairo_paint(wc);
            cairo_destroy(wc);
            cairo_surface_flush(winSurf);
            XFlush(d);
        }
        poll(&pfd, 1, 30);
    }

    g_running.store(false);
    if (worker.joinable()) worker.join();
    if (input.joinable()) input.join();
    if (monitor.joinable()) monitor.join();
    cairo_surface_destroy(back);
    cairo_surface_destroy(winSurf);
    XFreeCursor(d, handCursor);
    XDestroyWindow(d, win);
    XCloseDisplay(d);
    return 0;
}
