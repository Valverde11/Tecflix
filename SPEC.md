# Especificación Técnica del Proyecto: Tecflix (Spec-Driven Development)

## 1. Visión General
Tecflix es un sistema distribuido de streaming de video diseñado para la plataforma web, compuesto por un frontend reactivo, múltiples servicios backend en C++ organizados en microservicios/APIs, almacenamiento estructurado y binario, observabilidad completa y mecanismos de alta disponibilidad[cite: 1, 2, 3].

---

## 2. Stack Tecnológico General
- **Frontend Web:** ReactJS 18+ con TypeScript.
- **Reproducción de Video:** Reproductor open-source (Shaka Player o Dash.js) compatible con protocolos MPEG-DASH / HLS[cite: 2].
- **Backend APIs:** C++ (C++17/C++20) para API de Usuarios, API de Metadata y API de Streaming[cite: 2].
- **Base de Datos y Almacenamiento:** 
  - Archivos binarios estructurados en disco con **Árbol B** indexado para el API de Usuarios[cite: 2].
  - **MongoDB** para el API de Metadata[cite: 2].
  - Sistema de archivos local / Red distribuida para bloques transcodificados de video[cite: 2].
- **Procesamiento de Video:** FFMPEG (transcodificación de formatos y fragmentación)[cite: 2].
- **Observabilidad:** OpenTelemetry (Traces y Métricas) integrado con Prometheus / Grafana / Aspire[cite: 3].
- **Pruebas y CI/CD:** GitHub Actions (Pipelines), pruebas unitarias C++ (GoogleTest / Catch2) y React (Vitest / Jest), Pruebas Funcionales E2E con Playwright[cite: 3].
- **Pruebas de Carga:** k6 / Locust / Apache JMeter[cite: 3].

---

## 3. Arquitectura del Sistema y Componentes

### 3.1. Aplicación Web (ReactJS + TypeScript)
- **Ruta/Ubicación:** `/frontend`
- **Requerimientos Funcionales:**
  - **Autenticación y Registro:** Formularios de Login y Registro integrados con el API de Usuarios[cite: 2].
  - **Catálogo y Paginación:** Visualización de videos propios y públicos ordenables por título, autor y fecha de carga[cite: 2]. La ordenación y paginación se realiza delegando la consulta al API de Metadata[cite: 2].
  - **Gestión de Videos:** Subida de videos, modificación de metadata, eliminación y alternancia de visibilidad (Público/Privado)[cite: 1, 2].
  - **Cuotas de Almacenamiento:** Control estricto del límite asignado por usuario. Bloquear la subida cuando la cuota sea superada[cite: 2].
  - **Reproductor:** Integración de Shaka Player / Dash.js para streaming adaptativo[cite: 2].

### 3.2. API de Usuarios (C++)
- **Ruta/Ubicación:** `/services/user-service`
- **Interfaz:** REST API en C++ (usando Crow, Pistache o HTTPLib)[cite: 2].
- **Requerimientos Funcionales:**
  - Gestión CRUD completa de usuarios (Registrar, Consultar, Actualizar, Eliminar)[cite: 2].
  - Persistence Engine: Guardado directo en **archivo binario** personalizado[cite: 2].
  - Indexación: Implementación nativa de un **Árbol B en disco** para optimización de búsquedas[cite: 2].

### 3.3. API de Metadata (C++)
- **Ruta/Ubicación:** `/services/metadata-service`
- **Interfaz:** REST API en C++[cite: 2].
- **Requerimientos Funcionales:**
  - Gestión de metadatos de videos: autor, duración, fecha de carga, título, tema, etiquetas y estado de visibilidad[cite: 1, 2].
  - Integración directa con MongoDB usando `mongocxx` driver[cite: 2].
  - Endpoints para listado con soporte de ordenación (título, autor, fecha) y paginación[cite: 2].

### 3.4. API de Streaming (C++)
- **Ruta/Ubicación:** `/services/streaming-service`
- **Interfaz:** REST API en C++[cite: 2].
- **Requerimientos Funcionales:**
  - Transcodificación de videos en background al momento de la carga mediante **FFMPEG** (conversión a segmentos HLS/DASH)[cite: 2].
  - Servicio de entrega de bloques/chunks de video hacia el reproductor web de manera eficiente[cite: 2].

### 3.5. Dashboard y Observabilidad
- **Instrumentación:** OpenTelemetry C++ SDK en las 3 APIs y OpenTelemetry JS en el Frontend[cite: 3].
- **Métricas:** Latencia de endpoints, tasa de errores (HTTP 5xx/4xx), uso de CPU/memoria y throughput de streaming[cite: 3].
- **Visualización:** Configuración de Prometheus + Grafana o Aspire para consumo e interpretación de métricas y traces[cite: 3].

---

## 4. Requerimientos de Arquitectura V2.0 (Alta Disponibilidad y Calidad)

### 4.1. Eliminación de Single Point of Failure (SPOF)
- Mapeo y ejecución en al menos 2 instancias independientes/separadas para cada API (Usuarios, Metadata, Streaming)[cite: 3].
- Implementación de balanceador de carga (Nginx / HAProxy / Traefik) frente a los servicios[cite: 3].

### 4.2. Optimización de Performance
- Identificación y resolución de al menos 5 cuellos de botella basados en las métricas de la V1.0 (ej. caching de metadata, hilos de FFMPEG, optimización de B-Tree)[cite: 3].

### 4.3. Pruebas de Carga y Estrés
- Scripts de carga simulando usuarios concurrentes para medir capacidad límite de 1 servidor vs. 2 servidores con balanceador[cite: 3].

### 4.4. CI/CD y Pruebas
- **GitHub Workflows:** Pipeline de compilación y empaquetado automático de ejecutables C++ y cliente React[cite: 3].
- **Cobertura:** Mínimo **90% de cobertura de código** mediante pruebas unitarias en todos los componentes[cite: 3].
- **Pruebas Funcionales:** Pruebas E2E automatizadas usando **Playwright** totalmente integradas en GitHub Actions[cite: 3].

---

## 5. Criterios de Aceptación para Agentes de IA
1. Todo código C++ debe estar estructurado bajo principios del paradigma orientado a objetos (OOP) y buenas prácticas de memoria[cite: 1, 5].
2. Los componentes del frontend deben estar fuertemente tipados con Interfaces de TypeScript sin usar `any`.
3. Todos los endpoints REST deben cumplir con respuestas en formato JSON estandarizado y códigos de estado HTTP apropiados.
4. El pipeline en GitHub Actions debe ejecutarse sin errores antes de consolidar ramas o crear tags para V1.0 y V2.0[cite: 3, 4].

## 6. Agent Memory

- **2026-10-13:** Se analizó la especificación técnica y se identificó que la sección de observabilidad requiere instrumentación OpenTelemetry en las APIs, métricas en Prometheus y visualización en Grafana/Aspire. El usuario eligió Prometheus + Grafana para el dashboard.
- **2026-10-13:** Se preparó una primera configuración local con Docker Compose: OpenTelemetry Collector recibe métricas OTLP en 4317/4318, Prometheus las recopila y Grafana carga un dashboard inicial. Se añadió la guía de inicio a `README.md`. Se validaron YAML, JSON y formato del diff; Docker no estaba disponible, así que no se pudo iniciar ni verificar los contenedores. Próximo paso sugerido: instrumentar un servicio de ejemplo en C++ con contador de solicitudes y medición de latencia, explicando cada parte al usuario.
- **2026-10-13:** El usuario pidió registrar en esta sección anotaciones de las respuestas y conversaciones para mantener el historial del trabajo.