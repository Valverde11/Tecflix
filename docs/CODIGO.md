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

### W0.5 · Rutas

React Router decide qué página mostrar según la URL, sin recargar. Las páginas todavía son provisionales: W2 a W5 las reemplazan.

- **`App` y `AppRoutes` separados**: `App` aporta `BrowserRouter`, que necesita el navegador real; `AppRoutes` solo declara las rutas. Así las pruebas usan `MemoryRouter` y arrancan en cualquier URL.
- **`/` redirige a `/login`**: el enunciado pide que al entrar se vea el formulario. Una ruta desconocida también vuelve a `/`.
- **`replace` en las redirecciones**: evita que el botón Atrás regrese a `/` y vuelva a redirigir sin fin.
- **Rutas protegidas sin guard por ahora**: `/videos`, `/videos/:id` y `/upload` se protegen en W2.3, cuando exista `AuthContext`.

### W0.6 · Variables de entorno

Vite expone al navegador solo las variables que empiezan con `VITE_`; por eso ahí no debe ir ningún secreto. `.env.example` documenta las tres URLs y el `.env` real no se sube.

`src/api/apiConfig.ts` las valida al leerlas. Sin esa validación, una variable faltante produciría peticiones a `undefined/api/users` y un error difícil de rastrear. Con ella, el error nombra la variable. `loadApiConfig` recibe el entorno como parámetro en vez de leer `import.meta.env` por dentro, así las pruebas le pasan cualquier combinación sin alterar el proceso. También quita la barra final de la URL, porque las rutas ya empiezan con `/`.

## W1 · Cliente HTTP y tipos

### W1.1 · `HttpClient` y `ApiError`

`HttpClient` es el único lugar de la web que llama a `fetch`. Hay una instancia por API, cada una con su URL base. Centraliza tres cosas que de otro modo se repetirían en cada llamada:

- **Token**: lo pide a `AuthHandler` en cada petición y lo envía como `Authorization: Bearer`. Recibe esa interfaz, no la sesión concreta, para poder probarlo sin ella.
- **Errores**: todo fallo sale como `ApiError` con `status` y `code`. Las pantallas deciden por `code`, que es estable, y no por el texto. Si el servidor responde sin JSON (un proxy caído) se usa `http_error`; si no hubo respuesta, `network_error` con status 0; si un 2xx trae algo inesperado, `invalid_response`.
- **Sesión vencida**: un 401 avisa a `onUnauthorized`, pero **solo si la petición llevaba token**. La API de usuarios también responde 401 a una clave incorrecta en el login, y eso no debe tratarse como sesión vencida.

**Parsers en vez de `as T`:** cada lectura recibe una función que valida el JSON. `SPEC.md` exige validar toda entrada externa. Además, un `as T` no comprueba nada en ejecución: si otra API cambia un campo, la interfaz mostraría `undefined` sin explicación. Con el parser falla de inmediato nombrando el campo.

### W1.2 · Tipos y clientes por API

`src/types/` tiene los modelos de `SPEC.md` y `UsersApi`, `MetadataApi` y `StreamingApi` traducen cada endpoint a un método con tipos. Reciben un `HttpClient`, así que ninguna conoce `fetch`.

- **`JsonObject`**: lee un JSON de tipo `unknown` campo por campo y lanza nombrando el que no cumple. Sustituye a una librería de validación: son pocos modelos y no justifican una dependencia.
- **Los parsers detectan diferencias de contrato**: metadata responde hoy en snake_case (`uploaded_at`) y `SPEC.md` pide camelCase. Una prueba confirma que eso da un error que nombra `uploadedAt`, en vez de una fecha vacía en pantalla.
- **`listVideos` solo reenvía `page`, `pageSize`, `sortBy` y `order`**: el enunciado asigna a metadata ordenar y paginar. La web no toca la lista.
- **`encodeURIComponent` en los ids**: el `videoId` puede venir de la URL del navegador. Sin escaparlo, `../users/1` cambiaría la ruta pedida.
- **`getManifestUrl` no hace ninguna petición**: Shaka Player descarga el manifiesto y los segmentos por su cuenta y solo necesita la dirección.
- **La subida no está aquí todavía**: `fetch` no informa el progreso de una subida, así que W5.2 usará `XMLHttpRequest` para la barra de avance.

### W1.3 · Servidor simulado con MSW

