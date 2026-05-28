# 🛡️ GBS Respaldo (Ultra-Light Visual Backup)

[![License: MIT](https://img.shields.io/badge/License-MIT-yellow.svg)](https://opensource.org/licenses/MIT)
[![Language: C](https://img.shields.io/badge/Language-C-blue.svg)](https://en.wikipedia.org/wiki/C_(programming_language))
[![Platform: Windows](https://img.shields.io/badge/Platform-Windows-0078D6.svg)](https://www.microsoft.com/windows)

Un **"Git ultrasónico y local"** escrito en C puro para desarrolladores. Olvídate de herramientas pesadas que consumen gigabytes de RAM y espacio en disco. **GBS Respaldo** es un ejecutable nativo de menos de 100 KB que vigila tus archivos de código en tiempo real y genera copias de seguridad versionadas por fecha y hora (`.zip`) de forma automática y quirúrgica.

---

## 👥 Créditos & Autoría

Este proyecto fue ideado, diseñado y desarrollado por:
* **Germán Peña** - *Bioinformático en GBSCoreLab, Venezuela* 🇻🇪
* **GitHub:** [@germanpet-svg](https://github.com/germanpet-svg)

**Desarrollado con 💻 y C puro para optimizar los flujos de trabajo de la comunidad de desarrolladores y científicos independientes.**

---

## ✨ Características Principales

* **⚡ Consumo Cero:** Menos de 1.5 MB de RAM en uso y 0% de CPU mientras duerme entre ciclos.
* **🖱️ Experiencia Drag & Drop:** Selecciona tus archivos de código o carpetas desde tu entorno de desarrollo, arrástralos y suéltuarios encima de `respaldador.exe` para activar el guardián.
* **📂 Organización Inteligente por Proyectos:** El programa detecta automáticamente la carpeta origen de tus archivos y crea subcarpetas dedicadas para cada proyecto en tu directorio de respaldos.
* **🔍 Validación por Hash MD5:** A diferencia de otros scripts que solo miran la fecha, **GBS Respaldo** calcula el MD5 bit a bit. Solo genera un respaldo si el contenido cambió de verdad (evita archivos basura si tu editor guarda sin modificar).
* **🚫 Exclusiones Inteligentes:** Filtra de forma automática carpetas y extensiones pesadas o irrelevantes (como `node_modules`, `.git`, `.exe`, `.dll`, `.log`).
* **🎨 Indicador Visual en el System Tray:** Funciona de forma 100% invisible, añadiendo un pequeño icono al lado del reloj de Windows que cambia de color y parpadea cuando está empaquetando un respaldo.
* **🌙 Modo Nocturno & Retención:** Configura horarios de descanso para no saturar procesos y define límites de backups máximos para cuidar tu almacenamiento.

---

## 🛠️ Compilación (Instalación Rápida)

Al ser código nativo en C para la API de Windows, puedes compilarlo en un segundo con **GCC (MinGW)**. 

Abre tu terminal en la carpeta con el código fuente y ejecuta:

```bash
gcc gbsbio_respaldo.c -o respaldador.exe -mwindows -lcrypt32
