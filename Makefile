# ============================================================================
# Makefile — Smart Backup Kernel-Space Utility
# Sistemas Operativos - EAFIT - 2026
#
# TARGETS:
#   make all        — Compila el proyecto (backup_smart)
#   make benchmark  — Compila y ejecuta la prueba de rendimiento completa
#   make test       — Prueba rápida de copia de un archivo simple
#   make clean      — Elimina ejecutables y archivos generados
#   make help       — Muestra esta ayuda
#
# NOTA: Este Makefile está optimizado para compilar en Linux/WSL.
#       Requiere gcc instalado (sudo apt install gcc).
# ============================================================================

CC      = gcc
CFLAGS  = -Wall -Wextra -g -std=c11
TARGET  = backup_smart

# Archivos fuente del nuevo proyecto modular
SRCS    = main.c backup_engine.c
HDRS    = smart_copy.h

# Archivo fuente del código base del profesor (referencia, no se elimina)
LEGACY_SRC  = backup.c
LEGACY_BIN  = backup_EAFITos

# ============================================================================
# TARGET PRINCIPAL: compilar backup_smart
# ============================================================================

all: $(TARGET)

$(TARGET): $(SRCS) $(HDRS)
	@echo "  [CC]  Compilando $(TARGET)..."
	$(CC) $(CFLAGS) -o $(TARGET) $(SRCS)
	@echo "  [OK]  Compilado exitosamente: ./$(TARGET)"
	@echo ""
	@echo "  Uso: ./$(TARGET) -b <origen> <destino>"
	@echo "       ./$(TARGET) --benchmark"
	@echo "       ./$(TARGET) --help"
	@echo ""

# ============================================================================
# TARGET LEGACY: compilar el código base del profesor (backup.c)
# ============================================================================

legacy: $(LEGACY_SRC)
	@echo "  [CC]  Compilando código base del profesor..."
	$(CC) $(CFLAGS) -o $(LEGACY_BIN) $(LEGACY_SRC)
	@echo "  [OK]  Compilado: ./$(LEGACY_BIN)"

# ============================================================================
# TARGET BENCHMARK: compilar y ejecutar prueba de rendimiento completa
# ============================================================================

benchmark: $(TARGET)
	@echo ""
	@echo "  ============================================================"
	@echo "  Ejecutando benchmark: syscalls vs stdio"
	@echo "  Buffer de página: 4096 bytes | Corridas: 3 por prueba"
	@echo "  ============================================================"
	@echo ""
	./$(TARGET) --benchmark

# ============================================================================
# TARGET TEST: prueba rápida de copia de un archivo simple
# ============================================================================

test: $(TARGET)
	@echo ""
	@echo "  --- [1] PREPARANDO ARCHIVO DE PRUEBA ---"
	echo "Este es el contenido del archivo de prueba para SmartCopy." > test_src.txt
	echo "Línea 2: Verificando integridad de la copia." >> test_src.txt
	echo "Línea 3: Sistema Operativos EAFIT 2026." >> test_src.txt
	@echo ""

	@echo "  --- [2] EJECUTANDO COPIA CON sys_smart_copy ---"
	./$(TARGET) -b test_src.txt test_dest.txt
	@echo ""

	@echo "  --- [3] VERIFICANDO INTEGRIDAD (diff) ---"
	@diff test_src.txt test_dest.txt && echo "  [PASS] Los archivos son idénticos." || echo "  [FAIL] Los archivos difieren."
	@echo ""

	@echo "  --- [4] CONTENIDO DEL ARCHIVO COPIADO ---"
	@cat test_dest.txt
	@echo ""

	@echo "  --- [5] LIMPIANDO ARCHIVOS DE PRUEBA ---"
	@rm -f test_src.txt test_dest.txt test_dest.txt.stdio_copy
	@echo "  Limpieza completada."
	@echo ""

# ============================================================================
# TARGET CLEAN: eliminar todos los archivos generados
# ============================================================================

clean:
	@echo "  Limpiando archivos generados..."
	rm -f $(TARGET) $(LEGACY_BIN)
	rm -f test_src.txt test_dest.txt test_dest.txt.stdio_copy
	rm -rf bench_files src_test dest_test
	@echo "  Limpieza completada."

# ============================================================================
# TARGET HELP
# ============================================================================

help:
	@echo ""
	@echo "  Smart Backup Kernel-Space Utility — Makefile Help"
	@echo "  =================================================="
	@echo "  make all        Compila el proyecto (./backup_smart)"
	@echo "  make legacy     Compila el código base del profesor (./backup_EAFITos)"
	@echo "  make benchmark  Compila y ejecuta el benchmark completo"
	@echo "  make test       Prueba rápida de copia con verificación de integridad"
	@echo "  make clean      Elimina ejecutables y archivos temporales"
	@echo "  make help       Muestra esta ayuda"
	@echo ""

.PHONY: all legacy benchmark test clean help