MSW intercepta `fetch` y responde como si existiera la API, así las pruebas recorren el `HttpClient` y los parsers reales sin servicios corriendo (`SPEC.md` 6.4). Los simuladores están en `src/test/handlers.ts` y usan los mismos códigos de error de la API de usuarios real.

- **`onUnhandledFrame: 'error'`**: una ruta sin simular hace fallar la prueba. Así se nota si el código pide algo que nadie contempló.
- **`handlers.test.ts`** comprueba que los simuladores pasan por los parsers; sin eso, un simulador mal escrito se confundiría con un error de la pantalla probada.
- **Solo se usa en pruebas por ahora**: no se publica ningún archivo de MSW en la aplicación.

## W2 · Autenticación

### W2.3 · Sesión y rutas protegidas

Se hizo antes que las pantallas porque ambas dependen de ella.

**`SessionStore`** guarda token y usuario. Es una clase común, fuera de React, porque `HttpClient` también necesita leer el token y borrar la sesión ante un 401, y ese código no está dentro de ningún componente. React se entera de los cambios con `useSyncExternalStore`, el mecanismo oficial para leer estado externo. Esto cumple el papel del `AuthContext` del plan sin duplicar el estado en dos lugares: `useAuth()` lee la tienda y no hay copia que pueda desincronizarse.

**`sessionStorage` y no `localStorage`**: la sesión sobrevive a una recarga, pero muere al cerrar la pestaña, lo que reduce la exposición de un token de 8 h en una computadora compartida. Ningún almacenamiento del navegador protege contra XSS; la defensa es que React escapa el contenido y no se inserta HTML crudo. Al restaurar, el dato se valida con el mismo parser de la API y, si está alterado, se descarta.

**`createServices`** arma todos los objetos una sola vez en `main.tsx` y `ServicesContext` los reparte. No hay variables globales: cada prueba crea sus propios servicios y ninguna contamina a otra. También vacía la caché de TanStack Query cuando la sesión termina, sea por cierre manual o por token vencido, para que otra persona en el mismo navegador no vea datos privados.

**Guards**: son rutas padre que deciden antes de mostrar a sus hijas. `RequireAuth` envía a `/login` y guarda en `state.from` a dónde iba la persona. `RedirectIfAuthenticated` saca de login y registro a quien ya tiene sesión y la devuelve a ese destino. Por eso las pantallas no navegan al terminar: guardan la sesión y el cambio de estado dispara la redirección. El mismo mecanismo cubre la sesión vencida: un 401 limpia la tienda y `RequireAuth` redirige sin código adicional.

`global.css` define los colores, las fuentes y los espacios como variables para que ningún componente repita esos valores.

### W2.2 · Inicio de sesión

La pantalla se divide en tres piezas para cumplir la regla de `SPEC.md` de mantener la lógica en hooks:

- **`useForm`**: valores y errores de un formulario. Es genérico, porque lo reutilizan el registro y luego la edición de videos. Al escribir en un campo se borra su error: el mensaje ya no describe lo que hay escrito.
- **`useLoginForm`**: valida y envía con `useMutation` de TanStack Query, que da gratis los estados de carga y error. Al terminar guarda la sesión; la redirección la hace `RedirectIfAuthenticated`.
- **`LoginPage`**: solo dibuja.

**Mensajes**: `authErrors.ts` traduce por `code` y no por texto. El error de login es el mismo para usuario inexistente y contraseña incorrecta porque la API lo diseñó así para no revelar qué usuarios existen; la web mantiene esa decisión.

**Accesibilidad**: `TextField` enlaza etiqueta, campo y mensaje con `htmlFor`, `aria-invalid` y `aria-describedby`, así un lector de pantalla anuncia el error al entrar al campo. Las pruebas buscan por esos atributos, de modo que un cambio que rompa la accesibilidad también rompe la prueba.

**El botón se desactiva mientras espera**: el hash de la contraseña tarda cerca de un segundo y un doble clic mandaría dos peticiones.

**Diseño**: el panel de marca imita una carta de ajuste, la imagen clásica de prueba de video, con barras de color arriba y banda oscura abajo. Las barras son un único `linear-gradient`, sin imágenes, y su animación se apaga con `prefers-reduced-motion`. Las fuentes vienen de Google Fonts; si no cargan, se usa la del sistema.
