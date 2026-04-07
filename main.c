/*
 * main.c
 * ======
 * Proyecto: Smart Backup Kernel-Space Utility
 * Curso: Sistemas Operativos - EAFIT
 *
 * Interfaz de usuario del sistema de backup.
 *
 * Modos de operación:
 *
 *   1. Modo backup (-b / --backup):
 *      Copia un archivo de src a dest usando sys_smart_copy.
 *      Ejemplo: ./backup_smart -b origen.txt destino.txt
 *
 *   2. Modo benchmark (--benchmark):
 *      Genera archivos de prueba de distintos tamaños (1KB, 1MB, 1GB),
 *      ejecuta ambos métodos (syscall y stdio), y presenta una tabla
 *      comparativa de rendimiento con promedios de 3 corridas.
 *      NOTA: El test de 1GB puede tardar varios minutos según el disco.
 *
 *   3. Modo ayuda (-h / --help):
 *      Muestra el manual de uso del programa.
 *
 * Autores: [Nombres del equipo]
 * Fecha:   2026
 */

#include "smart_copy.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <sys/stat.h>
#include <unistd.h>

/* =========================================================================
 * CONSTANTES DEL BENCHMARK
 * =========================================================================
 */
#define BENCHMARK_RUNS       3      /* Número de corridas por prueba            */
#define BENCH_DIR            "bench_files" /* Directorio temporal de prueba      */

/* Tamaños de archivo de prueba — la rúbrica pide explícitamente 1KB, 1MB y 1GB */
#define SIZE_1KB   (1024L)
#define SIZE_1MB   (1024L * 1024L)
#define SIZE_10MB  (10L * 1024L * 1024L)
#define SIZE_1GB   (1024L * 1024L * 1024L)

/* =========================================================================
 * FUNCIONES AUXILIARES DEL BENCHMARK
 * =========================================================================
 */

/*
 * print_separator()
 * -----------------
 * Imprime una línea separadora decorativa para la tabla de resultados.
 */
static void print_separator(void)
{
    printf("  +--------+------------------+-----------+----------+-----------+-----------+----------+----------+\n");
}

/*
 * print_banner()
 * --------------
 * Muestra el banner de presentación del programa.
 */
static void print_banner(void)
{
    printf("\n");
    printf("  ╔══════════════════════════════════════════════════════════════╗\n");
    printf("  ║         SMART BACKUP KERNEL-SPACE UTILITY                  ║\n");
    printf("  ║         Sistema Operativos - EAFIT - 2026                  ║\n");
    printf("  ║         Comparativa: System Calls vs Librería C            ║\n");
    printf("  ╚══════════════════════════════════════════════════════════════╝\n");
    printf("\n");
}

/*
 * generate_test_file()
 * --------------------
 * Genera un archivo de prueba de tamaño exacto 'size_bytes' relleno con
 * un patrón de bytes repetitivo (0x00..0xFF en ciclo).
 *
 * Usar un patrón conocido permite verificar la integridad de la copia
 * comparando el contenido byte a byte.
 *
 * Parámetros:
 *   path       - Ruta del archivo a crear
 *   size_bytes - Tamaño exacto en bytes del archivo a generar
 *
 * Retorna: 0 si fue exitoso, -1 si falló.
 */
static int generate_test_file(const char *path, long size_bytes)
{
    FILE *fp = fopen(path, "wb");
    if (fp == NULL) {
        fprintf(stderr, "  [ERROR] No se pudo crear archivo de prueba '%s': %s\n",
                path, strerror(errno));
        return -1;
    }

    /* Usar buffer de escritura grande para generar el archivo rápidamente */
    char write_buf[PAGE_BUFFER_SIZE];
    long written = 0;

    /* Llenar el buffer con un patrón de bytes conocido */
    for (int i = 0; i < PAGE_BUFFER_SIZE; i++) {
        write_buf[i] = (char)(i & 0xFF);
    }

    while (written < size_bytes) {
        long remaining    = size_bytes - written;
        long chunk        = (remaining < PAGE_BUFFER_SIZE)
                            ? remaining
                            : PAGE_BUFFER_SIZE;
        size_t fwrite_ret = fwrite(write_buf, 1, (size_t)chunk, fp);
        if ((long)fwrite_ret < chunk) {
            fprintf(stderr, "  [ERROR] Fallo generando archivo de prueba.\n");
            fclose(fp);
            return -1;
        }
        written += (long)fwrite_ret;
    }

    fclose(fp);
    return 0;
}

