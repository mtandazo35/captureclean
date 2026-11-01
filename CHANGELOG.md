# Cambios

## 3.3.0

Reescrita sobre el código de la 2.10.1, recuperando las funciones de la 3.2.8.

**Funciones de la 3.2.8 recuperadas:** captura de ventana activa y de escritorio completo, lupa, pixelar, numerar, seleccionar y mover, colores rápidos, tamaños de trazo, resaltador y texto, negrita, guardado automático en PNG o JPEG, confirmación al cerrar con `Esc`, ventana de preferencias e inicio con Windows configurable. Se conservan las preferencias ya guardadas.

**Novedades:**

- Elegir una ventana o un monitor con un clic.
- Repetir la última zona.
- Rehacer.
- Reeditar un texto con doble clic; texto de varias líneas y pegar con `Ctrl+V`.
- Mover y borrar cualquier objeto, no solo el texto.
- Mover con las flechas.
- Guardar como JPEG desde el diálogo, que además recuerda la última carpeta.
- Clic derecho para cancelar.

**Correcciones:**

- Con la aplicación abierta, el acceso directo ya abre una captura.
- El icono de la bandeja vuelve si se reinicia el Explorador, y se reintenta si el sistema aún no está listo al iniciar sesión.
- Avisa si otro programa ocupa `Impr Pant`.
- Avisa si no se puede guardar. El guardado usa un archivo temporal, de modo que un fallo no deja una imagen rota.
- Barras, trazos y letras se adaptan al DPI de cada monitor.
- Los dibujos respetan su orden: lo trazado sobre un texto queda encima.
- Los dibujos quedan igual de suaves que en la vista previa, y el resaltador ya no se oscurece en las uniones.
- Deshacer ya no copia la imagen de todo el escritorio: gasta una fracción de la memoria y permite 50 pasos.
- Un clic sin efecto ya no gasta un paso de Deshacer.
- El texto no salta de sitio al confirmarlo, porque se edita con el mismo dibujado del resultado final.
- `Esc` cancela el texto en vez de confirmarlo.
- Las barras se colocan dentro del monitor de la selección, nunca en zonas muertas entre monitores de distinto tamaño.
- Un clic o arrastre accidental ya no cierra la captura.

**Instalador:**

- Asistente en español, con casillas para el acceso directo, el inicio con Windows y abrir la aplicación al terminar.
- Detecta la versión instalada: actualiza en la misma carpeta, conserva lo que tenías elegido y avisa antes de instalar una versión más antigua.
- Comprueba que el sistema sea Windows 10 1703 o posterior de 64 bits.
- Cierra la aplicación de forma ordenada y, si no puede reemplazarla, lo explica en vez de mostrar un error técnico.
- Al desinstalar pregunta si también se borran las preferencias; las capturas no se tocan nunca.
- La instalación silenciosa (`/S`) no abre la aplicación, salvo con `/ABRIR`.
- Funciona con espacios en la ruta, sin barras dobles en el registro, y muestra el tamaño en Aplicaciones de Windows.
- Cada versión se publica con su SHA256.

**Proyecto:** se elimina el servidor de subida, que no usaba el cliente; la versión se define en un solo archivo; y las pruebas comprueban comportamiento real.
