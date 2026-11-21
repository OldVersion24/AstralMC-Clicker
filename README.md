# AstralMC Clicker

Autoclicker oficial del servidor **AstralMC** (`Astralmc.cc`) para Minecraft Java en **Windows** y **Linux**.

🌐 **Página:** https://oldversion24.github.io/AstralMC-Clicker/

⬇️ **Descargas:**
- Windows: [AstralClicker.exe](https://github.com/OldVersion24/AstralMC-Clicker/releases/latest/download/AstralClicker.exe)
- Linux: [AstralClicker-linux](https://github.com/OldVersion24/AstralMC-Clicker/releases/latest/download/AstralClicker-linux)

## Qué hace (y qué no)

Es un autoclicker **limitado**, hecho para cumplir las reglas del servidor. No tiene ningún otro módulo.

- **Solo clic izquierdo**, y solo mientras mantienes pulsado el botón del ratón.
- **Máximo 16 CPS**, siempre. El slider va de 1.0 a 16.0 CPS y además hay un límite duro: nunca más de 16 clics (contando tu clic real) en cualquier segundo.
- **Aleatoriedad fija** de ±0.5 CPS por clic.
- **Solo funciona con Minecraft enfocado** (ventana del juego o proceso Java).
- **No clica en menús**: inventario, cofres, aldeanos, chat… se bloquea automáticamente.
- **Click on inventory no está permitido**: en AstralMC el autoclick en inventarios (refill de pociones) es baneable. Solo PvP.
- **Whitelisted hotbar slots**: eliges en qué slots de la hotbar (1–9) funciona. El slot se sigue con las teclas 1–9 y la rueda del ratón.
- **Enabled** + **Bind**: se activa con la casilla o con una **tecla que tú eliges**.

Lo que **no** tiene: reach, aim, macros, inyección en el juego, conexión a internet, escritura de archivos ni cambios en el sistema. Cada versión es un único archivo de código que puedes revisar.

## Uso en Windows

1. Abre `AstralClicker.exe`.
2. Pulsa **Bind** y elige la tecla de activación (Esc = ninguna).
3. Ajusta los **clicks per second** y marca los **slots de la hotbar** donde quieres que funcione.
4. Con Minecraft enfocado, pulsa la tecla (o marca **Enabled**) y mantén el clic izquierdo.
5. Al abrirlo empieza en el slot 1: si no coincide con el juego, pulsa un número para sincronizarlo.

> Windows SmartScreen puede avisar porque el .exe no está firmado. Pulsa "Más información" → "Ejecutar de todas formas".

## Uso en Linux

1. Descarga `AstralClicker-linux`.
2. Dale permiso de ejecución una vez: clic derecho → Propiedades → "Permitir ejecutar como programa", o en la terminal:
   ```bash
   chmod +x AstralClicker-linux
   ```
3. Ábrelo con doble clic o con `./AstralClicker-linux`. Se usa igual que en Windows.

**Compatibilidad:**
- Funciona en sesiones **X11** y en **Wayland** con Minecraft normal (el juego corre por XWayland, que es lo predeterminado).
- **No funciona** si fuerzas Minecraft a Wayland nativo: Wayland no deja que otros programas cliquen en él.
- Necesita Ubuntu 22.04 o más nuevo, o una distro equivalente (Debian 12, Fedora, Arch, Mint 21…). Usa librerías que ya vienen con el escritorio (X11 y cairo).

## Verificación

Los programas de la Release los compila **GitHub Actions** directamente desde este repositorio (ver [`.github/workflows/build.yml`](.github/workflows/build.yml)). El archivo `SHA256SUMS.txt` de cada Release tiene los SHA-256.

La compilación de Linux también se prueba sola: abre la ventana en una pantalla virtual y comprueba, contra una ventana que imita a Minecraft, que clica, que nunca pasa de 16 CPS y que se para con un menú abierto.

| Carpeta | Contenido |
|---|---|
| [`windows/`](windows/) | Código de Windows (`AstralMC_Clicker.cpp`) |
| [`linux/`](linux/) | Código de Linux (`astral_clicker.cpp`) |

Compilarlo tú mismo:

```bash
# Windows (MinGW-w64 / MSYS2 UCRT64)
g++ -std=c++17 -O2 -s -municode -mwindows -static -static-libgcc -static-libstdc++ windows/AstralMC_Clicker.cpp -o AstralClicker.exe -lcomctl32 -lwinmm -luxtheme -lgdiplus -luser32 -lgdi32

# Linux (Debian/Ubuntu: sudo apt install g++ pkg-config libx11-dev libxtst-dev libxi-dev libxfixes-dev libcairo2-dev)
g++ -std=c++17 -O2 -s linux/astral_clicker.cpp -o AstralClicker-linux $(pkg-config --cflags --libs x11 xtst xi xfixes cairo) -pthread -static-libstdc++ -static-libgcc
```

## Créditos

Programa creado por el staff de AstralMC ([Yinoff/Clicker4Astral](https://github.com/Yinoff/Clicker4Astral)).

⚠️ Descárgalo solo desde este repositorio o el original. Copias de otros sitios pueden estar modificadas.
