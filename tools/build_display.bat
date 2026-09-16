@echo off
set IDF_TOOLS_PATH=C:\Espressif\tools
set IDF_COMPONENT_LOCAL_STORAGE_URL=file://C:\Espressif\tools
set IDF_PATH=C:\esp\v5.5.5\esp-idf
set ESP_ROM_ELF_DIR=C:\Espressif\tools\esp-rom-elfs\20241011/
set OPENOCD_SCRIPTS=C:\Espressif\tools\openocd-esp32\v0.12.0-esp32-20260424/openocd-esp32/share/openocd/scripts
set IDF_PYTHON_ENV_PATH=C:\Espressif\tools\python\v5.5.5\venv
set IDF_CCACHE_ENABLE=1
set ESP_IDF_VERSION=5.5
set PATH=C:\Espressif\tools\ccache\4.12.1\ccache-4.12.1-windows-x86_64;C:\Espressif\tools\cmake\3.30.2\bin;C:\Espressif\tools\ninja\1.12.1;C:\Espressif\tools\riscv32-esp-elf\esp-14.2.0_20260121\riscv32-esp-elf\bin;C:\Espressif\tools\python\v5.5.5\venv\Scripts;%PATH%
cd /d C:\projects\esp32-odb2-car-display\display_board
C:\Espressif\tools\python\v5.5.5\venv\Scripts\python.exe C:\esp\v5.5.5\esp-idf\tools\idf.py -B build %*
