# Hardware Analyzer & Monitor

Creado por: **vamp9**

[![Language](https://img.shields.io/badge/Language-C-00599C?style=flat&logo=c&logoColor=white)](https://en.wikipedia.org/wiki/C_(programming_language))
[![Platform](https://img.shields.io/badge/Platform-Windows-0078D6?style=flat&logo=windows&logoColor=white)](https://www.microsoft.com/windows)
[![API](https://img.shields.io/badge/API-Win32-0078D4?style=flat&logo=microsoft&logoColor=white)](https://learn.microsoft.com/en-us/windows/win32/)
[![GPU APIs](https://img.shields.io/badge/GPU_APIs-NVAPI_%26_ADL-76B900?style=flat&logo=nvidia&logoColor=white)](https://developer.nvidia.com/nvapi)
[![License](https://img.shields.io/badge/License-MIT-yellow.svg?style=flat)](LICENSE)
[![Overhead](https://img.shields.io/badge/Overhead-Zero-success?style=flat)](#)

Overlay nativo de monitorización de rendimiento de hardware en tiempo real escrito en C puro (Win32 API). Diseñado con enfoque en cero consumo de recursos adicionales (Zero Overhead) y portabilidad directa sin dependencias externas.

---

> [!WARNING]
> ### ADVERTENCIA IMPORTANTE SOBRE ANTI-CHEATS (VANGUARD / RIO T / COMPETITIVOS)
> El uso de este monitor de hardware junto con videojuegos que utilicen sistemas anti-trampas a nivel de kernel (como **Valorant Vanguard**, **Easy Anti-Cheat**, **BattlEye** o **Ricochet**) se realiza **bajo tu propia responsabilidad**. 
> 
> Aunque la aplicación no inyecta código en los juegos, no hace DLL Hooking y simula la tasa de FPS de manera heurística precisamente para evitar disparar alertas de seguridad, **no garantizamos que estos sistemas de seguridad de kernel no marquen el overlay como software no autorizado**. Te recomendamos encarecidamente **no arriesgar tu cuenta** y evitar ejecutar el overlay de forma simultánea con juegos competitivos protegidos por software de nivel Ring-0 (Kernel) si deseas evitar posibles bloqueos o suspensiones de cuenta.

---

## Características principales

- **Monitoreo Universal de Recursos**: Consumo de CPU, GPU y memoria RAM extraído directamente mediante la librería nativa de Windows `PDH` (Performance Data Helpers). Compatible con cualquier arquitectura y fabricante (NVIDIA, AMD, Intel).
- **Métricas de Temperatura Inteligentes**:
  - Lectura nativa de temperatura para tarjetas **NVIDIA** cargando dinámicamente `nvapi.dll` / `nvapi64.dll`.
  - Lectura nativa para tarjetas **AMD** cargando dinámicamente la biblioteca ADL (`atiadlxx.dll` / `atiadlxy.dll`).
  - Algoritmo térmico inteligente de respaldo (Fallback) para GPU integradas (Intel) o en entornos virtuales, estimando de forma matemática la temperatura según la carga de procesamiento real.
- **Algoritmo de FPS Simulados**: Para evitar técnicas de Hooking de DirectX/OpenGL o ETW (bloqueadas directamente por anti-cheats agresivos como Vanguard), el monitor estima los FPS basándose en la carga cruzada de CPU/GPU y añade ruido de micro-stuttering coherente con el rendimiento real.
- **Transparencia Click-through**: Configurado nativamente a través de las capas de renderizado DWM de Windows (`LWA_COLORKEY`) para actuar como una ventana fantasma sobre la cual se puede interactuar sin obstaculizar los clics en el juego.

## Compilación y Ejecución

### Requisitos previos
- Compilador de C compatible con Windows (por ejemplo, GCC mediante MinGW).
- Herramienta `make` o similar.

### Compilar el proyecto
Ejecuta el siguiente comando en la carpeta raíz del proyecto:
```bash
make
```

Esto generará el archivo ejecutable `hw_monitor.exe`.

### Ejecución
1. Haz doble clic en `hw_monitor.exe`.
2. El overlay se iniciará y monitorizará automáticamente los recursos del sistema cuando una aplicación o videojuego pase a pantalla completa.
3. Para cerrarlo, haz clic derecho sobre el ícono de la aplicación en la bandeja del sistema (System Tray, al lado del reloj de Windows) y selecciona **Salir**.

## Licencia

Este proyecto está bajo la Licencia MIT. Consulta el archivo `LICENSE` para ver los créditos y detalles de autoría asociados a **vamp9**.
