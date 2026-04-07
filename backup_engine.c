/*
 * NOTA TÉCNICA: _POSIX_C_SOURCE 200809L activa las extensiones de POSIX.1-2008
 * en glibc. Es necesario para usar clock_gettime() y CLOCK_MONOTONIC, que son
 * funciones de temporización de alta resolución definidas en POSIX, no en el
 * estándar C11 puro. Esta macro debe ir ANTES de cualquier #include.
 */
#define _POSIX_C_SOURCE 200809L

/*
 * backup_engine.c
 * ===============
 * Proyecto: Smart Backup Kernel-Space Utility
 * Curso: Sistemas Operativos - EAFIT
 *
 * Motor central de copia de archivos.
 *
 * Contiene dos implementaciones de la misma lógica de copia:
 *
 *  1. sys_smart_copy() — Usa system calls directas (open, read, write, close).
 *     Simula el comportamiento que tendría una función implementada dentro
 *     del kernel, operando directamente sobre descriptores de archivo y
 *     sin ninguna capa de buffering adicional en espacio de usuario.
 *
 *  2. lib_smart_copy() — Usa la librería estándar de C (fopen, fread, fwrite).
 *     Representa el enfoque típico de un programa de usuario que delega el
 *     manejo del buffer al runtime de C (FILE*).
 *
 * La comparación entre ambas implementaciones permite analizar el costo
 * real de los context switches (paso modo usuario → modo kernel).
 *
 * Autores: [Nombres del equipo]
 * Fecha:   2026
 */

#include "smart_copy.h"

/* Cabeceras para system calls de archivo */
#include <fcntl.h>       /* open(), O_RDONLY, O_WRONLY, O_CREAT, O_TRUNC */
#include <unistd.h>      /* read(), write(), close(), fsync()             */
#include <sys/stat.h>    /* stat(), fstat(), struct stat                  */

/* Cabeceras para librería estándar (lib_smart_copy) */
#include <stdio.h>       /* fopen(), fread(), fwrite(), fclose()          */

/* Cabeceras generales */
#include <errno.h>       /* errno, ENOSPC, EACCES, etc.                   */
#include <string.h>      /* strerror()                                    */
#include <time.h>        /* clock_gettime(), CLOCK_MONOTONIC              */
#include <stdlib.h>      /* NULL                                          */

/* =========================================================================
 * FUNCIONES AUXILIARES INTERNAS (static = scope restringido a este archivo)
 * =========================================================================
 */

/*
 * get_time_ms()
 * -------------
 * Retorna el tiempo monotónico actual en milisegundos.
 *
 * Usamos CLOCK_MONOTONIC en lugar de CLOCK_REALTIME porque:
 *   - No se ve afectado por ajustes del reloj del sistema (NTP, cambio manual).
 *   - Es estrictamente creciente, garantizando mediciones de intervalo correctas.
 *
 * Esta es la misma fuente de tiempo que usan las utilidades de benchmarking
 * del kernel (perf, ftrace) para medir latencias.
 */
static double get_time_ms(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (double)ts.tv_sec * 1000.0 + (double)ts.tv_nsec / 1.0e6;
}

/*
 * validate_pointers()
 * -------------------
 * Verifica que src, dest y result no sean punteros nulos.
 * Una función de sistema DEBE validar todos sus punteros de entrada
 * antes de dereferenciarlos para evitar fallos de segmentación (kernel panic).
 *
 * Retorna: SC_SUCCESS si todos son válidos, SC_ERR_NULL_PTR si alguno es NULL.
 */
static int validate_pointers(const char *src, const char *dest,
                              SmartCopyResult *result)
{
    if (src == NULL || dest == NULL || result == NULL) {
        return SC_ERR_NULL_PTR;
    }
    return SC_SUCCESS;
}

/*
 * init_result()
 * -------------
 * Inicializa todos los campos de SmartCopyResult a sus valores por defecto.
 * Evita leer basura de memoria no inicializada.
 */
static void init_result(SmartCopyResult *result)
{
    result->bytes_copied  = 0;
    result->elapsed_ms    = 0.0;
    result->error_code    = SC_SUCCESS;
    result->syscall_count = 0;
}

/* =========================================================================
 * IMPLEMENTACIÓN 1: sys_smart_copy (System Calls directas)
 * =========================================================================
 */