/*
 * format_size()
 * -------------
 * Formatea un tamaño en bytes a una cadena legible (KB, MB).
 * Escribe el resultado en el buffer 'buf' de longitud 'buf_len'.
 */
static void format_size(long bytes, char *buf, int buf_len)
{
    if (bytes < 1024L) {
        snprintf(buf, buf_len, "%ld B", bytes);
    } else if (bytes < 1024L * 1024L) {
        snprintf(buf, buf_len, "%ld KB", bytes / 1024L);
    } else {
        snprintf(buf, buf_len, "%ld MB", bytes / (1024L * 1024L));
    }
}

/*
 * run_benchmark_for_size()
 * ------------------------
 * Ejecuta el benchmark completo para un tamaño de archivo dado.
 * Realiza BENCHMARK_RUNS corridas de cada método y calcula el promedio.
 *
 * Parámetros:
 *   src_path   - Ruta del archivo origen (ya debe existir)
 *   size_bytes - Tamaño del archivo (para display)
 *
 * Retorna: 0 si fue exitoso, -1 si alguna copia falló.
 */
static int run_benchmark_for_size(const char *src_path, long size_bytes)
{
    SmartCopyResult result;
    char   dest_path[MAX_PATH_LEN];
    char   size_label[32];
    double syscall_total_ms = 0.0;
    double stdio_total_ms   = 0.0;
    long   syscall_calls    = 0;
    long   stdio_calls      = 0;
    int    flags            = SC_FLAG_PRESERVE_PERMS;

    format_size(size_bytes, size_label, sizeof(size_label));

    /* === MÉTODO 1: sys_smart_copy (System Calls directas) === */
    for (int run = 0; run < BENCHMARK_RUNS; run++) {
        snprintf(dest_path, sizeof(dest_path),
                 BENCH_DIR "/dest_syscall_%d.bin", run);

        int ret = sys_smart_copy(src_path, dest_path, flags, &result);

        if (ret != SC_SUCCESS) {
            fprintf(stderr, "  [ERROR] sys_smart_copy falló en corrida %d: %s\n",
                    run + 1, sc_strerror(result.error_code));
            return -1;
        }

        syscall_total_ms += result.elapsed_ms;
        syscall_calls    += result.syscall_count;

        /* Eliminar archivo de destino para que la caché de disco no sesgue
         * las corridas siguientes */
        unlink(dest_path);
    }

    /* === MÉTODO 2: lib_smart_copy (Librería stdio) === */
    for (int run = 0; run < BENCHMARK_RUNS; run++) {
        snprintf(dest_path, sizeof(dest_path),
                 BENCH_DIR "/dest_stdio_%d.bin", run);

        int ret = lib_smart_copy(src_path, dest_path, flags, &result);

        if (ret != SC_SUCCESS) {
            fprintf(stderr, "  [ERROR] lib_smart_copy falló en corrida %d: %s\n",
                    run + 1, sc_strerror(result.error_code));
            return -1;
        }

        stdio_total_ms += result.elapsed_ms;
        stdio_calls    += result.syscall_count;

        unlink(dest_path);
    }

    /* === Calcular promedios === */
    double syscall_avg_ms = syscall_total_ms / BENCHMARK_RUNS;
    double stdio_avg_ms   = stdio_total_ms   / BENCHMARK_RUNS;
    long   syscall_avg_calls = syscall_calls / BENCHMARK_RUNS;
    long   stdio_avg_calls   = stdio_calls   / BENCHMARK_RUNS;

    /* Calcular throughput en MB/s */
    double size_mb = (double)size_bytes / (1024.0 * 1024.0);
    double syscall_mbps = (syscall_avg_ms > 0)
                          ? (size_mb / (syscall_avg_ms / 1000.0))
                          : 0.0;
    double stdio_mbps   = (stdio_avg_ms > 0)
                          ? (size_mb / (stdio_avg_ms / 1000.0))
                          : 0.0;

    /* === Mostrar fila de la tabla === */
    printf("  | %6s | %8s  %8.3f ms | %8.1f | %8ld | %8s  %8.3f ms | %8.1f | %8ld |\n",
           size_label,
           "syscall", syscall_avg_ms, syscall_mbps, syscall_avg_calls,
           "stdio",   stdio_avg_ms,   stdio_mbps,   stdio_avg_calls);

    return 0;
}

