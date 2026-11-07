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

### W2.1 · Registro

`registerValidation.ts` repite en la web las reglas de `UserValidator`. La API sigue siendo quien decide; la copia evita un viaje al servidor (cada registro calcula un hash que tarda cerca de un segundo) y permite explicar el problema junto al campo.

- **La contraseña se mide en bytes**: el servidor usa el tamaño en UTF-8. En JavaScript `'ñ'.length` es 1, pero ocupa 2 bytes; medir con `length` dejaría pasar contraseñas que el servidor rechaza.
- **El correo usa un rango ASCII**: `[!-?A-~]` son los caracteres visibles sin `@`, igual que la comprobación de `IsValidEmail`.
- **Los errores del servidor van a su campo**: `FIELD_BY_ERROR_CODE` asocia `username_taken` con el usuario y `email_taken` con el correo. El resto de errores aparece como mensaje general.
- **La confirmación de contraseña nunca sale de la web**: se usa para detectar errores de tecleo y no forma parte del contrato.
- **Tras registrarse se inicia sesión automáticamente**: `POST /api/users` no devuelve token, así que se llama al login con los datos recién escritos.

Limitación conocida: si el registro funciona pero el login posterior falla por red, la persona ve el error de conexión y, al reintentar, "ese usuario ya existe". Basta con entrar desde "Iniciar sesión".

### W2.4 · Pruebas de autenticación

Las pruebas de cada pantalla se escribieron con ella y montan la aplicación completa con `renderApp`: rutas, guards, TanStack Query y `HttpClient` reales, y solo la red simulada. Así prueban el comportamiento que ve la persona y no detalles internos. Esta tarea agrega la prueba directa de `authErrors`, cuya rama de error desconocido no se alcanza desde las pantallas.

### W2.5 · Conexión con la API de usuarios real

Se recorrió el flujo en Chrome contra `user-service` real, con datos temporales y secretos de un solo uso: 17 comprobaciones, entre ellas registro, recarga, cierre de sesión, clave incorrecta, usuario duplicado sin distinguir mayúsculas, token alterado, CORS y ausencia de scroll horizontal en móvil. Esto cubre lo que MSW no puede: CORS solo existe en un navegador.

La prueba encontró dos fallos que las pruebas simuladas no vieron:

- **Se perdía el destino**: quien iba a `/upload`, llegaba al login y pasaba a "Crear cuenta" terminaba en `/videos`, porque un `Link` no conserva el `state`. Ahora los enlaces entre login y registro lo pasan.
- **Faltaba el favicon**: el navegador pedía `/favicon.ico` y recibía 404. Se agregó `public/favicon.svg`, unas barras de ajuste en miniatura.

## W3 · Lista de videos

### Metadata simulada mientras no esté integrada

`src/test/videoCatalog.ts` es un catálogo de 25 videos y `queryCatalog` ordena y pagina como lo hará la API de metadata según `SPEC.md`. Está en la carpeta de pruebas porque representa al servidor: la web de verdad nunca ejecuta esa lógica.

Con `VITE_MOCK_METADATA=true`, `main.tsx` activa esos mismos simuladores en el navegador durante el desarrollo. Solo se intercepta metadata; lo demás sale a la red, así que la API de usuarios sigue siendo la real. El plugin `msw/vite` sirve el worker en desarrollo y la comprobación de `import.meta.env.DEV` permite que el build lo elimine: se verificó que `dist/` no contiene ni el worker ni el catálogo.

### W3.1 · Página de listado

`useVideoList` consulta con `useQuery` de TanStack Query. La clave de caché incluye los parámetros (`['videos', query]`), así que cada combinación de página y orden se guarda por separado y volver a una página ya vista es inmediato. `keepPreviousData` deja la página anterior a la vista mientras llega la nueva.

La web solo reenvía `page`, `pageSize`, `sortBy` y `order` y dibuja `items` tal como llegan. Una prueba responde títulos desordenados a propósito y comprueba que se muestran en ese mismo orden: si alguien agregara un `sort` en la web, fallaría. La API ya devuelve propios y públicos juntos, y por eso la web tampoco filtra.

Las consultas no se reintentan solas (`retry: false`): repetir un 4xx no cambia nada y los reintentos retrasarían varios segundos el mensaje de error.

### W3.4 · Fila de video

El contrato no incluye miniaturas, así que una cuadrícula de tarjetas tendría recuadros vacíos. `VideoCard` se dibuja como una fila de guía de programación: duración grande, título y datos, y a la derecha el acceso y el estado.

- **El color del filo depende del tema**: `topicColor` calcula un hash del texto y elige un color de la carta de ajuste. No decora: videos del mismo tema se reconocen de un vistazo. El tema también está escrito, porque el color por sí solo no basta para quien no lo distingue.
- **`formatDuration` muestra `--:--` si la duración es 0**: un video recién subido todavía no la tiene.
- **Solo se avisa el estado si no es `ready`**: el caso normal no necesita texto.
- **"Administrar" solo aparece en videos propios**, comparando `ownerId` con el usuario de la sesión. Es comodidad de interfaz: quien protege de verdad es la API, que debe rechazar los cambios de quien no es dueño. Lleva a la página del video, donde W5 pondrá editar, cambiar acceso y borrar, para no mostrar botones que aún no hacen nada.

