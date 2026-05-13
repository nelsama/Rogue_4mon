# ============================================================================
# Makefile - ROGUE 6502 + SID 6581 - Roguelike para Monitor ROM
# ============================================================================
# Compatible con: Windows CMD, PowerShell, Git Bash, MSYS2/MinGW
#
# Requiere CC65 (https://cc65.github.io/)
#
# Uso:
#   make          - Compilar el programa
#   make clean    - Limpiar archivos generados
#   make info     - Ver tamano del binario
#   make map      - Ver mapa de memoria
#   make help     - Esta ayuda
# ============================================================================

# ============================================================
# Herramientas CC65
# ============================================================
CC65_HOME ?=

ifneq ($(CC65_HOME),)
  CC       = $(CC65_HOME)/bin/cl65
  CA65     = $(CC65_HOME)/bin/ca65
  LD65     = $(CC65_HOME)/bin/ld65
  CC65_LIB = $(CC65_HOME)/lib
else
  CC       = cl65
  CA65     = ca65
  LD65     = ld65
  CC65_LIB = /usr/share/cc65/lib
endif

# ============================================================
# Directorios
# ============================================================
SRC_DIR    = src
INC_DIR    = include
CFG_DIR    = config
BUILD_DIR  = build
OUTPUT_DIR = output

# ============================================================
# Archivos de entrada / salida
# ============================================================
PROGRAM    = $(OUTPUT_DIR)/rogue.bin
MAP_FILE   = $(OUTPUT_DIR)/rogue.map
LD_CFG     = $(CFG_DIR)/programa.cfg

C_SRCS     = $(SRC_DIR)/main.c
ASM_SRCS   = $(SRC_DIR)/startup.s

C_OBJS     = $(BUILD_DIR)/main.o
ASM_OBJS   = $(BUILD_DIR)/startup.o
OBJS       = $(ASM_OBJS) $(C_OBJS)

# ============================================================
# Flags de compilacion
# ============================================================
CFLAGS     = -t none -Oi --cpu 6502 -I $(SRC_DIR) -I $(INC_DIR)
ASFLAGS    = -t none --cpu 6502
LDFLAGS    = -C $(LD_CFG) -m $(MAP_FILE) -L $(CC65_LIB)

# ============================================================
# REGLAS PRINCIPALES
# ============================================================
.PHONY: all dirs clean info map help

all: dirs $(PROGRAM)
	@echo ========================================
	@echo Programa generado: $(PROGRAM)
	@echo ========================================
	@echo   1. Copiar a SD como rogue.bin
	@echo   2. En el monitor:
	@echo      LOAD rogue.bin 0800
	@echo      R 0800
	@echo ========================================

# ── Crear directorios de salida (funciona en CMD y sh) ──
dirs:
	@mkdir $(BUILD_DIR) 2>nul || cd .
	@mkdir $(OUTPUT_DIR) 2>nul || cd .

# ── Compilar C ──────────────────────────────────────────
$(BUILD_DIR)/main.o: $(C_SRCS)
	$(CC) -c $(CFLAGS) -o $@ $<

# ── Ensamblar ───────────────────────────────────────────
$(BUILD_DIR)/startup.o: $(ASM_SRCS)
	$(CA65) $(ASFLAGS) -o $@ $<

# ── Linkar ──────────────────────────────────────────────
$(PROGRAM): $(OBJS)
	$(LD65) $(LDFLAGS) -o $@ $(OBJS) none.lib

# ============================================================
# UTILIDADES
# ============================================================

# ── Tamano del binario ──────────────────────────────────
info:
	@echo ========================================
	@echo Informacion del programa
	@echo ========================================
	@echo Tamano: $(shell wc -c < $(PROGRAM)) bytes

# ── Mapa de memoria ────────────────────────────────────
map:
	@cat $(MAP_FILE) 2>nul || echo Error: Mapa no encontrado. Compilar primero.

# ── Limpieza (híbrida: CMD + Git Bash) ───────────────────────
# Detecta el shell: si 'sh' existe, usa rm; si no, usa rmdir
SHELL_TYPE := $(shell sh -c "echo unix" 2>NUL)
ifeq ($(SHELL_TYPE),unix)
CLEAN_CMD = -rm -rf $(BUILD_DIR) $(OUTPUT_DIR)
else
CLEAN_CMD = -if exist $(BUILD_DIR) rmdir /s /q $(BUILD_DIR) 2>nul & if exist $(OUTPUT_DIR) rmdir /s /q $(OUTPUT_DIR) 2>nul
endif

clean:
	$(CLEAN_CMD)
	@echo Limpieza completa.

# ── Ayuda ───────────────────────────────────────────────
help:
	@echo ========================================
	@echo ROGUE 6502 + SID 6581 - Roguelike
	@echo ========================================
	@echo Targets:
	@echo   make        - Compilar el programa
	@echo   make clean  - Limpiar archivos generados
	@echo   make info   - Ver tamano del binario
	@echo   make map    - Ver mapa de memoria
	@echo   make help   - Esta ayuda
	@echo ========================================
	@echo Variables:
	@echo   CC65_HOME   - Ruta de instalacion de CC65
	@echo ========================================
