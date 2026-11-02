Capturador de pantalla para Windows 10 1703 o posterior de 64 bits. Es nativo, no necesita permisos de administrador y no tiene código de red.

Este instalador lo compiló y lo publicó GitHub Actions a partir del código de esta etiqueta, después de pasar las pruebas automáticas.

**Instalar:** ejecuta `CaptureClean-Setup-<versión>.exe`. Si ya tienes CaptureClean, se actualiza en la misma carpeta y conserva tus preferencias y tus capturas.

El instalador no está firmado, así que Windows SmartScreen puede avisar. Para comprobar que el archivo no se alteró, compara `certutil -hashfile CaptureClean-Setup-<versión>.exe SHA256` con el archivo `.sha256`.