### W3.2 · Orden y paginación en la URL

`useVideoListParams` lee `?page=` y `?sort=` y arma la consulta. La URL es la fuente de verdad, no un `useState`: por eso recargar, compartir el enlace y el botón Atrás funcionan sin código adicional. Cambiar de página es navegar, y `Pagination` usa enlaces reales.

- **Un solo selector de orden**: cada opción combina campo y dirección ("Título de la A a la Z"). Dos controles permitirían la combinación poco natural "Fecha ascendente" sin explicar qué significa.
- **Los valores de la URL se validan**: cualquiera puede escribir `?page=abc`. Lo inválido vuelve al valor por defecto y no llega a la API.
- **Cambiar el orden vuelve a la página 1**: con otro orden, la página actual muestra otros videos y quedarse ahí no tiene sentido.
- **`totalPages` se calcula con lo que informa la API** (`total` y `pageSize`). Esa cuenta decide qué enlaces mostrar; no pagina datos.

### W3.3 · Estados de carga, vacío y error

`VideoListBody` decide qué mostrar a partir de lo que entrega `useQuery`. Se separó de `VideoListPage` para que el encabezado y el selector de orden sigan visibles en todos los casos.

- **Cargando**: solo la primera vez, cuando no hay datos. Lleva `role="status"` para que un lector de pantalla lo anuncie.
- **Error con reintento**: lleva `role="alert"`, explica el motivo y ofrece reintentar con `refetch`. Si un refresco falla pero ya hay datos, se conservan: es mejor una lista algo vieja que una pantalla de error.
- **Sin videos**: invita a subir el primero.
- **Página sin videos**: `total` mayor que 0 con `items` vacío significa que hay videos, pero no en esa página, algo que pasa al borrar videos o al editar la URL. Se ofrece volver a la primera página conservando el orden.
- **Cambio de página**: la lista anterior queda atenuada y con `aria-busy` hasta que llega la nueva, sin parpadeo.
- **Sesión vencida**: un 401 de metadata limpia la sesión en `HttpClient` y `RequireAuth` redirige. La página no necesita código para este caso; una prueba lo confirma.

También se agregó al encabezado el enlace "Subir video", para llegar a esa pantalla desde cualquier lugar.

### Verificación de W3 en el navegador

Se recorrió la lista en Chrome con la API de usuarios real y metadata simulada: paginar, ordenar, recargar, botón Atrás, página fuera de rango y vista móvil. Esto reveló dos problemas que las pruebas no vieron:

- **Temas distintos con el mismo color**: el primer hash era polinómico con base 31. Como 31 mod 6 es 1, al reducir a 6 colores el resultado era solo la suma de las letras. Cambiar el módulo intermedio no lo resolvía: en un tema corto ese módulo nunca actúa. Por eso se usa FNV-1a, un hash estándar de 32 bits; `Math.imul` multiplica en 32 bits y no desborda con temas largos. Una prueba con anagramas falla con el hash anterior. Con 6 colores siempre habrá coincidencias, y por eso el tema también va escrito.
- **Encabezado partido en móvil**: "Subir video" y "Cerrar sesión" ocupaban dos líneas. Ahora cada texto se mantiene en una línea, el encabezado se reacomoda y un nombre largo se recorta con puntos suspensivos, con el nombre completo en `title`.

## W4 · Reproducción

### W4.1 · `VideoPlayer` con Shaka Player

Un navegador no reproduce HLS por sí solo, salvo Safari. Shaka descarga el manifiesto `.m3u8`, baja los segmentos `.ts` y los entrega al elemento `<video>` mediante Media Source Extensions (MSE). Chrome no acepta MPEG-TS en MSE, así que Shaka convierte cada segmento a MP4 fragmentado con su propio convertidor.

- **`PlaybackSession` encapsula a Shaka**: el resto de la web no conoce su API, y se prueba sustituyendo el módulo porque jsdom no tiene MSE.
- **`destroy()` al desmontar**: un reproductor sin destruir sigue descargando segmentos y reteniendo memoria. El efecto de `VideoPlayer` crea una sesión y su limpieza la detiene. En `StrictMode` React monta dos veces a propósito para detectar fugas: una prueba comprueba que toda sesión iniciada también se detiene.
- **Carga interrumpida**: al salir a mitad de la carga, Shaka rechaza `load()` con un error. `stopped` lo descarta, porque no es un fallo que deba ver la persona.
- **Token por filtro de peticiones**: Shaka hace sus propias peticiones y no pasa por `HttpClient`, así que `registerRequestFilter` agrega `Authorization` al manifiesto y a cada segmento. Sin eso, un video privado no podría protegerse.
- **Errores traducidos**: 409 significa que aún se procesa (así lo define streaming), 404 que el archivo ya no existe, y un fallo de red o de CORS se informa como problema de conexión.
- **Reintentar crea una sesión nueva**: el contador `attempt` es dependencia del efecto. Cambiarlo destruye la sesión fallida y empieza de cero, sin lógica especial de recuperación.

