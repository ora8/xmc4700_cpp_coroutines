#!/bin/bash

set -e

ELF="build/xmc4700_coroutines.elf"

if [ ! -f "$ELF" ]; then
    echo "Fehler: ELF-Datei nicht gefunden!"
    exit 1
fi

JLinkExe \
    -CommanderScript flash.jlink

echo "Flash-Vorgang abgeschlossen."