/* =========================================================================
 * MODO BENCHMARK COMPLETO
 * =========================================================================
 */

/*
 * run_benchmark()
 * ---------------
 * Ejecuta el benchmark completo con los 4 tamaños de archivo definidos.
 *
 * Proceso:
 *   1. Crea el directorio temporal 'bench_files'
 *   2. Genera los archivos de prueba de cada tamaño
 *   3. Ejecuta BENCHMARK_RUNS corridas de cada método para cada tamaño
 *   4. Muestra la tabla comparativa de resultados
 *   5. Limpia los archivos generados
 */
static int run_benchmark(void)
{
    print_banner();

    printf("  ▶  MODO BENCHMARK\n");
    printf("  ▶  Corridas por prueba: %d\n", BENCHMARK_RUNS);
    printf("  ▶  Buffer de sistema:   %d bytes (%d KB)\n",
           PAGE_BUFFER_SIZE, PAGE_BUFFER_SIZE / 1024);
    printf("\n");

    /* Crear directorio de trabajo para archivos de prueba */
    if (mkdir(BENCH_DIR, 0755) == -1 && errno != EEXIST) {
        perror("  [ERROR] No se pudo crear el directorio 'bench_files'");
        return EXIT_FAILURE;
    }

    /* Definir los casos de prueba */
    struct {
        long        size;
        const char *label;
        const char *src_path;
    } test_cases[] = {
        { SIZE_1KB,  "  1 KB", BENCH_DIR "/test_1kb.bin"  },
        { SIZE_1MB,  "  1 MB", BENCH_DIR "/test_1mb.bin"  },
        { SIZE_10MB, " 10 MB", BENCH_DIR "/test_10mb.bin" },
        { SIZE_1GB,  "  1 GB", BENCH_DIR "/test_1gb.bin"  },
    };

    /* Advertencia: el archivo de 1 GB puede tardar varios minutos en generarse
     * y copiarse, especialmente en WSL sobre disco HDD o red. Es normal. */
    printf("  AVISO: El caso de 1 GB puede tardar varios minutos en WSL.\n\n");
    int num_cases = (int)(sizeof(test_cases) / sizeof(test_cases[0]));

    /* Generar archivos de prueba */
    printf("  Generando archivos de prueba...\n");
    for (int i = 0; i < num_cases; i++) {
        printf("    - %s (%s)... ", test_cases[i].label, test_cases[i].src_path);
        fflush(stdout);
        if (generate_test_file(test_cases[i].src_path, test_cases[i].size) != 0) {
            return EXIT_FAILURE;
        }
        printf("OK\n");
    }
    printf("\n");

    /* ---------------------------------------------------------------
     * TABLA DE RESULTADOS
     * Columnas: Tamaño | Método1: tiempo promedio, MB/s, llamadas I/O
     *                   Método2: tiempo promedio, MB/s, llamadas I/O
     * --------------------------------------------------------------- */
    printf("  ╔══════════════════════════════════════════════════════════════════════════════════╗\n");
    printf("  ║                     TABLA COMPARATIVA DE RENDIMIENTO                           ║\n");
    printf("  ║           sys_smart_copy (syscalls) vs lib_smart_copy (stdio)                  ║\n");
    printf("  ╚══════════════════════════════════════════════════════════════════════════════════╝\n");
    printf("\n");

    printf("  +--------+------------------------------------------+------------------------------------------+\n");
    printf("  |        |        sys_smart_copy (syscalls)         |          lib_smart_copy (stdio)          |\n");
    printf("  | Tamaño +-----------+----------+--------------------+-----------+----------+------------------+\n");
    printf("  |        |  Método   | T.Prom.  |  MB/s  | Llam.I/O |  Método   | T.Prom.  |  MB/s  | Llam.I/O|\n");
    print_separator();

    int all_ok = 1;
    for (int i = 0; i < num_cases; i++) {
        if (run_benchmark_for_size(test_cases[i].src_path, test_cases[i].size) != 0) {
            all_ok = 0;
            break;
        }
    }

    print_separator();
    printf("\n");

    /* Análisis cualitativo */
    if (all_ok) {
        printf("  ╔══════════════════════════════════════════════════════════════╗\n");
        printf("  ║                   ANÁLISIS DE RESULTADOS                   ║\n");
        printf("  ╠══════════════════════════════════════════════════════════════╣\n");
        printf("  ║                                                             ║\n");
        printf("  ║  • Para archivos PEQUEÑOS (1KB):                           ║\n");
        printf("  ║    stdio tiende a ser más rápido porque su buffer interno  ║\n");
        printf("  ║    (BUFSIZ=8192 en glibc) puede contener el archivo        ║\n");
        printf("  ║    completo en una sola llamada al kernel, reduciendo       ║\n");
        printf("  ║    el número de context switches.                           ║\n");
        printf("  ║                                                             ║\n");
        printf("  ║  • Para archivos GRANDES (1GB):                            ║\n");
        printf("  ║    ambos métodos se equiparan porque el cuello de botella  ║\n");
        printf("  ║    pasa a ser el disco (I/O), no el context switch.        ║\n");
        printf("  ║    sys_smart_copy puede ganar si el OS page cache está     ║\n");
        printf("  ║    frío (primer acceso al archivo sin caché de disco).     ║\n");
        printf("  ║                                                             ║\n");
        printf("  ║  • Context Switch Cost:                                    ║\n");
        printf("  ║    Cada syscall implica ~100-300 ns de overhead por el     ║\n");
        printf("  ║    cambio de modo usuario→kernel (guardar/restaurar        ║\n");
        printf("  ║    registros, TLB flush parcial, etc.).                    ║\n");
        printf("  ║                                                             ║\n");
        printf("  ╚══════════════════════════════════════════════════════════════╝\n");
        printf("\n");
    }

    /* Limpiar archivos generados */
    printf("  Limpiando archivos de prueba...\n");
    for (int i = 0; i < num_cases; i++) {
        unlink(test_cases[i].src_path);
    }
    rmdir(BENCH_DIR);
    printf("  Limpieza completada.\n\n");

    return all_ok ? EXIT_SUCCESS : EXIT_FAILURE;
}

