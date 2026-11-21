// Prueba automatica: una ventana que se comporta como Minecraft para el clicker.
//  - clase de ventana "Minecraft" y es la ventana activa (_NET_ACTIVE_WINDOW)
//  - "jugando": cursor oculto y recolocado en el centro, como hace el juego
//  - "menu abierto": cursor normal y libre
// Cuenta los clics que recibe en cada fase y falla si el clicker no se comporta bien.
#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include <X11/Xatom.h>
#include <X11/cursorfont.h>
#include <stdio.h>
#include <time.h>
#include <poll.h>

static double now(void) {
    struct timespec ts; clock_gettime(CLOCK_MONOTONIC, &ts);
    return ts.tv_sec + ts.tv_nsec / 1e9;
}

int main(void) {
    Display* d = XOpenDisplay(NULL);
    if (!d) { fprintf(stderr, "sin display\n"); return 2; }
    Window root = DefaultRootWindow(d);
    const int W = 400, H = 300;
    Window w = XCreateSimpleWindow(d, root, 600, 50, W, H, 0, 0, 0x335533);
    XClassHint ch = { "minecraft", "Minecraft" };
    XSetClassHint(d, w, &ch);
    XSelectInput(d, w, ButtonPressMask);
    XMapRaised(d, w);
    XChangeProperty(d, root, XInternAtom(d, "_NET_ACTIVE_WINDOW", False), XA_WINDOW, 32,
                    PropModeReplace, (unsigned char*)&w, 1);

    // Cursor invisible (como Minecraft jugando) y cursor normal (menu abierto)
    char zero = 0;
    Pixmap pm = XCreateBitmapFromData(d, w, &zero, 1, 1);
    XColor black = { 0 };
    Cursor blank = XCreatePixmapCursor(d, pm, pm, &black, &black, 0, 0);
    Cursor arrow = XCreateFontCursor(d, XC_left_ptr);
    XFlush(d);

    // Fases: 0-1 s calentar, 1-5 s jugando, 5-7 s menu abierto, 7-9 s jugando otra vez
    const double t0 = now();
    int phaseClicks[4] = { 0 }, maxPerSecond = 0;
    double times[4096]; int n = 0;
    int lastPhase = -1;
    struct pollfd pfd = { ConnectionNumber(d), POLLIN, 0 };

    for (;;) {
        const double t = now() - t0;
        if (t > 9.0) break;
        const int phase = t < 1 ? 0 : t < 5 ? 1 : t < 7 ? 2 : 3;
        const int inGame = phase != 2;
        if (phase != lastPhase) {
            XDefineCursor(d, w, inGame ? blank : arrow);
            if (!inGame) XWarpPointer(d, None, w, 0, 0, 0, 0, 40, 40);   // cursor libre, fuera del centro
            lastPhase = phase;
            printf("t=%.1fs fase %d (%s)\n", t, phase, inGame ? "jugando" : "menu abierto");
        }
        if (inGame) XWarpPointer(d, None, w, 0, 0, 0, 0, W / 2, H / 2);
        XFlush(d);

        while (XPending(d)) {
            XEvent e; XNextEvent(d, &e);
            if (e.type == ButtonPress && e.xbutton.button == Button1) {
                const double tc = now() - t0;
                const int p = tc < 1 ? 0 : tc < 5 ? 1 : tc < 7 ? 2 : 3;
                phaseClicks[p]++;
                if (n < 4096) times[n++] = tc;
            }
        }
        poll(&pfd, 1, 2);
    }

    // Maximo de clics en cualquier ventana de 1 segundo
    for (int i = 0; i < n; ++i) {
        int c = 0;
        for (int j = i; j < n && times[j] - times[i] < 1.0; ++j) c++;
        if (c > maxPerSecond) maxPerSecond = c;
    }
    // Los clics de la fase 2 en los primeros 100 ms son el margen de deteccion del menu.
    int menuLate = 0;
    for (int i = 0; i < n; ++i) if (times[i] >= 5.1 && times[i] < 7.0) menuLate++;

    printf("clics jugando (1-5 s): %d  -> %.1f CPS\n", phaseClicks[1], phaseClicks[1] / 4.0);
    printf("clics con menu abierto (5.1-7 s): %d\n", menuLate);
    printf("clics jugando otra vez (7-9 s): %d\n", phaseClicks[3]);
    printf("maximo en cualquier segundo: %d\n", maxPerSecond);

    int ok = 1;
    if (phaseClicks[1] < 4 * 12) { printf("FALLO: muy pocos clics jugando\n"); ok = 0; }
    if (maxPerSecond > 16)       { printf("FALLO: mas de 16 clics en un segundo\n"); ok = 0; }
    if (menuLate > 0)            { printf("FALLO: clica con el menu abierto\n"); ok = 0; }
    if (phaseClicks[3] < 2 * 12) { printf("FALLO: no vuelve a clicar al cerrar el menu\n"); ok = 0; }
    printf(ok ? "PRUEBA OK\n" : "PRUEBA FALLIDA\n");
    return ok ? 0 : 1;
}
