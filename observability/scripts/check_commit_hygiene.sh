#!/usr/bin/env bash
# Auditoría NO destructiva de rutas versionadas. No lee contenido ni secretos.
set -euo pipefail
if ! git rev-parse --show-toplevel >/dev/null 2>&1; then
  echo 'Ejecutar dentro del repositorio Git.' >&2; exit 2
fi
cd "$(git rev-parse --show-toplevel)"
fail=0
while IFS= read -r -d '' file; do
  case "/$file" in
    */.env|*/.env.local|*/.env.production|*/.env.development|*/quarantine/*|*/data/*|*/node_modules/*|*/dist/*|*/build/*|*/.mongo-data/*|*/mongodb-data/*|*/vcpkg_installed/*|*/.docker-data/*|*/secrets/*|*/credentials/*|*/.DS_Store|*/__MACOSX/*)
      echo "REVISAR: $file"; fail=1;;
    */segment_*.ts|*.m3u8|*.mp4|*.mov|*.mkv|*.profraw|*.profdata|*.pem|*.p12|*.pfx|*.key)
      echo "REVISAR: $file"; fail=1;;
  esac
done < <(git ls-files -z)
if ((fail)); then
  echo 'Se encontraron rutas rastreadas que requieren revisión manual; no se modificó nada.' >&2
  exit 1
fi
echo 'OK: no se encontraron rutas rastreadas que coincidan con las categorías de riesgo.'
echo 'NOTA: esta comprobación por nombre NO detecta secretos dentro de archivos permitidos.'
