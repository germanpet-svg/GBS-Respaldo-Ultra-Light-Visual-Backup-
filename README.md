# 🛡️ GBS Respaldo (Ultra-Light Visual Backup)

[![License: MIT](https://img.shields.io/badge/License-MIT-yellow.svg)](https://opensource.org/licenses/MIT)
[![Language: C](https://img.shields.io/badge/Language-C-blue.svg)](https://en.wikipedia.org/wiki/C_(programming_language))
[![Platform: Windows](https://img.shields.io/badge/Platform-Windows-0078D6.svg)](https://www.microsoft.com/windows)

Un **"Git ultrasónico y local"** escrito en C puro para desarrolladores. Olvídate de herramientas pesadas que consumen gigabytes de RAM y espacio en disco. **GBS Respaldo** es un ejecutable nativo de menos de 150 KB que vigila tus archivos de código en tiempo real y genera copias de seguridad versionadas por fecha y hora (`.zip`) de forma automática y quirúrgica.

---

## 👥 Créditos & Autoría

Este proyecto fue ideado, diseñado y desarrollado por:
* **Germán Peña** - *Bioinformático en GBSCoreLab, Venezuela* 🇻🇪
* **GitHub:** [@germanpet-svg](https://github.com/germanpet-svg)

**Desarrollado con 💻 y C puro para optimizar los flujos de trabajo de la comunidad de desarrolladores y científicos independientes.**

---

## ✨ Características Principales

### ⚡ Rendimiento Extremo
* **Consumo Cero:** Menos de 1.5 MB de RAM en uso y 0% de CPU mientras duerme entre ciclos.
* **Tamaño ridículo:** Ejecutable nativo de ~150 KB (sin dependencias externas ni frameworks pesados).

### 🖱️ Experiencia Drag & Drop y Menú Contextual
* Selecciona tus archivos de código o carpetas desde tu entorno de desarrollo, arrástralos y suéltalos encima de `respaldador.exe` para activar el guardián.
* **🔥 NUEVO:** También funciona desde el menú contextual del Explorador de Windows (Clic derecho $\rightarrow$ *"Enviar a GBS Respaldo"*).

### 📂 Organización Inteligente por Proyectos
* Detecta automáticamente la carpeta origen de tus archivos.
* Crea subcarpetas dedicadas para cada proyecto de forma organizada dentro de tu directorio de respaldos.

### 🔍 Validación por Hash MD5
* A diferencia de otros scripts que solo miran la fecha de modificación, **GBS Respaldo** calcula el MD5 bit a bit.
* Solo genera un respaldo si el contenido cambió de verdad, evitando archivos basura en caso de que tu editor guarde automáticamente sin realizar modificaciones.

### 🚫 Exclusiones Inteligentes
Filtra de forma automática carpetas y extensiones pesadas o irrelevantes:
* **Carpetas:** `node_modules`, `.git`, `debug`, `release`, `bin`, `obj`.
* **Extensiones:** `.exe`, `.dll`, `.obj`, `.log`, `.tmp`, `.bak`.

### 🚀 Comunicación entre Instancias
* Si el programa ya se encuentra en ejecución y arrastras nuevos archivos (o los mandas vía clic derecho), se agregan a la lista actual **sin duplicar procesos en segundo plano**.

---

## 🎨 Interfaz en la Barra de Tareas (System Tray)

Funciona de forma 100% invisible, añadiendo un pequeño icono inteligente al lado del reloj de Windows con las siguientes acciones:

| Acción | Comportamiento |
| :--- | :--- |
| **Hover (Puntero encima)** | Muestra el panel expandido con estadísticas de monitoreo y últimos cambios. |
| **Click Izquierdo** | Despliega una ventana emergente con métricas detalladas del sistema. |
| **Doble Click** | Abre la carpeta central de respaldos directamente en el Explorador. |
| **Click Derecho** | Abre el menú contextual (Pausar / Reanudar / Agregar archivo / Estadísticas / Salir). |
| **Icono Dinámico** | Cambia a **Verde** al procesar un respaldo y a **Amarillo** si la vigilancia está pausada. |

### 📊 Tooltip Expandido On-Hover
Al posicionar el mouse sobre el icono de la bandeja, verás un panel de estado estructurado así:

```text
═══════════════════════════════════
📁 GBS Respaldo - Estado
═══════════════════════════════════
📄 Archivos vigilados: 47
📦 Backups hoy: 12
💾 Espacio usado: 156.3 MB
🎛️ Estado: ACTIVO
═══════════════════════════════════
📜 Últimos respaldos:
   • main.c (14:23:15)
   • utils.h (14:18:42)
   • config.c (14:12:07)
   • main.c (14:05:33)
   • database.c (13:58:21)
═══════════════════════════════════
💡 Click izq: Estadísticas
💡 Click der: Menú
💡 Doble click: Abrir carpeta
