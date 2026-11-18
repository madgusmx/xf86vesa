#!/bin/bash
# hacer_tgz.sh - empaqueta TU instalación de SuSE Linux 5.3 para el CD restaurador.
#   sudo bash hacer_tgz.sh DIR_MONTADO DIR_SALIDA
# DIR_MONTADO tiene las particiones montadas así (en solo lectura, desde otra Linux o una VM apagada):
#   DIR/raiz  DIR/boot  DIR/usr  DIR/opt  DIR/home      (las que no tengas separadas, déjalas vacías)
# Salen raiz/boot/usr/opt/home.tgz.00, .01, ... en pedazos de 600 MB: el iso9660 de Linux 2.0 toma los
# archivos de más de 700 MB por «defective cdrom» y recorta todos los tamaños a 24 bits.
# GNU tar en formato «gnu» y UID/GID numéricos: el tar 1.12 de 1998 los lee tal cual.
set -e
M=${1:?dir montado}; O=${2:?salida}
mkdir -p "$O"
GZ=$(command -v pigz || command -v gzip)
for p in raiz boot usr opt home; do
    [ -d "$M/$p" ] || mkdir -p "$M/$p"
    (cd "$M/$p" && tar --format=gnu --numeric-owner --one-file-system -cpf - .) | $GZ -9 | \
        split -b 600m -d -a 2 - "$O/$p.tgz."
    echo "$p listo"
done
ls -la "$O"
