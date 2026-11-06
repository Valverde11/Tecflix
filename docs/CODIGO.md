# Explicación del código — Aplicación web

Explicación en prosa de cada parte de `frontend/` (qué hace y por qué), como preparación para la defensa.

## W0 · Base del proyecto

### W0.1 · Vite + React + TypeScript

`frontend/` se creó con la plantilla `react-ts` de Vite. Vite sirve los módulos directamente al navegador durante el desarrollo y empaqueta con `npm run build`, que primero corre `tsc -b`: si hay un error de tipos, no se genera `dist/`.

Cambios respecto a la plantilla y su porqué:

- **`strict: true` declarado en ambos `tsconfig`**: `SPEC.md` lo exige. Se escribe aunque el compilador ya lo traiga por defecto, para que no dependa de la versión.
- **TypeScript 6.0 en vez de 7**: `typescript-eslint` solo admite versiones menores a 6.1, y sin él ESLint no puede analizar TypeScript.
- **Sin oxlint**: la plantilla lo trae, pero `SPEC.md` pide ESLint.
- **Sin `.gitignore` propio**: el de la raíz ya ignora `node_modules/`, `dist/`, `coverage/` y `.env`.
- **`main.tsx` valida `#root`**: la plantilla usa `!` para decirle al compilador que el elemento existe. Aquí se comprueba y, si falta, el error dice qué pasó.
