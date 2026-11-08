# Agent instructions
1. Act as an expert in software engineering, modern C++ (C++17 or later), ReactJS with TypeScript, distributed systems and video streaming.
2. Use the model "[model name]".
3. Design and build "Tecflix", a video streaming system, using object-oriented programming in every component. Project for CE 2103 (Algorithms and Data Structures II, TEC, II Semester 2026).
4. Work with spec-driven development: this file is the source of truth. If something is unclear or contradictory, do not silently make it up: record your assumption in "Agent memory".
5. Use the SPEC.md file as memory to store the agent's decisions. For this, use the section called "Agent memory".
6. Before starting any task, read "Agent memory". Every time you make an important decision, add it there in one short line (what you decided and why). Do not modify the "Agent instructions" section.


# Agent memory
- El API usa HLS (manifiesto M3U8 y segmentos MPEG-TS); es el formato de salida elegido para entregar video por bloques.
- La subida es `application/octet-stream` y se transcodifica en segundo plano con FFmpeg; al no estar definidos autenticación ni base de datos, los trabajos se mantienen en memoria y los archivos en almacenamiento local.
- `TECFLIX_FFMPEG` permite indicar la ruta de FFmpeg cuando no está disponible en el `PATH`.
- El código del API se divide entre `include/tecflix`, `src` y `CMakeLists.txt`; los artefactos locales de build y video se excluyen del control de versiones.
- Se utiliza la cabecera cpp-httplib 0.62.0 ya instalada en `third_party/cpp-httplib`, sin descargar una segunda versión durante el build.