/*
 * smart_copy.h
 * ============
 * Proyecto: Smart Backup Kernel-Space Utility
 * Curso: Sistemas Operativos - EAFIT
 *
 * Definición de la interfaz pública de sys_smart_copy.
 *
 * Esta cabecera simula cómo se definiría la firma de una llamada al sistema
 * real en el kernel de Linux. En un kernel real, la función estaría declarada
 * en include/linux/syscalls.h y su firma precedida por el macro asmlinkage.
 *
 * Aquí la simulamos como una función en espacio de usuario que usa las mismas
 * primitivas de bajo nivel (open, read, write, close) que el kernel usaría
 * internamente para implementar operaciones de E/S sobre archivos.
 *
 * Autores: [Nombres del equipo]
 * Fecha:   2026
 */

#ifndef SMART_COPY_H
#define SMART_COPY_H

#include <sys/types.h>   /* ssize_t, off_t */
#include <time.h>        /* struct timespec */

/* =========================================================================
 * CONSTANTES DEL SISTEMA
 * =========================================================================
 *
 * PAGE_BUFFER_SIZE: Tamaño del buffer de transferencia = 4096 bytes (4 KB).
 *
 * Este valor no es arbitrario. Corresponde exactamente al tamaño de una
 * página de memoria en la arquitectura x86/x86-64 (PAGE_SIZE en el kernel).
 *
 * Usar un buffer del tamaño de página maximiza el throughput porque:
 *   1. El kernel trabaja con páginas como unidad mínima de memoria.
 *   2. Una sola llamada a read() puede transferir exactamente una página,
 *      evitando lecturas parciales que lleven a más context switches.
 *   3. Alinea las transferencias al cache de disco (sector size múltiplo de 4KB).
 */
#define PAGE_BUFFER_SIZE   4096   /* Tamaño de página del kernel (bytes) */
#define MAX_PATH_LEN       4096   /* Longitud máxima de ruta de archivo    */
#define LOG_BUFFER_SIZE    256    /* Buffer para mensajes de log           */

/* =========================================================================
 * FLAGS DE COMPORTAMIENTO
 * =========================================================================
 * Los flags se diseñan como bits individuales para poder combinarlos con OR.
 * Esto replica el patrón usado en las flags reales de open() (O_RDONLY, etc.)
 */
#define SC_FLAG_NONE           0x00  /* Sin opciones especiales          */
#define SC_FLAG_VERBOSE        0x01  /* Mostrar progreso detallado       */
#define SC_FLAG_PRESERVE_PERMS 0x02  /* Preservar permisos del original  */
#define SC_FLAG_OVERWRITE      0x04  /* Sobrescribir destino si existe   */
#define SC_FLAG_SYNC           0x08  /* Forzar fsync() al cerrar         */

/* =========================================================================
 * CÓDIGOS DE RETORNO (simulando errno del kernel)
 * =========================================================================
 */
#define SC_SUCCESS              0   /* Operación exitosa                  */
#define SC_ERR_SRC_NOT_FOUND   -1   /* Archivo origen no existe          */
#define SC_ERR_DST_NO_PERM     -2   /* Sin permiso de escritura en dest  */
#define SC_ERR_OPEN_SRC        -3   /* Error al abrir archivo origen     */
#define SC_ERR_OPEN_DST        -4   /* Error al abrir/crear archivo dest */
#define SC_ERR_READ            -5   /* Error durante lectura             */
#define SC_ERR_WRITE           -6   /* Error durante escritura           */
#define SC_ERR_DISK_FULL       -7   /* Disco lleno (ENOSPC)              */
#define SC_ERR_NULL_PTR        -8   /* Puntero nulo recibido             */

/* =========================================================================
 * ESTRUCTURAS DE DATOS
 * =========================================================================
 */

/*
 * SmartCopyResult
 * ---------------
 * Estructura que encapsula el resultado de una operación de copia.
 * Una función de sistema real retornaría esta información a través de
 * parámetros de salida (punteros) o registros del procesador.
 */
typedef struct {
    long   bytes_copied;   /* Bytes totales copiados exitosamente      */
    double elapsed_ms;     /* Tiempo de ejecución en milisegundos      */
    int    error_code;     /* Código de error (SC_SUCCESS si OK)       */
    int    syscall_count;  /* Número de syscalls (read+write) usadas   */
} SmartCopyResult;

/* =========================================================================
 * PROTOTIPOS DE FUNCIONES
 * =========================================================================
 */

/*
 * sys_smart_copy()
 * ----------------
 * Función que simula una llamada al sistema para copia de archivos.
 *
 * Opera exclusivamente con system calls de bajo nivel (open, read, write,
 * close, stat) usando un buffer de PAGE_BUFFER_SIZE bytes.
 *
 * Parámetros:
 *   src    - Ruta del archivo origen (puntero a cadena constante)
 *   dest   - Ruta del archivo destino (puntero a cadena constante)
 *   flags  - Combinación de SC_FLAG_* con operador OR bit a bit
 *   result - Puntero a SmartCopyResult donde se almacena el resultado
 *
 * Retorna: SC_SUCCESS (0) si la copia fue exitosa, código negativo si no.
 *
 * Garantías:
 *   - Cierra todos los descriptores de archivo abiertos, incluso en error.
 *   - Si falla la escritura, NO deja el archivo destino en estado corrupto
 *     (lo trunca antes del error si es posible).
 *   - Valida todos los punteros de entrada antes de usarlos.
 */
int sys_smart_copy(const char *src, const char *dest,
                   int flags, SmartCopyResult *result);

/*
 * lib_smart_copy()
 * ----------------
 * Implementación equivalente usando la librería estándar de C (stdio.h).
 *
 * Usa fopen/fread/fwrite/fclose para demostrar la diferencia arquitectural
 * con sys_smart_copy: las funciones de biblioteca agregan una capa de
 * buffering en espacio de usuario (FILE*), reduciendo el número de
 * context switches al kernel pero añadiendo overhead de copia de memoria.
 *
 * Mismo contrato de parámetros que sys_smart_copy().
 */
int lib_smart_copy(const char *src, const char *dest,
                   int flags, SmartCopyResult *result);

/*
 * sc_strerror()
 * -------------
 * Convierte un código de error SC_ERR_* a una cadena descriptiva.
 * Análogo a strerror() de la librería estándar.
 */
const char *sc_strerror(int error_code);

/*
 * sc_print_result()
 * -----------------
 * Imprime en stdout un resumen formateado de SmartCopyResult.
 * Útil para el modo verbose y el benchmark.
 *
 * Parámetros:
 *   label  - Etiqueta descriptiva del método (ej: "syscall" o "stdio")
 *   result - Puntero al resultado a mostrar
 */
void sc_print_result(const char *label, const SmartCopyResult *result);

#endif /* SMART_COPY_H */
