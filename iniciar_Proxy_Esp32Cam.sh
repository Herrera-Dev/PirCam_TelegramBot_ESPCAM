#!/bin/bash
#
# Esto inicia el script de python 'Proxy_Video_ESP32_Celdas.py' en su entorno virtual (venv)
VENV_PATH="/home/juancarlos/Descargas/DIY/ESP32/PirCam_TelegramBot_ESPCAM/venv"
PYTHON_SCRIPT="/home/juancarlos/Descargas/DIY/ESP32/PirCam_TelegramBot_ESPCAM/Proxy_Video_ESP32_Celdas.py"
#
echo "Entorno Virtual: $VENV_PATH/bin/python3"
echo "Script: $PYTHON_SCRIPT"
#
$VENV_PATH/bin/python3 $PYTHON_SCRIPT
