# CaptureClean

Capturador local de pantalla para Windows x64 construido desde cero, sin componentes de Lightshot, publicidad ni conexión a servidores. La interfaz 2.10.1 mantiene selección y edición directamente sobre el escritorio, inspirada en el flujo de Lightshot pero con código propio.

## Cliente

- Al iniciar queda discretamente en la bandeja. Usa doble clic en bandeja, `Impr Pant` o `Ctrl+Shift+S` para capturar.
- La pantalla se oscurece, muestra dimensiones y al soltar el ratón aparece una barra flotante integrada.
- Editor directo y local: lápiz, línea, flecha, elipse, rectángulo, marcador translúcido, texto editable, selector de color y deshacer.
- Copiar desde el botón o con `Ctrl+C` coloca la imagen en el portapapeles y cierra la captura.
- Guardar desde el icono de disquete o con `Ctrl+S` abre el selector nativo, escribe un PNG y cierra la captura.
- La imagen se publica como DIB estándar de Windows para pegarla en Paint, Word, navegadores y mensajería.
- La versión aparece en el nombre de las descargas y en el menú de la bandeja para identificar con certeza el ejecutable activo.
- Interfaz 2.7: barra translúcida estilo cristal, estados suaves, icono de copiar destacado, etiquetas al pasar el puntero y contorno de selección semitransparente.
- Ajuste 2.7.1: barra reducida de 44 a 36 píxeles por botón, fondo más legible e iconos vectoriales reescalados y centrados.
- Versión 2.8: icono multirresolución propio integrado en aplicación, bandeja, accesos directos, instalador y desinstalador; barra con degradado y botón Copiar destacado.
- Versión 2.9: barra vertical marfil para dibujo y barra inferior separada con Copiar, Guardar y Cerrar; ambas se recolocan para no salirse de la pantalla.
- Versión 2.9.1: corrige la primera captura sobredimensionada después de estar inactivo mediante DPI Per-Monitor V2 incrustado, límites físicos de monitores y estabilización del disparo de `Impr Pant`.
- Versión 2.10.0: escritura directamente sobre la captura con fondo transparente; al terminar, el texto queda seleccionado y puede moverse arrastrándolo. Copiar y Guardar fusionan los objetos de texto en la imagen final y Deshacer restaura también sus posiciones.
- Versión 2.10.1: conserva las nueve herramientas y las tres acciones sin interferencias; mover texto solo se activa con la herramienta Texto, permitiendo dibujar normalmente sobre textos con las demás herramientas.
- Mantener `Ctrl` al terminar la selección copia y cierra directamente; `Ctrl+A` selecciona toda la pantalla y `Esc` o `Ctrl+X` cancela.
- La bandeja queda reducida a **Nueva captura** y **Salir**.
- La superposición usa doble búfer para eliminar el parpadeo al seleccionar o dibujar.
- La imagen base se conserva en un contexto GDI y se copia directamente en cada cuadro; esto evita franjas y bloqueos en pantallas de alta resolución.
- Todos los iconos de la barra se dibujan como vectores antialias y ya no dependen de símbolos instalados en Windows.
- No contiene código de red, configuración de servidor ni subida de archivos.
- El ejecutable es nativo y no requiere .NET.

## Compilar el cliente

```sh
x86_64-w64-mingw32-g++ -std=c++17 -O2 -static -municode -mwindows client/CaptureCleanV2.cpp -o dist/CaptureClean.exe -lgdiplus -lcomdlg32
makensis installer/CaptureClean.nsi
```

## Alcance de validación

Se verifica compilación reproducible, formato PE, contenido del instalador y ausencia de dependencias de red. Una certificación formal requiere firma de código y auditoría independiente en Windows 10/11.
