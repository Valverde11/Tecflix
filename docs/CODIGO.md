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

### W0.2 · ESLint y Prettier

ESLint revisa errores de lógica y Prettier solo el formato. `eslint-config-prettier` apaga las reglas de ESLint que chocarían con Prettier, para que cada herramienta haga una sola cosa.

- **`strictTypeChecked`**: usa la información de tipos del compilador, así detecta problemas que el análisis por sintaxis no ve, como una promesa sin esperar.
- **`no-explicit-any` y `no-console` como errores** y `--max-warnings 0`: `SPEC.md` los prohíbe y cada mala práctica resta 5 puntos, así que deben detener el lint.
- **`endOfLine: auto`**: Git convierte a CRLF en Windows y a LF en Linux. Sin esta opción, Prettier marcaría todos los archivos en una de las dos máquinas.
- **`overrides` de `minimatch` a 11.0.5**: la versión 10 tiene un aviso de denegación de servicio (GHSA-g3hv-75wg-m249). `npm audit fix --force` lo resolvía bajando ESLint de versión mayor; forzar solo este paquete deja `npm audit` limpio. Solo afecta a herramientas de desarrollo, no al código que llega al navegador.

### W0.3 · Pruebas con Vitest

Vitest reutiliza la configuración de Vite, así que las pruebas compilan igual que la aplicación. `jsdom` simula el navegador en Node y Testing Library renderiza los componentes y los busca como lo haría una persona: por rol y texto, no por clases CSS.

- **`src/test/setup.ts`**: agrega comparadores como `toBeInTheDocument` y limpia el DOM después de cada prueba, para que ninguna dependa de lo que dejó la anterior.
- **Imports explícitos de `vitest`**: se usan `describe` e `it` importados, no globales, así cada archivo declara de dónde viene lo que usa.
- **Umbral de cobertura del 80 %**: es la meta de `SPEC.md` para la V1.0. `npm run coverage` falla si baja, de modo que la cobertura se mantiene durante el desarrollo y no se persigue al final. `main.tsx` queda fuera porque solo conecta React con el HTML.

### W0.4 · Estructura de carpetas

- `src/api/`: el único lugar con llamadas HTTP (regla de `SPEC.md`). Si cambia un contrato, solo se toca esta carpeta.
- `src/features/{auth,videos,player,upload}/`: el código se agrupa por función, no por tipo de archivo; lo que cambia junto vive junto.
- `src/components/`, `src/hooks/`, `src/types/`: lo que comparten varias funciones.

Git no guarda carpetas vacías, por eso llevan un `.gitkeep` que se borra al agregar el primer archivo real.