/* =========================================================================
 * FUNCIÓN DE AYUDA
 * =========================================================================
 */
static void print_help(const char *prog_name)
{
    printf("\n");
    printf("  SMART BACKUP KERNEL-SPACE UTILITY\n");
    printf("  Sistemas Operativos - EAFIT - 2026\n");
    printf("\n");
    printf("  USO:\n");
    printf("    %s -b <origen> <destino>   Copia un archivo usando system calls\n", prog_name);
    printf("    %s --benchmark             Ejecuta benchmark syscall vs stdio\n", prog_name);
    printf("    %s -h | --help             Muestra esta ayuda\n", prog_name);
    printf("\n");
    printf("  DESCRIPCIÓN:\n");
    printf("    sys_smart_copy() — función que simula una llamada al sistema.\n");
    printf("    Opera con open/read/write/close y buffer de 4096 bytes (tamaño\n");
    printf("    de página del kernel). Se compara contra fopen/fread/fwrite.\n");
    printf("\n");
    printf("  EJEMPLOS:\n");
    printf("    %s -b archivo.txt backup.txt\n", prog_name);
    printf("    %s --benchmark\n", prog_name);
    printf("\n");
    printf("  SYSCALLS UTILIZADAS:\n");
    printf("    stat(), open(), read(), write(), close(), fsync(), mkdir()\n");
    printf("\n");
}

