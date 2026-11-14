# Cómo incorporar el paquete de cierre V1

Este paquete se preparó a partir de `Tecflix-1test.zip` aportado antes de confirmar los cuatro jobs verdes. **No es una copia en vivo del repositorio remoto.** Revisar los diff antes de incorporar, especialmente si los compañeros han modificado README o workflow después.

1. Descomprimir el ZIP del paquete fuera de la carpeta del proyecto. Copiar su contenido **manteniendo las rutas** dentro de la raíz de Tecflix. No reemplazar de forma automática archivos que tengan cambios más recientes.
2. Revisar: `git diff -- .github/workflows/tecflix-v1-ci.yml README.md Aplicacion_Web/README.md Documentacion/SPEC_INTEGRATION.md Documentacion/SPEC_DASHBOARD.md`.
3. Ejecutar `bash scripts/check_commit_hygiene.sh`, `git diff --check` y `bash tecflix_v1_verification.sh` desde la raíz (con builds locales existentes).
4. Realizar `git add` exclusivamente de los archivos de documentación, script y workflow previstos; inspeccionar `git diff --cached --stat`.
5. Hacer commit y push a `V1test`; en GitHub comprobar que continúan verdes los **cuatro jobs anteriores** y revisar los tres casos del **nuevo job matricial Telemetry ON**. Son comprobaciones nuevas y no deben declararse aprobadas hasta ejecutarlas.
6. Ejecutar la lista `Documentacion/VALIDACION_FINAL_V1.md` para el E2E real y adjuntar evidencia sin secretos.

**Atención:** `TECFLIX_REAL_TELEMETRY=ON` activa el enlace con `tecflix_telemetry`, mientras `TECFLIX_ENABLE_TELEMETRY=ON` selecciona SDK OTLP real y no el stub. El nuevo job fija ambos en ON y compila solamente binarios; los tests de los otros cuatro jobs permanecen intactos. Descarga OpenTelemetry C++ desde GitHub en el runner y puede tardar o fallar por dependencias/toolchain.
