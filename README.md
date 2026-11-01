# CaptureClean

Capturador de pantalla para Windows 10/11 x64. Nativo (C++ y GDI+), sin publicidad, sin .NET y sin ningún código de red: nada sale del equipo.

## Uso

| Acción | Cómo |
|---|---|
| Capturar un área | `Impr Pant`, `Ctrl+Mayús+S`, doble clic en la bandeja o abrir el acceso directo |
| Ventana activa | `Alt+Impr Pant` o menú de la bandeja |
| Repetir la última zona | `Mayús+Impr Pant` o menú de la bandeja |
| Escritorio completo | Menú de la bandeja, o `Ctrl+A` durante la captura |
| Elegir una ventana o monitor | Pasar el puntero y hacer un clic, sin arrastrar |

Durante la captura:

- **Selección:** se redimensiona con los tiradores, se mueve con *Seleccionar y mover* o con las flechas (`Mayús` = 10 px).
- **Herramientas:** lápiz, línea, flecha, rectángulo, elipse, texto, resaltador, pixelar y numerar. Todo lo dibujado es un objeto que se puede mover o borrar (`Supr`) con *Seleccionar y mover*.
- **Texto:** se escribe sobre la captura. `Intro` confirma, `Mayús+Intro` añade una línea, `Esc` cancela, `Ctrl+V` pega y el doble clic vuelve a editarlo.
- **Teclas:** `Ctrl+C` copia y cierra, `Ctrl+S` guarda, `Ctrl+Z` deshace, `Ctrl+Y` rehace, `Esc` cierra. Mantener `Ctrl` al soltar la selección copia directamente.

Las preferencias (bandeja → *Preferencias…*) se guardan en `HKCU\Software\CaptureClean`, la misma clave que las versiones 3.x, de modo que se conservan al actualizar.

## Instalar

Ejecuta `CaptureClean-Setup-<versión>.exe`. No necesita permisos de administrador y requiere Windows 10 1703 o posterior de 64 bits. Para comprobar que el archivo no se alteró, compara `certutil -hashfile CaptureClean-Setup-<versión>.exe SHA256` con el archivo `.sha256` que lo acompaña.

Instalación desatendida: `/S` silenciosa, `/ABRIR` abre la aplicación al terminar, `/D=C:\carpeta` elige la carpeta (siempre al final y sin comillas), y solo se aplica si no hay otra instalación. Para desinstalar en silencio: `Uninstall.exe /S`, que conserva las preferencias.

## Compilar

Desde Linux, con `mingw-w64` y `nsis`:

```sh
bash build.sh
```

El resultado queda en `dist/`. La versión se cambia en un solo sitio: `src/version.h`.

## Estructura

| Archivo | Contenido |
|---|---|
| `src/overlay.cpp` | Ventana de captura: selección, objetos, dibujo y exportación |
| `src/main.cpp` | Bandeja, atajos, instancia única |
| `src/preferences.cpp`, `src/settings.cpp` | Ventana de preferencias y registro |
| `src/imageio.cpp` | Portapapeles, PNG y JPEG |
| `src/logic.h` | Lógica pura, sin ventanas |
| `tests/unit.cpp` | Pruebas de `logic.h` |
| `tests/ui_test.cpp` | Prueba de extremo a extremo: maneja la aplicación real con ratón y teclado |

Las pruebas se ejecutan en CI (`.github/workflows/build.yml`) en un Windows desechable, porque cambian las preferencias y el portapapeles del equipo. `ui_test.exe` no debe ejecutarse en un equipo de uso diario.

## Historia

La base es el código de la versión 2.10.1. Las funciones de la 3.2.8, de la que no se conserva el código, se reimplementaron usando su ejecutable como referencia de comportamiento. Ver [CHANGELOG.md](CHANGELOG.md).
