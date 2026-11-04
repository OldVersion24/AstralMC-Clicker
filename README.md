# AstralMC Clicker

Autoclicker oficial del servidor **AstralMC** (`Astralmc.cc`) para Minecraft Java en Windows.

🌐 **Página:** https://oldversion24.github.io/AstralMC-Clicker/
⬇️ **Descarga:** [AstralClicker.exe](https://github.com/OldVersion24/AstralMC-Clicker/releases/latest/download/AstralClicker.exe)

## Qué hace (y qué no)

Es un autoclicker **limitado**, hecho para cumplir las reglas del servidor. No tiene ningún otro módulo.

- **Solo clic izquierdo**, y solo mientras mantienes pulsado el botón del ratón.
- **Máximo 16 CPS**, siempre. El slider va de 1.0 a 16.0 CPS y además hay un límite duro: nunca más de 16 clics (contando tu clic real) en cualquier segundo.
- **Aleatoriedad fija** de ±0.5 CPS por clic.
- **Solo funciona con Minecraft enfocado** (ventana con "Minecraft" en el título o proceso `javaw.exe`/`java.exe`).
- **No clica en menús**: inventario, cofres, aldeanos, chat… se bloquea automáticamente.
- Se activa/desactiva con una **tecla que tú eliges**.

Lo que **no** tiene: reach, aim, macros, inyección en el juego, conexión a internet, escritura de archivos ni cambios en el registro. Es un único archivo de código fuente (`AstralMC_Clicker.cpp`) que puedes revisar.

## Uso

1. Abre `AstralClicker.exe`.
2. Pulsa el botón de tecla y elige la tecla de activación (Esc = ninguna).
3. Ajusta los **clicks por segundo**.
4. Con Minecraft enfocado, pulsa la tecla para activarlo y mantén el clic izquierdo.

> Windows SmartScreen puede avisar porque el .exe no está firmado. Pulsa "Más información" → "Ejecutar de todas formas".

## Verificación

El `.exe` de la Release se compiló directamente desde `AstralMC_Clicker.cpp` de este repositorio. El SHA-256 está en las notas de la Release.

Compilarlo tú mismo (MinGW-w64 / MSYS2 UCRT64):

```bash
g++ -std=c++17 -O2 -s -municode -mwindows -static -static-libgcc -static-libstdc++ AstralMC_Clicker.cpp -o AstralClicker.exe -lcomctl32 -lwinmm -luxtheme -lgdiplus -luser32 -lgdi32
```

## Créditos

Programa creado por el staff de AstralMC ([Yinoff/Clicker4Astral](https://github.com/Yinoff/Clicker4Astral)).

⚠️ Descárgalo solo desde este repositorio o el original. Copias de otros sitios pueden estar modificadas.
