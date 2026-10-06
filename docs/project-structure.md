# Estructura del proyecto Tecflix

## Componentes

```text
Tecflix/
├── .github/workflows/       # Compilacion, pruebas y artefactos de CI
├── deploy/                  # Configuracion de ejecucion y despliegue
├── docs/                    # Arquitectura, decisiones y resultados tecnicos
├── frontend/                # Aplicacion web React + TypeScript
├── services/
│   ├── common/              # Utilidades HTTP compartidas entre APIs
│   ├── user-service/        # API C++ de usuarios y persistencia binaria
│   ├── metadata-service/    # API C++ de metadata y MongoDB
│   └── streaming-service/   # API C++ de streaming y FFmpeg
├── tests/
│   ├── unit/                # Pruebas unitarias C++
│   ├── integration/         # Pruebas entre servicios y dependencias
│   ├── functional/          # Recorridos web automatizados
│   └── load/                # Pruebas concurrentes y de estres
├── CMakeLists.txt           # Configuracion CMake del monorepo C++
├── CMakePresets.json        # Preset de configuracion local
└── SPEC.md                  # Requisitos y criterios de aceptacion
```

Las carpetas frontend, deploy, integration, functional y load se completaran en
sus respectivas etapas. Por ahora las tres APIs solo exponen `GET /health`;
esto verifica el esqueleto y no representa la funcionalidad final del proyecto.

## Compilar, probar y ejecutar

Requisitos iniciales: CMake 3.20+, Git y un compilador C++17. La configuracion
descarga la version fijada de `cpp-httplib`.

```sh
cmake --preset dev
cmake --build --preset dev --parallel
ctest --preset dev
```

Para generar los ejecutables de entrega, configura una compilacion Release y
usa `cmake --install` con el prefijo `package`, como hace el pipeline de CI.

Los puertos locales iniciales son 8081 (usuarios), 8082 (metadata) y 8083
(streaming). Cada servicio responde en `/health`.

## Orden recomendado de desarrollo

1. Acordar contratos, permisos, formatos de error y criterios verificables en
   `SPEC.md`.
2. Implementar y probar el almacenamiento binario y el arbol B del servicio de
   usuarios.
3. Implementar metadata, consultas paginadas y persistencia MongoDB.
4. Implementar la subida, transcodificacion y entrega compatible con el
   reproductor en el servicio de streaming.
5. Integrar la web y demostrar el flujo completo desde registro hasta
   reproduccion.
6. Instrumentar el sistema y guardar una linea base de rendimiento para v1.0.
7. Implementar redundancia, optimizaciones demostrables y pruebas comparables
   de uno contra dos servidores para v2.0.

No se deben considerar terminadas las etapas hasta probar la integracion del
flujo completo. La cobertura y los objetivos de disponibilidad se mediran con
las pruebas correspondientes, no solo por la configuracion inicial de CI.
