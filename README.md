# Smart Backup Kernel-Space Utility

Sistema de respaldo de archivos implementado en C que opera a nivel de **kernel-space simulado**, usando system calls directas de Linux (POSIX) como `open`, `read`, `write` y `close`.

**Curso:** Sistemas Operativos  
**Institución:** Universidad EAFIT — 2026  
**Autores:** Jeronimo Contreras, Juan Esteban Peña y Daniel Arcila

---

## Tabla de Contenido

- [Descripción del Proyecto](#-descripción-del-proyecto)
- [Arquitectura del Sistema](#-arquitectura-del-sistema)
- [Estructura de Archivos](#-estructura-de-archivos)
- [Requisitos Previos](#-requisitos-previos)
- [Instalación y Compilación](#-instalación-y-compilación)
- [Cómo Ejecutar](#-cómo-ejecutar)
  - [Modo Backup](#modo-backup--b)
  - [Modo Benchmark](#modo-benchmark---benchmark)
  - [Modo Ayuda](#modo-ayuda--h)
- [Descripción Técnica](#-descripción-técnica)
  - [sys\_smart\_copy — Capa Kernel](#sys_smart_copy--capa-kernel)
  - [lib\_smart\_copy — Capa Usuario](#lib_smart_copy--capa-usuario)
  - [Comparativa de Rendimiento](#comparativa-de-rendimiento)
- [Manejo de Errores](#-manejo-de-errores)
- [Targets del Makefile](#-targets-del-makefile)
- [Código Base del Profesor](#-código-base-del-profesor)

---

## Descripción del Proyecto

Este proyecto implementa un sistema de backup en dos capas:

| Capa | Función | Descripción |
|---|---|---|
| **Kernel-Space** | `sys_smart_copy()` | Copia usando system calls directas (`open`, `read`, `write`) con buffer de **4096 bytes** (tamaño de página del kernel) |
| **User-Space** | `lib_smart_copy()` | Copia equivalente usando `fopen`/`fread`/`fwrite` de `stdio.h` |

El propósito es demostrar la diferencia arquitectural entre operar directamente sobre el kernel vs usar la capa de abstracción de la librería estándar de C, y medir el impacto real en rendimiento (**context switches**).

---

## Arquitectura del Sistema

```
┌─────────────────────────────────────────────────────────┐
│                    ESPACIO DE USUARIO                   │
│                                                         │
│  ┌──────────┐    ┌─────────────────┐    ┌───────────┐  │
│  │  main.c  │───▶│ backup_engine.c │───▶│smart_copy │  │
│  │  (CLI)   │    │                 │    │   .h      │  │
│  └──────────┘    │ sys_smart_copy  │    └───────────┘  │
│                  │ lib_smart_copy  │                    │
│                  └────────┬────────┘                    │
│                           │  System Calls               │
├───────────────────────────┼─────────────────────────────┤
│                    KERNEL SPACE                         │
│                           │                             │
│              ┌────────────▼────────────┐                │
│              │  open / read / write    │                │
│              │  close / stat / fsync   │                │
│              └────────────┬────────────┘                │
│                           │                             │
│              ┌────────────▼────────────┐                │
│              │   Sistema de Archivos   │                │
│              │   (VFS / ext4 / btrfs)  │                │
│              └─────────────────────────┘                │
└─────────────────────────────────────────────────────────┘
```

**Flujo de `sys_smart_copy` para un archivo de N bytes:**
```
stat(src) → open(src) → open(dest) → [read(4KB) → write(4KB)] × N/4096 → close() × 2
```

---

## Estructura de Archivos

```
Backup_Kernel-Space/
│
├── smart_copy.h          ← Interfaz pública: firmas, constantes, flags, struct SmartCopyResult
├── backup_engine.c       ← Motor de copia: sys_smart_copy() + lib_smart_copy()
├── main.c                ← CLI: modo backup (-b) y modo benchmark (--benchmark)
├── Makefile              ← Reglas de compilación y pruebas
│
├── reporte_backup.md     ← Informe técnico (exportar a PDF para entrega)
│
├── backup.c              ← Código base proporcionado por el profesor (referencia)
│
└── Apoyo/
    └── CallSystem_document.md   ← Documentación de apoyo sobre system calls
```

### Descripción de cada archivo fuente

#### `smart_copy.h` — Interfaz pública
Define la "API del sistema":
- `PAGE_BUFFER_SIZE = 4096` — constante de tamaño de página del kernel
- Flags de comportamiento: `SC_FLAG_VERBOSE`, `SC_FLAG_PRESERVE_PERMS`, etc.
- Códigos de error: `SC_SUCCESS`, `SC_ERR_SRC_NOT_FOUND`, `SC_ERR_DISK_FULL`, etc.
- `struct SmartCopyResult` — encapsula bytes copiados, tiempo y número de syscalls
- Prototipos de `sys_smart_copy()` y `lib_smart_copy()`

#### `backup_engine.c` — Motor de copia
Contiene las dos implementaciones:
- **`sys_smart_copy()`** — Syscalls directas: `stat → open → read/write loop → close`
- **`lib_smart_copy()`** — Librería estándar: `fopen → fread/fwrite loop → fclose`
- Medición de tiempo con `clock_gettime(CLOCK_MONOTONIC)`
- Gestión completa de errores con `errno`

#### `main.c` — Interfaz de usuario
- Modo `-b src dest`: copia operacional + comparación inmediata con stdio
- Modo `--benchmark`: genera archivos de 1KB/1MB/10MB/1GB y muestra tabla comparativa
- Modo `-h`: manual de uso

---

## Requisitos Previos

Este proyecto usa **system calls de Linux/POSIX** y debe compilarse y ejecutarse en un entorno Linux. En Windows, usar **WSL** (Windows Subsystem for Linux).

### Verificar que tienes GCC instalado en WSL

```bash
gcc --version
```

Si no está instalado:

```bash
sudo apt update && sudo apt install -y gcc make
```

### Verificar WSL (desde PowerShell/CMD de Windows)

```powershell
wsl --list --verbose
```

---

## Instalación y Compilación

### 1. Abrir una terminal WSL y navegar al proyecto

```bash
# Desde PowerShell de Windows, abrir WSL en el directorio del proyecto:
wsl -e bash -c "cd /mnt/c/Users/57318/EAFIT/Semestre_6/Sistemas_operativos/Backup_Kernel-Space && bash"

# O directamente dentro de WSL:
cd /mnt/c/Users/57318/EAFIT/Semestre_6/Sistemas_operativos/Backup_Kernel-Space
```

### 2. Compilar el proyecto

```bash
make all
```

**Salida esperada:**
```
  [CC]  Compilando backup_smart...
  [OK]  Compilado exitosamente: ./backup_smart

  Uso: ./backup_smart -b <origen> <destino>
       ./backup_smart --benchmark
       ./backup_smart --help
```

### 3. Verificar que compiló correctamente

```bash
ls -la backup_smart
```

---

## Cómo Ejecutar

### Modo Backup (`-b`)

Copia un archivo usando `sys_smart_copy` (system calls directas), y también genera una copia de referencia con `lib_smart_copy` para comparación inmediata.

```bash
./backup_smart -b <archivo_origen> <archivo_destino>
```

**Ejemplos:**

```bash
# Copiar un archivo de texto
./backup_smart -b documento.txt backup_documento.txt

# Copiar un archivo binario
./backup_smart -b programa.bin backup_programa.bin

# Copiar preservando ruta
./backup_smart -b /home/user/datos.db /tmp/backup/datos.db
```

**Salida de ejemplo:**
```
  ╔══════════════════════════════════════════════════════════════════╗
  ║         SMART BACKUP KERNEL-SPACE UTILITY                      ║
  ║         Sistema Operativos - EAFIT - 2026                      ║
  ╚══════════════════════════════════════════════════════════════════╝

  ▶  MODO BACKUP
  ▶  Origen:  documento.txt
  ▶  Destino: backup_documento.txt
  ▶  Buffer:  4096 bytes (4 KB)

  [1/2] Copiando con sys_smart_copy (system calls)...
  ✓  Copia exitosa.
  [syscall ] Estado: Operación exitosa              | Bytes:     1024 | Tiempo:    0.082 ms | Llamadas I/O: 2

  [2/2] Copiando con lib_smart_copy (stdio) para comparar...
  ✓  Copia de referencia lista en: backup_documento.txt.stdio_copy
  [stdio   ] Estado: Operación exitosa              | Bytes:     1024 | Tiempo:    0.051 ms | Llamadas I/O: 2
```

---

### Modo Benchmark (`--benchmark`)

Ejecuta una comparativa de rendimiento completa entre `sys_smart_copy` y `lib_smart_copy` con 4 tamaños de archivo (1 KB, 1 MB, 10 MB, 1 GB) y 3 corridas por prueba.

```bash
./backup_smart --benchmark
```

O directamente desde Make:

```bash
make benchmark
```

**Salida real del benchmark (3 corridas, buffer 4 KB):**
```
  TABLA COMPARATIVA DE RENDIMIENTO
  sys_smart_copy (syscalls) vs lib_smart_copy (stdio)

  +--------+--------------------+------------------------------------------+------------------------------------------+
  |        |  sys_smart_copy (syscalls)                |  lib_smart_copy (stdio)                  |
  | Tamaño +----------+----------+--------+-----------+----------+----------+--------+-----------+
  |        |  Método  | T.Prom.  |  MB/s  | Llam.I/O  |  Método  | T.Prom.  |  MB/s  | Llam.I/O  |
  +--------+----------+----------+--------+-----------+----------+----------+--------+-----------+
  |   1 KB | syscall  |   4.314 ms |   0.2 |         2 | stdio    |   6.193 ms |   0.2 |         2 |
  |   1 MB | syscall  |  88.010 ms |  11.4 |       512 | stdio    | 136.285 ms |   7.3 |       512 |
  |  10 MB | syscall  | 753.071 ms |  13.3 |      5120 | stdio    |1315.643 ms |   7.6 |      5120 |
  | 1024 MB| syscall  |249221.126 ms|   4.1 |    524288 | stdio    |96586.439 ms|  10.6 |    524288 |
  +--------+----------+----------+--------+-----------+----------+----------+--------+-----------+
```

---

### Modo Ayuda (`-h`)

```bash
./backup_smart -h
# o
./backup_smart --help
```

---

### Prueba Rápida con Makefile

```bash
# Prueba de copia + verificación de integridad con diff
make test
```

**Salida esperada:**
```
  --- [1] PREPARANDO ARCHIVO DE PRUEBA ---
  --- [2] EJECUTANDO COPIA CON sys_smart_copy ---
  ...
  --- [3] VERIFICANDO INTEGRIDAD (diff) ---
  [PASS] Los archivos son idénticos.
```

---

## 🔬 Descripción Técnica

### `sys_smart_copy` — Capa Kernel

Simula el comportamiento interno que tendría una función implementada dentro del kernel de Linux. Las system calls utilizadas son:

| System Call | Cabecera | Propósito en la función | N° Linux x86-64 |
|---|---|---|---|
| `stat()` | `<sys/stat.h>` | Verificar existencia y obtener permisos del origen | #4 |
| `open()` | `<fcntl.h>` | Obtener descriptor de archivo para origen y destino | #2 |
| `read()` | `<unistd.h>` | Leer bloque de 4096 bytes desde origen | #0 |
| `write()` | `<unistd.h>` | Escribir bloque de 4096 bytes hacia destino | #1 |
| `close()` | `<unistd.h>` | Liberar descriptores abiertos | #3 |
| `fsync()` | `<unistd.h>` | Forzar escritura del page cache al disco (flag opcional) | #74 |

**Buffer de 4096 bytes (PAGE_SIZE):**  
Este valor corresponde exactamente al tamaño de una página de memoria en x86-64. Usar este tamaño maximiza el throughput porque el kernel gestiona la memoria en unidades de páginas; transferencias alineadas al tamaño de página evitan lecturas parciales que aumentan el número de syscalls.

**Costo de context switches para un archivo de 1 MB:**
```
1 048 576 bytes / 4096 bytes = 256 bloques
→ 256 llamadas read() + 256 llamadas write() = 512 context switches al kernel
Overhead estimado: 512 × ~200 ns = ~102 µs extra vs stdio
```

### `lib_smart_copy` — Capa Usuario

Implementación equivalente usando `stdio.h`. La diferencia clave es el **buffer interno de `FILE*`** (8192 bytes por defecto en glibc, definido como `BUFSIZ`):

```
       Programa                 glibc                  Kernel
          │                      │                       │
  fread(buf, 1, 4096) ──────────▶│                       │
          │              Si buffer FILE* tiene datos:     │
          │◀─────────────── memcpy() (sin syscall) ──────│
          │                      │                       │
          │              Si buffer FILE* vacío:           │
          │                      ├──── read(8192) ───────▶│
          │                      │◀───────────────────────│
          │◀─────────────── memcpy() ─────────────────────│
```

### Comparativa de Rendimiento

| Escenario | Ganador | Resultado real | Razón |
|---|---|---|---|
| Archivos ≤ 1 KB | Indiferente | ~0.2 MB/s ambos | Ruido de medición domina; mismo N° de syscalls |
| Archivos medianos (1–10 MB) | **`sys_smart_copy`** ✓ | 1.55–1.75× más rápido | Evita copia intermedia del buffer interno de `FILE*` |
| Archivos grandes (≥ 1 GB) | **`lib_smart_copy`** ✓ | 10.6 vs 4.1 MB/s | Presión sobre page cache + `fsync()` penaliza syscalls |
| Control total del sistema | `sys_smart_copy` | — | Acceso directo a metadatos del kernel vía `stat/errno` |

> ⚠️ **Resultado contraintuitivo en 1 GB:** `lib_smart_copy` superó a `sys_smart_copy` por ~2.5×. Esto se atribuye a la saturación del page cache en WSL2 y al overhead del `fsync()` final al escribir 1 GB a disco.

---

## Manejo de Errores

El sistema implementa manejo exhaustivo de errores usando `errno`, los mismos códigos que usa el kernel de Linux:

| Situación | `errno` del kernel | Código interno | Comportamiento |
|---|---|---|---|
| Archivo origen no existe | `ENOENT` | `SC_ERR_SRC_NOT_FOUND` | Mensaje descriptivo, exit sin crear destino |
| Sin permiso de lectura/escritura | `EACCES` | `SC_ERR_DST_NO_PERM` | Cierra fds abiertos, exit con código error |
| Disco lleno durante escritura | `ENOSPC` | `SC_ERR_DISK_FULL` | Cierra fds, no deja archivo corrupto |
| Error de lectura en medio de copia | `EIO` | `SC_ERR_READ` | Abort seguro, cierra todos los recursos |
| Parámetros NULL | — | `SC_ERR_NULL_PTR` | Validación antes de cualquier operación |

**Garantía:** En cualquier ruta de error, todos los descriptores de archivo abiertos son cerrados antes de retornar.

---

## Targets del Makefile

```bash
make all          # Compila backup_smart (proyecto principal)
make legacy       # Compila backup_EAFITos (código base del profesor)
make benchmark    # Compila + ejecuta benchmark completo
make test         # Prueba rápida con verificación de integridad (diff)
make clean        # Elimina ejecutables y archivos temporales
make help         # Muestra ayuda del Makefile
```

**Flags de compilación:** `-Wall -Wextra -g -std=c11`

---

## Código Base del Profesor

El archivo `backup.c` es el código base original proporcionado por el profesor. Implementa copia de archivos y directorios recursiva usando system calls. Se conserva como referencia y se puede compilar de forma independiente:

```bash
make legacy
./backup_EAFITos -b origen destino
./backup_EAFITos --help
```

El proyecto `backup_smart` (este proyecto) es una versión extendida y modularizada que agrega:
- Firma formal de función de sistema (`sys_smart_copy`)
- Comparativa con stdio (`lib_smart_copy`)
- Medición de rendimiento con `clock_gettime`
- Modo benchmark automático
- Manejo de errores con códigos internos

---

## Entregables

| Archivo | Descripción |
|---|---|
| `smart_copy.h` | Definición de la firma de la función y constantes de flags |
| `backup_engine.c` | Lógica central de la copia (simulando comportamiento del kernel) |
| `main.c` | Interfaz de usuario con rutas origen/destino y pruebas de tiempo |
| `reporte_backup.md` | Informe técnico — convertir a PDF para la entrega final |

---

## Referencias

- Código base del profesor: https://github.com/evalenciEAFIT/courses/tree/main/SO_XV6/tema/backup
- Material de apoyo: https://drive.google.com/drive/folders/1m8dM3hU7BUP2Mt0nYIK9VtySzbmymSLw