### W4.2 · Página de detalle

`VideoDetailPage` pide la metadata con `useVideo` y resuelve los estados; `VideoDetails` muestra el reproductor y los datos.

- **`VideoPlayer` se carga con `lazy`**: Shaka pesa unos 870 kB y el resto de la web 307 kB. Quien solo inicia sesión o ve la lista no lo descarga. Por eso el límite del aviso de tamaño del build se subió con su justificación: el fragmento grande es intencional.
- **Si el video no está `ready`, no se crea el reproductor**: streaming respondería 409. Se explica el estado en el mismo lugar donde iría la imagen.
- **404 y 403 se muestran igual**: distinguirlos revelaría que existe un video privado ajeno.
- **Límite conocido**: el estado se lee de metadata, pero `SPEC.md` no dice quién lo actualiza cuando streaming termina. Si nadie lo hace, un video quedaría en `queued` para siempre. Hay que definirlo con el equipo.

### Verificación de W4 con video real

Streaming todavía no tiene CORS, así que se generó un HLS real con los mismos parámetros de `FfmpegTranscoder.cpp` y se sirvió con un servidor temporal que imita sus rutas. En Chrome: 13 comprobaciones, entre ellas reproducción (640×360, cuadros decodificados y tiempo avanzando), salto a otro segmento, token en cada petición, fin de las descargas al salir, 409, reintento y ausencia de CORS. Shaka 5 convierte los `.ts` sin `mux.js`.

Esa prueba encontró un bug que las simuladas no podían ver: **"Intentar de nuevo" dejaba la pantalla sin imagen**. `stop()` es asíncrono y la sesión nueva empezaba mientras la anterior se destruía sobre el mismo `<video>`; al terminar, la destrucción le quitaba la fuente. Ahora `previousRelease` encadena las sesiones: cada una espera a que la anterior se libere. Si liberar falla, la siguiente igual puede intentarlo.

## W5 · Gestión de videos

### W5.1 · Indicador de cuota

`useQuota` consulta `GET /api/users/{id}/quota` y `QuotaMeter` lo muestra en la lista y en la subida. No se usan `usedBytes` de la sesión porque se guardaron al iniciar sesión y dejan de ser ciertos al subir o borrar un video.

- **`formatBytes` divide entre 1000**: la cuota se definió como 1 GB = 10⁹ bytes. Con 1024 la pantalla mostraría 0,93 GB.
- **`skipToken`**: dice a TanStack Query que no consulte si no hay usuario, sin usar un id inventado.
- **`role="meter"`**: un lector de pantalla anuncia el resumen en palabras gracias a `aria-valuetext`. Desde el 90 % la barra cambia de color y el texto sigue diciendo el dato exacto.

También se subieron los tiempos de espera de las pruebas: con todos los archivos en paralelo, los flujos que escriben en varios campos superaban el límite de 1 s y fallaban de forma intermitente.

### W5.2 · Subida

La subida sigue el flujo 3 de `SPEC.md`: consultar cuota, enviar el archivo a streaming y crear la metadata con el `videoId` que devuelve.

- **`HttpClient.upload` usa `XMLHttpRequest`**: `fetch` no informa cuánto se ha enviado y la barra de avance lo necesita. Está dentro de `HttpClient` para reutilizar el token, los errores comunes y el manejo de 401. El archivo se envía crudo como `application/octet-stream`, y el navegador agrega el `Content-Length` que streaming exige.
- **Preflight CORS**: esa cabecera y `Authorization` hacen que el navegador pregunte primero con `OPTIONS`. Streaming debe responderlo. jsdom también lo aplica, así que las pruebas simulan esa respuesta.
- **Si la cuota no se conoce, no se sube**: el enunciado prohíbe superarla, y bloquear es el error seguro. La comparación usa el tamaño del archivo original como estimación; el tamaño final transcodificado lo reporta streaming.
- **Deshacer**: si crear la metadata falla, se borra lo subido, porque sin metadata nadie vería ese video y ocuparía espacio para siempre. Si deshacer también falla, se muestra el error original, que es el que explica el problema.
- **Cancelar**: un `AbortController` detiene la subida, y también al salir de la página. Cancelar no se muestra como error.
- **`durationSeconds` y `sizeBytes` los envía la web**: es la única que tiene el archivo cuando se crea la metadata. `readVideoDuration` lee la duración con un `<video>` temporal. Si el navegador no entiende el formato, queda en 0 y la subida sigue.
- **`videoFields.ts` y `VideoFieldsForm`** son comunes a subir y editar. Los límites (título 200, tema 100, descripción 5000, 20 etiquetas) salen de `video_validation.cpp` y se miden en bytes, igual que allá.
- **Privado por defecto**: publicar debe ser una decisión explícita.

`VITE_MOCK_METADATA` pasó a llamarse `VITE_MOCK_VIDEO_APIS`, porque ahora simula metadata y streaming. `FakeVideoStore` guarda los cambios como una base de datos falsa y las pruebas la reinician después de cada caso.