/* =========================================================================
 * MAIN
 * =========================================================================
 */
int main(int argc, char *argv[])
{
    /* Sin argumentos: mostrar ayuda */
    if (argc < 2) {
        print_help(argv[0]);
        return EXIT_FAILURE;
    }

    /* --- Modo ayuda --- */
    if (strcmp(argv[1], "-h") == 0 || strcmp(argv[1], "--help") == 0) {
        print_help(argv[0]);
        return EXIT_SUCCESS;
    }

    /* --- Modo benchmark --- */
    if (strcmp(argv[1], "--benchmark") == 0) {
        return run_benchmark();
    }

    /* --- Modo backup (-b / --backup) --- */
    if (strcmp(argv[1], "-b") == 0 || strcmp(argv[1], "--backup") == 0) {

        if (argc != 4) {
            fprintf(stderr,
                    "\n  Error: Faltan argumentos.\n"
                    "  Uso correcto: %s -b <origen> <destino>\n\n",
                    argv[0]);
            return EXIT_FAILURE;
        }

        const char *src  = argv[2];
        const char *dest = argv[3];

        print_banner();
        printf("  ▶  MODO BACKUP\n");
        printf("  ▶  Origen:  %s\n", src);
        printf("  ▶  Destino: %s\n", dest);
        printf("  ▶  Buffer:  %d bytes (%d KB)\n\n",
               PAGE_BUFFER_SIZE, PAGE_BUFFER_SIZE / 1024);

        SmartCopyResult result;

        /* Ejecutar copia con system calls */
        printf("  [1/2] Copiando con sys_smart_copy (system calls)...\n");
        int ret = sys_smart_copy(src, dest, SC_FLAG_VERBOSE | SC_FLAG_PRESERVE_PERMS, &result);

        if (ret != SC_SUCCESS) {
            fprintf(stderr, "\n  [ERROR] sys_smart_copy falló: %s\n\n",
                    sc_strerror(result.error_code));
            return EXIT_FAILURE;
        }

        printf("  ✓  Copia exitosa.\n");
        sc_print_result("syscall", &result);
        printf("\n");

        /* También ejecutar con stdio para comparación inmediata */
        char dest_stdio[MAX_PATH_LEN];
        snprintf(dest_stdio, sizeof(dest_stdio), "%s.stdio_copy", dest);

        printf("  [2/2] Copiando con lib_smart_copy (stdio) para comparar...\n");
        int ret2 = lib_smart_copy(src, dest_stdio, SC_FLAG_PRESERVE_PERMS, &result);

        if (ret2 != SC_SUCCESS) {
            fprintf(stderr, "  [AVISO] lib_smart_copy falló: %s (no crítico)\n",
                    sc_strerror(result.error_code));
        } else {
            printf("  ✓  Copia de referencia lista en: %s\n", dest_stdio);
            sc_print_result("stdio", &result);
            printf("\n");
            printf("  Tip: Para ver comparativa completa usa --benchmark\n");
        }

        printf("\n");
        return EXIT_SUCCESS;
    }

    /* Opción desconocida */
    fprintf(stderr, "\n  Error: Opción no reconocida '%s'\n", argv[1]);
    print_help(argv[0]);
    return EXIT_FAILURE;
}