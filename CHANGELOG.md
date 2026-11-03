# Cambios

## 3.3.4

- Windows 11: el instalador ofrece, marcada por defecto, la opción de usar `Impr Pant` para CaptureClean en lugar de Recortes de Windows. Al desinstalar, o al desmarcarla, el ajuste de Windows vuelve a como estaba.
- Si `Impr Pant` está ocupada al arrancar, CaptureClean la toma en cuanto queda libre, sin tener que reiniciarlo.

## 3.3.3

- El instalador oficial lo compila y lo publica GitHub Actions a partir del código de cada etiqueta, después de pasar las pruebas. La aplicación no cambia respecto a la 3.3.2.

## 3.3.2

- El formato de texto pasa a una mini barra **B I U** junto al botón Texto. Queda abierta mientras se escribe, marca lo activo y no interrumpe la escritura. También aparece al seleccionar un texto.
- Si el Explorador tarda en aceptar iconos tras reiniciarse, el icono de la bandeja se reintenta en vez de perderse.

## 3.3.1

- Formato de texto: negrita, cursiva y subrayado, combinables. Se eligen en el menú *Tamaño y formato* con la herramienta Texto, con `Ctrl+B`, `Ctrl+I` y `Ctrl+U`, o en Preferencias. Se aplican al texto que se escribe, al seleccionado o, si no hay ninguno, a los textos siguientes.

## 3.3.0

Versión reescrita, con el código reorganizado y pruebas automáticas.

**Funciones:** captura de ventana activa y de escritorio completo, lupa, pixelar, numerar, seleccionar y mover, colores rápidos, tamaños de trazo, resaltador y texto, negrita, guardado automático en PNG o JPEG, confirmación al cerrar con `Esc`, ventana de preferencias e inicio con Windows configurable. Se conservan las preferencias ya guardadas.

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