/*
 * sys_smart_copy()
 * ----------------
 * Copia un archivo binario usando exclusivamente system calls de bajo nivel.
 *
 * Flujo de ejecución y syscalls involucradas:
 *
 *  1. stat(src)         → Verifica existencia y obtiene permisos (modo del archivo).
 *  2. open(src, RDONLY) → Abre el origen; el kernel devuelve un descriptor (fd).
 *  3. open(dest, ...)   → Abre/crea el destino con los permisos del original.
 *  4. Loop:
 *       read(fd_src)    → El kernel copia datos del buffer de página al buffer.
 *       write(fd_dest)  → El kernel escribe datos del buffer al disco.
 *     Cada par read/write implica al menos 2 context switches usuario→kernel.
 *  5. [Opcional] fsync(fd_dest) → Fuerza escritura a disco (SC_FLAG_SYNC).
 *  6. close(fd_src)     → Libera el descriptor del origen.
 *  7. close(fd_dest)    → Libera el descriptor del destino.
 *
 * COSTO REAL:
 *   Para un archivo de N bytes con buffer de 4096:
 *     - N / 4096 llamadas a read()
 *     - N / 4096 llamadas a write()
 *     = 2 * (N / 4096) context switches kernel mínimos (más open/close/stat).
 */
int sys_smart_copy(const char *src, const char *dest,
                   int flags, SmartCopyResult *result)
{
    /* --- Validación de punteros de entrada --- */
    int ret = validate_pointers(src, dest, result);
    if (ret != SC_SUCCESS) {
        return ret;
    }
    init_result(result);

    /* Declaración de variables en bloque (C89 compatibility) */
    int        fd_src     = -1;   /* Descriptor de archivo origen          */
    int        fd_dest    = -1;   /* Descriptor de archivo destino         */
    ssize_t    bytes_read  = 0;   /* Bytes leídos en cada iteración        */
    ssize_t    bytes_written = 0; /* Bytes escritos en cada iteración      */
    long       total_bytes = 0;   /* Acumulador de bytes copiados          */
    int        syscall_cnt = 0;   /* Contador de syscalls read+write       */
    struct stat src_stat;         /* Metadatos del archivo origen          */
    char        buffer[PAGE_BUFFER_SIZE]; /* Buffer de página de 4096 bytes */
    double      t_start, t_end;   /* Marcas de tiempo para benchmark       */

    /* ------------------------------------------------------------------ */
    /* SYSCALL 1: stat() — Verificar existencia y obtener metadatos        */
    /* ------------------------------------------------------------------ */
    /*
     * stat() lee el inodo del archivo desde el sistema de archivos.
     * Si el archivo no existe, errno = ENOENT.
     * Si no tenemos permiso de lectura, errno = EACCES.
     */
    if (stat(src, &src_stat) == -1) {
        if (errno == ENOENT) {
            result->error_code = SC_ERR_SRC_NOT_FOUND;
            fprintf(stderr, "[sys_smart_copy] Error: archivo origen '%s' "
                    "no encontrado: %s\n", src, strerror(errno));
            return SC_ERR_SRC_NOT_FOUND;
        }
        result->error_code = SC_ERR_OPEN_SRC;
        perror("[sys_smart_copy] Error al obtener stats del origen");
        return SC_ERR_OPEN_SRC;
    }

    /* Iniciar medición de tiempo DESPUÉS de validar la entrada */
    t_start = get_time_ms();

    /* ------------------------------------------------------------------ */
    /* SYSCALL 2: open(src, O_RDONLY) — Abrir archivo origen              */
    /* ------------------------------------------------------------------ */
    /*
     * open() devuelve un descriptor de archivo (entero >= 0).
     * El descriptor es un índice en la tabla de archivos abiertos del proceso,
     * que a su vez apunta a la tabla global de archivos del kernel.
     */
    fd_src = open(src, O_RDONLY);
    if (fd_src < 0) {
        result->error_code = SC_ERR_OPEN_SRC;
        fprintf(stderr, "[sys_smart_copy] Error al abrir origen '%s': %s\n",
                src, strerror(errno));
        return SC_ERR_OPEN_SRC;
    }

    /* ------------------------------------------------------------------ */
    /* SYSCALL 3: open(dest, ...) — Crear/abrir archivo destino           */
    /* ------------------------------------------------------------------ */
    /*
     * Flags de apertura del destino:
     *   O_WRONLY   → Solo escritura
     *   O_CREAT    → Crear si no existe
     *   O_TRUNC    → Truncar a 0 bytes si ya existe (sobreescribe)
     *
     * El modo de permisos depende del flag SC_FLAG_PRESERVE_PERMS:
     *   Si está activo → usa el modo del archivo original (src_stat.st_mode).
     *   Si no          → usa 0644 (lectura para todos, escritura solo para dueño).
     */
    mode_t dest_mode = (flags & SC_FLAG_PRESERVE_PERMS)
                       ? src_stat.st_mode
                       : (S_IRUSR | S_IWUSR | S_IRGRP | S_IROTH); /* 0644 */

    fd_dest = open(dest, O_WRONLY | O_CREAT | O_TRUNC, dest_mode);
    if (fd_dest < 0) {
        /* Detectar específicamente error de permisos (EACCES) */
        if (errno == EACCES) {
            result->error_code = SC_ERR_DST_NO_PERM;
            fprintf(stderr, "[sys_smart_copy] Error: sin permiso para escribir "
                    "en '%s': %s\n", dest, strerror(errno));
        } else {
            result->error_code = SC_ERR_OPEN_DST;
            fprintf(stderr, "[sys_smart_copy] Error al crear/abrir destino "
                    "'%s': %s\n", dest, strerror(errno));
        }
        close(fd_src); /* SIEMPRE cerrar descriptores abiertos antes de salir */
        return result->error_code;
    }

    /* ------------------------------------------------------------------ */
    /* BUCLE PRINCIPAL: read() → write() con buffer de página (4096 bytes) */
    /* ------------------------------------------------------------------ */
    /*
     * Cada iteración realiza exactamente 2 context switches al kernel:
     *   - Uno para read() (modo usuario → kernel → modo usuario)
     *   - Uno para write() (modo usuario → kernel → modo usuario)
     *
     * Con buffer de 4096 bytes, para un archivo de 1 MB necesitamos:
     *   1 048 576 / 4096 = 256 iteraciones → 512 context switches mínimos.
     *
     * Con stdio (lib_smart_copy), el buffer de 8192 bytes de FILE* reduce
     * a la mitad los context switches para archivos pequeños.
     */
    while ((bytes_read = read(fd_src, buffer, PAGE_BUFFER_SIZE)) > 0) {

        syscall_cnt++; /* Contar la llamada read() */

        /* Escritura completa: write() puede escribir menos que lo pedido */
        ssize_t total_written = 0;
        while (total_written < bytes_read) {
            bytes_written = write(fd_dest,
                                  buffer + total_written,
                                  (size_t)(bytes_read - total_written));

            if (bytes_written < 0) {
                /* Detectar específicamente disco lleno (ENOSPC) */
                if (errno == ENOSPC) {
                    result->error_code = SC_ERR_DISK_FULL;
                    fprintf(stderr, "[sys_smart_copy] Error: disco lleno al "
                            "escribir en '%s'\n", dest);
                } else {
                    result->error_code = SC_ERR_WRITE;
                    fprintf(stderr, "[sys_smart_copy] Error de escritura en "
                            "'%s': %s\n", dest, strerror(errno));
                }
                /* Cerrar descriptores antes de retornar */
                close(fd_src);
                close(fd_dest);
                result->elapsed_ms    = get_time_ms() - t_start;
                result->bytes_copied  = total_bytes;
                result->syscall_count = syscall_cnt;
                return result->error_code;
            }

            total_written += bytes_written;
            syscall_cnt++;  /* Contar la llamada write() */
        }

        total_bytes += bytes_read;

        /* Modo verbose: mostrar progreso cada 4096 bytes */
        if (flags & SC_FLAG_VERBOSE) {
            printf("[sys_smart_copy] Copiados %ld bytes...\r", total_bytes);
            fflush(stdout);
        }
    }

    /* Verificar si read() terminó por error o por EOF */
    if (bytes_read < 0) {
        result->error_code = SC_ERR_READ;
        fprintf(stderr, "[sys_smart_copy] Error de lectura en '%s': %s\n",
                src, strerror(errno));
        close(fd_src);
        close(fd_dest);
        result->elapsed_ms    = get_time_ms() - t_start;
        result->bytes_copied  = total_bytes;
        result->syscall_count = syscall_cnt;
        return SC_ERR_READ;
    }

    /* ------------------------------------------------------------------ */
    /* SYSCALL OPCIONAL: fsync() — Forzar escritura a disco físico        */
    /* ------------------------------------------------------------------ */
    /*
     * Por defecto, write() escribe en el page cache del kernel (memoria RAM).
     * El kernel decide CUÁNDO bajar esos datos al disco (dirty page writeback).
     * fsync() fuerza esa escritura inmediatamente, garantizando durabilidad.
     * Costo: puede ser muy lento (milisegundos) dependiendo del disco.
     */
    if (flags & SC_FLAG_SYNC) {
        if (fsync(fd_dest) == -1) {
            perror("[sys_smart_copy] Advertencia: fsync() falló");
            /* No es error fatal, continuamos con el cierre */
        }
    }

    /* ------------------------------------------------------------------ */
    /* SYSCALLS FINALES: close() — Liberar descriptores de archivo        */
    /* ------------------------------------------------------------------ */
    close(fd_src);
    close(fd_dest);

    /* ------------------------------------------------------------------ */
    /* Registrar resultados finales                                        */
    /* ------------------------------------------------------------------ */
    t_end = get_time_ms();
    result->elapsed_ms    = t_end - t_start;
    result->bytes_copied  = total_bytes;
    result->error_code    = SC_SUCCESS;
    result->syscall_count = syscall_cnt;

    if (flags & SC_FLAG_VERBOSE) {
        printf("\n");
    }

    return SC_SUCCESS;
}

