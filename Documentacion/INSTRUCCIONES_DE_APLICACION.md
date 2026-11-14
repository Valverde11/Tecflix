# Aplicación de los ajustes finales

Desde la raíz del repositorio, descomprimir el paquete **en una carpeta temporal** o copiar individualmente sus archivos; revisar antes de sobrescribir documentación existente.

1. Copiar `.github/workflows/tecflix-v1-ci.yml` a la misma ruta dentro de Tecflix.
2. Copiar `Documentacion/RESUMEN_INTEGRACION_V1.md` y `Documentacion/AUDITORIA_MARKDOWN_Y_CI.md`.
3. Revisar las recomendaciones de `AUDITORIA_MARKDOWN_Y_CI.md` contra la versión de tu rama y actualizar README/SPEC con aprobación del equipo.
4. Añadir `/quarantine/` al `.gitignore` raíz (si no está ya).
5. Verificar que metadata está en el árbol fuente de Git, no únicamente como archivo ZIP:
   `git ls-files 'tecflix-metadata/services/metadata-service/CMakeLists.txt'`.
6. Revisar cambios: `git diff --check`, `git status --short`; no subir archivos `.env`, datos de pruebas, videos, cuarentena ni dependencias descargadas.
7. Probar en GitHub Actions al abrir PR; los jobs no se han ejecutado en GitHub desde este entorno.

CI nuevo: Web, Streaming, Usuarios, Metadata en memoria. El workflow existente de observabilidad permanece intacto; MongoDB real y la compilación de las tres APIs con telemetría ON no están cubiertos por el nuevo workflow.
