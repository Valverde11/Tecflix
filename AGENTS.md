# AGENTS.md — Tecflix
Sistema de streaming de video para el curso CE 2103 (TEC). Este agente trabaja solo en la **Aplicación web** y la **API de usuarios** de la V1.0. Objetivo: código funcional, integrado y que yo pueda explicar en la defensa.
**Siempre debes de explicarme en un archivo nuevo que vas a generar y actualizar llamado código que hace esa parte de código que generes y además agregar una explicación en prosa para entender que estamos haciendo**


## Stack y estructura
- `web/` → ReactJS + TypeScript. Reproductor: Shaka Player o Dash.js (por decidir).
- `users-api/` → REST API en C++ (estándar y librería HTTP por decidir).
  - Persistencia en archivo binario + índice con árbol B en disco.
- Observabilidad: OpenTelemetry en ambos componentes.
- `docs/` → enunciado y documentación.
- Los demás componentes (metadata, streaming, dashboard) son de otros integrantes.

## Comandos
- Por definir cuando se cree el proyecto. Deben quedar aquí, exactos y copiables:
  - Web: instalar, ejecutar, probar, lint, compilar.
  - API: compilar, ejecutar, probar.

## Convenciones
- Código (variables, funciones, clases) en inglés; comentarios y textos de la interfaz en español.
- Orientación a objetos: clases con una sola responsabilidad, encapsulamiento, sin variables globales.
- TypeScript estricto: nada de `any`.
- Funciones cortas, nombres descriptivos, sin código duplicado ni comentado.
- Manejo de errores explícito: en C++ validar lectura/escritura del archivo binario; en la API devolver códigos HTTP correctos.
- Archivo de referencia de estilo: por definir con el primer módulo terminado.

## Reglas de dominio / trampas conocidas
- El **árbol B se implementa a mano**: no usar bases de datos ni librerías que lo resuelvan. Es la parte que se evalúa.
- El árbol B vive **en disco**, no solo en memoria: los nodos se leen y escriben en el archivo.
- Registros binarios de tamaño fijo para poder calcular posiciones en el archivo.
- Nunca guardar contraseñas en texto plano.
- El ordenamiento y la paginación de videos los hace la API de metadata, **no** la web.
- La web nunca trae la lista completa de videos: siempre paginada.
- La cuota de almacenamiento no se puede modificar.
- Un usuario solo ve sus videos y los públicos de otros.
- Los contratos con otras APIs se acuerdan con el equipo: no inventes endpoints de otros componentes; si falta uno, pregunta.

## Forma de trabajar
- Planifica antes de tocar código cuando la tarea afecte más de un archivo o cambie un contrato.
- Cambios pequeños y enfocados: una funcionalidad por vez.
- Al terminar, explica qué hiciste, por qué, y las partes difíciles (sobre todo del árbol B) como si me prepararas para la defensa.
- Si hay varias formas de hacer algo, muéstrame las opciones con sus ventajas antes de elegir.

## Memoria
- Al empezar, lee `MEMORY.md` para conocer el estado del proyecto y las decisiones tomadas.
- Al terminar una tarea, actualízalo: estado actual, decisiones importantes (con su porqué) y errores a evitar.
- Mantenlo breve (máximo ~50 líneas): resume o elimina lo que ya no aporte.
- Si algo se convierte en una regla permanente, propón moverlo a `AGENTS.md` en lugar de dejarlo en la memoria.
- No guardes nunca datos sensibles (claves, tokens, datos personales).

## Límites
- ✅ Siempre: seguir las convenciones, manejar errores y escribir pruebas de lo que implementes.
- ✅ Siempre: explicar el código generado para que pueda defenderlo.
- ✅ Siempre: actualizar `MEMORY.md` al terminar cada tarea.
- ⚠️ Pregunta antes: dependencias nuevas, archivos o carpetas nuevos, cambios en el formato del archivo binario, cambios en endpoints o contratos.
- 🚫 Nunca: modificar código de metadata, streaming o dashboard.
- 🚫 Nunca: redactar ni corregir el paper (debe hacerse sin IA).
- 🚫 Nunca: hacer commits, push o merge sin que yo lo pida.
- 🚫 Nunca: subir credenciales, archivos binarios de datos ni `node_modules` al repositorio.

## Verificación
- El código compila sin errores ni warnings.
- Las pruebas pasan (en la API: insertar, buscar, actualizar y eliminar en el árbol B, incluyendo divisiones de nodo y reinicio del programa).
- Los datos persisten tras reiniciar la API.
- La web funciona contra la API real, no solo con datos de prueba.
- Lint sin errores.