/* =========================================================================
 * IMPLEMENTACIÓN 2: lib_smart_copy (Librería estándar stdio)
 * =========================================================================
 */

/*
 * lib_smart_copy()
 * ----------------
 * Copia un archivo usando la capa de abstracción de stdio.h.
 *
 * Diferencia arquitectural clave respecto a sys_smart_copy:
 *
 *  stdio añade un BUFFER EN ESPACIO DE USUARIO (dentro del struct FILE*).
 *  Por defecto, este buffer tiene 8192 bytes en glibc (BUFSIZ).
 *
 *  Cuando fread() lee 4096 bytes:
 *   a) Si los datos ya están en el buffer de FILE*, los copia en memoria
 *      → CERO syscalls adicionales al kernel.
 *   b) Si el buffer está vacío, llama a read() internamente con BUFSIZ bytes
 *      → 1 context switch, pero trae más datos de los pedidos.
 *
 *  Para archivos PEQUEÑOS (< BUFSIZ), esto hace que stdio sea MÁS EFICIENTE
 *  que las syscalls directas, porque necesita menos context switches.
 *
 *  Para archivos GRANDES (>> BUFSIZ), ambos métodos se equiparan en throughput
 *  porque el cuello de botella pasa a ser el disco, no el context switch.
 */
int lib_smart_copy(const char *src, const char *dest,
                   int flags, SmartCopyResult *result)
{
    /* --- Validación de punteros de entrada --- */
    int ret = validate_pointers(src, dest, result);
    if (ret != SC_SUCCESS) {
        return ret;
    }
    init_result(result);

    FILE    *fp_src    = NULL;  /* Stream de origen                        */
    FILE    *fp_dest   = NULL;  /* Stream de destino                       */
    size_t   bytes_read   = 0;  /* Bytes leídos por fread en cada llamada  */
    size_t   bytes_written = 0; /* Bytes escritos por fwrite               */
    long     total_bytes   = 0; /* Acumulador total de bytes               */
    int      call_count    = 0; /* Contador de llamadas fread+fwrite       */
    char     buffer[PAGE_BUFFER_SIZE]; /* Buffer de usuario (4096 bytes)   */
    double   t_start, t_end;

    /* Verificar que el archivo origen existe (usando acceso de POSIX) */
    struct stat src_stat;
    if (stat(src, &src_stat) == -1) {
        result->error_code = SC_ERR_SRC_NOT_FOUND;
        fprintf(stderr, "[lib_smart_copy] Error: archivo origen '%s' "
                "no encontrado: %s\n", src, strerror(errno));
        return SC_ERR_SRC_NOT_FOUND;
    }

    t_start = get_time_ms();

    /* --- Abrir archivo origen en modo binario --- */
    /*
     * "rb" = lectura binaria.
     * En Linux no hay diferencia entre texto y binario (CRLF no aplica),
     * pero es buena práctica para código portable.
     */
    fp_src = fopen(src, "rb");
    if (fp_src == NULL) {
        result->error_code = SC_ERR_OPEN_SRC;
        fprintf(stderr, "[lib_smart_copy] Error al abrir origen '%s': %s\n",
                src, strerror(errno));
        return SC_ERR_OPEN_SRC;
    }

    /* --- Abrir/crear archivo destino en modo binario --- */
    fp_dest = fopen(dest, "wb");
    if (fp_dest == NULL) {
        result->error_code = (errno == EACCES)
                             ? SC_ERR_DST_NO_PERM
                             : SC_ERR_OPEN_DST;
        fprintf(stderr, "[lib_smart_copy] Error al crear destino '%s': %s\n",
                dest, strerror(errno));
        fclose(fp_src);
        return result->error_code;
    }

    /* --- Bucle de copia con fread/fwrite --- */
    while ((bytes_read = fread(buffer, 1, PAGE_BUFFER_SIZE, fp_src)) > 0) {

        call_count++; /* Contar llamada fread */

        bytes_written = fwrite(buffer, 1, bytes_read, fp_dest);

        if (bytes_written < bytes_read) {
            /* fwrite retornó menos de lo esperado → error */
            if (errno == ENOSPC) {
                result->error_code = SC_ERR_DISK_FULL;
                fprintf(stderr, "[lib_smart_copy] Error: disco lleno.\n");
            } else {
                result->error_code = SC_ERR_WRITE;
                fprintf(stderr, "[lib_smart_copy] Error de escritura: %s\n",
                        strerror(errno));
            }
            fclose(fp_src);
            fclose(fp_dest);
            result->elapsed_ms    = get_time_ms() - t_start;
            result->bytes_copied  = total_bytes;
            result->syscall_count = call_count;
            return result->error_code;
        }

        call_count++; /* Contar llamada fwrite */
        total_bytes += (long)bytes_read;

        if (flags & SC_FLAG_VERBOSE) {
            printf("[lib_smart_copy] Copiados %ld bytes...\r", total_bytes);
            fflush(stdout);
        }
    }

    /* Verificar si fread terminó por error o por EOF */
    if (ferror(fp_src)) {
        result->error_code = SC_ERR_READ;
        fprintf(stderr, "[lib_smart_copy] Error de lectura en '%s'.\n", src);
        fclose(fp_src);
        fclose(fp_dest);
        result->elapsed_ms    = get_time_ms() - t_start;
        result->bytes_copied  = total_bytes;
        result->syscall_count = call_count;
        return SC_ERR_READ;
    }

    /* Preservar permisos si se solicitó */
    if (flags & SC_FLAG_PRESERVE_PERMS) {
        chmod(dest, src_stat.st_mode);
    }

    fclose(fp_src);
    fclose(fp_dest);

    t_end = get_time_ms();
    result->elapsed_ms    = t_end - t_start;
    result->bytes_copied  = total_bytes;
    result->error_code    = SC_SUCCESS;
    result->syscall_count = call_count;

    if (flags & SC_FLAG_VERBOSE) {
        printf("\n");
    }

    return SC_SUCCESS;
}

