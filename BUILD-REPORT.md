# Informe de compilación y verificación

Fecha: 2026-11-01 UTC

- Cliente 2.3 compilado como PE32+ x86-64 nativo y enlazado estáticamente con MinGW-w64.
- Se sustituyó la vista previa genérica por selección y edición integradas sobre el escritorio; el portable abre la captura al ejecutarse.
- El texto se escribe directamente sobre la selección y la barra flotante usa iconos, estado de herramienta y selector de color.
- El instalador configura el inicio con Windows para el usuario actual y lo elimina al desinstalar.
- Correcciones 2.1: doble búfer para eliminar parpadeo, configuración gráfica del servidor y validación estricta de respuesta JSON/HTTPS.
- Corrección 2.2: captura y repintado base mediante BitBlt/GDI directo para evitar franjas y cursor de espera en monitores de alta resolución.
- Corrección 2.3: sustitución completa de glifos de fuente por iconos vectoriales propios y antialias.
- Corrección 2.4: barra local más amplia y redondeada, estados al pasar el puntero e iconos refinados; eliminadas por completo la subida, la configuración y la biblioteca WinHTTP.
- Corrección 2.5: copiar cierra la superposición; añadidos Ctrl+selección, Ctrl+A y Ctrl+X; el inicio automático ya no abre una captura sobre el instalador.
- Corrección 2.5.1: portapapeles corregido con propietario de ventana, reintentos ante bloqueos y formato CF_DIB compatible.
- Corrección 2.6.0: compilaciones con nombre versionado y versión visible en la bandeja para impedir confusiones con ejecutables anteriores o descargas en caché.
- Interfaz 2.7.0: reducción de la oscuridad exterior, borde y esquinas semitransparentes, barra de cristal y ayudas de herramienta en español.
- Interfaz 2.7.1: reducción del tamaño total de la barra y rediseño uniforme de todos los iconos.
- Interfaz 2.9.0: barra vertical marfil para lápiz, línea, flecha, elipse, rectángulo, marcador, texto, color y deshacer; barra inferior local con copiar, guardar PNG y cerrar.
- Corrección 2.9.1: manifiesto DPI Per-Monitor V2, cálculo del escritorio con EnumDisplayMonitors, tecla Impr Pant sin repetición y captura diferida 60 ms para eliminar la primera captura sobredimensionada tras inactividad.
- Interfaz 2.10.0: editor de texto transparente, objetos de texto independientes y movibles, selección visual, cursor de arrastre, límites dentro de la captura y soporte completo de Deshacer/Copiar/Guardar.
- Corrección 2.10.1: el movimiento de texto queda aislado a la herramienta Texto y se añaden comprobaciones de compilación que exigen conservar las nueve herramientas y las tres acciones existentes.
- Instalador generado con NSIS 3.09, instalación por usuario en `%LOCALAPPDATA%` y desinstalador incluido.
- El ejecutable no contiene funciones de subida, URL de servidor, clave API ni dependencia WinHTTP.
- Búsqueda de cadenas no deseadas: no incluye Lightshot, Skillbrains, Yandex ni componentes del instalador original.

Limitación: este entorno no dispone de Windows 10/11 interactivo. El cliente fue validado por compilación, formato PE e inspección estática; no equivale a una certificación independiente ni a una prueba completa de interfaz en Windows real.
