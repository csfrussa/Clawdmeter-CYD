#!/bin/bash
# Compila o firmware Clawdmeter-CYD e copia o .bin para a raiz do projeto.
# Uso: ./build_cyd.sh [porta]
#   porta: ex. /dev/ttyUSB0  (Linux) ou /dev/cu.usbserial-0001 (macOS)
#          se omitida, só compila sem flashar.

set -e

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
FIRMWARE_DIR="$SCRIPT_DIR/firmware"
OUT_BIN="$SCRIPT_DIR/clawdmeter_cyd.bin"

# ---- Verificar PlatformIO ----
if ! command -v pio &>/dev/null; then
    echo "PlatformIO não encontrado. Instalando..."
    pip install platformio
fi

echo "=== Compilando firmware Clawdmeter CYD ==="
cd "$FIRMWARE_DIR"
pio run -e esp32_cyd

# Localizar o .bin gerado
BIN_SRC=$(find .pio/build/esp32_cyd -name "firmware.bin" 2>/dev/null | head -1)
if [ -z "$BIN_SRC" ]; then
    echo "Erro: firmware.bin não encontrado após build."
    exit 1
fi

cp "$BIN_SRC" "$OUT_BIN"
echo ""
echo "✓ Build completo: $OUT_BIN"
echo "  Tamanho: $(du -h "$OUT_BIN" | cut -f1)"

# ---- Flash (opcional) ----
if [ -n "$1" ]; then
    PORT="$1"
    echo ""
    echo "=== Flashando em $PORT ==="
    pio run -e esp32_cyd -t upload --upload-port "$PORT"
    echo "✓ Flash concluído."
else
    echo ""
    echo "Para flashar, rode:"
    echo "  ./build_cyd.sh /dev/ttyUSB0        # Linux"
    echo "  ./build_cyd.sh /dev/cu.usbserial-* # macOS"
fi