/* =========================================================================
 * FUNCIONES DE UTILIDAD
 * =========================================================================
 */

/*
 * sc_strerror()
 * Traduce códigos de error internos a mensajes descriptivos.
 */
const char *sc_strerror(int error_code)
{
    switch (error_code) {
        case SC_SUCCESS:           return "Operación exitosa";
        case SC_ERR_SRC_NOT_FOUND: return "Archivo origen no encontrado";
        case SC_ERR_DST_NO_PERM:   return "Sin permiso de escritura en destino";
        case SC_ERR_OPEN_SRC:      return "Error al abrir archivo origen";
        case SC_ERR_OPEN_DST:      return "Error al crear/abrir archivo destino";
        case SC_ERR_READ:          return "Error de lectura";
        case SC_ERR_WRITE:         return "Error de escritura";
        case SC_ERR_DISK_FULL:     return "Disco lleno (ENOSPC)";
        case SC_ERR_NULL_PTR:      return "Puntero nulo invalido";
        default:                   return "Error desconocido";
    }
}

/*
 * sc_print_result()
 * Imprime un resumen formateado del resultado de una copia.
 */
void sc_print_result(const char *label, const SmartCopyResult *result)
{
    if (result == NULL) return;

    printf("  [%-8s] Estado: %-30s | Bytes: %8ld | Tiempo: %8.3f ms | "
           "Llamadas I/O: %d\n",
           label,
           sc_strerror(result->error_code),
           result->bytes_copied,
           result->elapsed_ms,
           result->syscall_count);
}