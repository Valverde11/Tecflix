# PROMPT.md — Tecflix (V1)
Algoritmos y Estructuras de Datos II (CE 2103), TEC, II Semestre 2026. Proyecto grupal de 4 personas.

## Antes de empezar
- Lee este archivo, `AGENTS.md` (reglas) y `MEMORY.md` (estado actual).
- Si algo no está claro o se contradice, pregunta antes de asumir.

## El proyecto
- Tecflix es un sistema de streaming de video: los usuarios suben, gestionan y reproducen videos desde una aplicación web.
- Componentes: Aplicación web, API de usuarios, API de metadata, API de streaming y Dashboard (OpenTelemetry).
- Ahora solo trabajamos la **versión 1.0** (todo en una sola máquina).
- Enunciado completo: `docs/Proyecto_-_Tecflix.pdf`

## Mi parte
1. **Aplicación web** — ReactJS + TypeScript.
2. **API de usuarios** — REST API en C++.
- Carpetas: `web/` y `users-api/` (confirmar con el equipo).

## Fuera de mi alcance
- API de metadata (C++ + MongoDB) → otro integrante.
- API de streaming (C++ + FFmpeg) → otro integrante.
- Dashboard del sistema → otro integrante.
- Todo lo de la V2.0: redundancia, load-test, CI con 90% de cobertura, Playwright.
- El paper: se redacta **sin IA**.

## Cómo se conecta con lo demás
- **Web → API de usuarios:** registro, inicio de sesión, consulta y actualización de usuarios.
- **Web → API de metadata:** lista de videos paginada y ordenable (título, autor, fecha). El ordenamiento lo hace la API, no la web. Editar metadata y cambiar acceso público/privado.
- **Web → API de streaming:** subir videos y reproducirlos con Shaka Player o Dash.js.
- **Mis dos componentes → Dashboard:** deben emitir traces y métricas con OpenTelemetry.
- Contratos (endpoints, formato JSON, autenticación): por definir con el equipo.

## Requisitos de mi parte

### Aplicación web
- Pantalla inicial con formulario de registro e inicio de sesión.
- Lista de videos: los propios y todos los públicos, con paginación y orden por título, autor y fecha de carga.
- Reproducir un video con un reproductor open source (Shaka o Dash.js).
- Subir, eliminar y editar la metadata de videos, y gestionar si son públicos o privados.
- Cuota de almacenamiento fija por usuario: si se alcanza, no se permite subir más videos.

### API de usuarios
- Operaciones: registrar, actualizar, eliminar y consultar usuarios.
- Usuarios guardados en un **archivo binario**.
- Búsquedas mediante un **árbol B en disco** implementado por nosotros.

## Restricciones
- Paradigma orientado a objetos en el diseño y la implementación.
- Git y GitHub obligatorios. V1.0 y V2.0 van en tags o ramas separadas.
- Metodología spec-driven: el repo debe tener un `SPEC.md` como input de los agentes.
- Se permite IA, pero **debo entender y poder explicar todo el código** en la defensa. Si alguien no puede explicar algo, la nota es cero.
- Cada mala práctica de programación resta 5 puntos.
- Si los componentes no funcionan integrados, la nota es cero.

## Evaluación (de mi parte)
- Aplicación web: 10 puntos.
- API de usuarios: 15 puntos.

## Dudas abiertas
- ¿Qué librería HTTP usaremos para el REST API en C++?
- ¿Cómo será la autenticación (token, sesión)? ¿Dónde se guarda la contraseña y cómo se protege?
- ¿Qué componente controla la cuota de almacenamiento: usuarios, metadata o streaming?
- ¿Cuál es el valor de la cuota?
- ¿Shaka Player o Dash.js?
- ¿Qué campos tiene un usuario y cuál es la clave del árbol B?
- Estructura del repositorio y nombres de carpetas.

## Primera tarea
1. Resume con tus palabras lo que entendiste de mi parte.
2. Lista dudas, riesgos o contradicciones.
3. Propón un plan por fases, sin escribir código todavía.
4. Espera mi aprobación.
