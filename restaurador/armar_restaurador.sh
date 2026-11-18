#!/bin/bash
# armar_restaurador.sh - CD restaurador de SuSE Linux 5.3 (retroblog, SOLO LABORATORIO: lleva SuSE completo).
#   sudo bash armar_restaurador.sh RAIZ_MONTADA ISO_SUSE53 DIR_TGZ SALIDA.iso
# RAIZ_MONTADA: la partición / de la imagen limpia (de ahí salen las herramientas del initrd)
# ISO_SUSE53:   el ISO de SuSE 5.3 con el arrancador de Compunauta (de ahí salen isolinux.bin y el kernel linux53)
# DIR_TGZ:      carpeta con raiz/boot/usr/opt/home.tgz.NN (tar --format=gnu --numeric-owner | gzip | split -b 600m: el iso9660 de Linux 2.0 no acepta archivos de más de 700 MB)
set -e
R=${1:?raiz}; ISO=${2:?iso}; TGZ=${3:?tgz}; OUT=${4:?salida}
AQUI=$(cd "$(dirname "$0")" && pwd)
W=$(mktemp -d); T=$(mktemp -d)

# --- initrd: herramientas del propio SuSE 5.3 (libc5) ---
mkdir -p $T/{bin,sbin,lib,dev,proc,mnt,cdrom,tmp,etc}
for b in bash mount umount ls cat mkdir ln cp rm sync sleep gzip tar dd stty; do cp -a $R/bin/$b $T/bin/; done
ln -s bash $T/bin/sh
for b in sfdisk fdisk mke2fs mkswap lilo halt ldconfig hdparm; do cp -a $R/sbin/$b $T/sbin/; done
ln -s halt $T/sbin/reboot
for l in ld-linux.so.1 ld-linux.so.1.9.9 libc.so.5 libc.so.5.4.46 libncurses.so.3.0 libncurses.so.3.0.980228 \
         libdl.so.1 libdl.so.1.9.9; do cp -a $R/lib/$l $T/lib/; done
: > $T/etc/mtab
echo "root::0:0:root:/:/bin/sh" > $T/etc/passwd
install -m 755 $AQUI/linuxrc $T/linuxrc
ln -s ../linuxrc $T/sbin/init
# dispositivos (números de Linux 2.0)
cd $T/dev
mknod console c 5 1; mknod tty c 5 0; mknod null c 1 3; mknod zero c 1 5
for i in 1 2 3 4; do mknod tty$i c 4 $i; done
mknod ram0 b 1 0; mknod ram b 1 1
disco() { mknod $1 b $2 $3; for p in 1 2 3 4 5 6 7 8; do mknod $1$p b $2 $(($3 + p)); done; }
disco hda 3 0; disco hdb 3 64; disco hdc 22 0; disco hdd 22 64
disco hde 33 0; disco hdf 33 64; disco hdg 34 0; disco hdh 34 64
cd - > /dev/null
chown -R 0:0 $T
mkdir -p $W/boot/loader $W/restaura
# ext2 revisión 0 (Linux 2.0), bloques de 1 KB como el ramdisk
/sbin/mke2fs -q -t ext2 -E revision=0,root_owner=0:0 -b 1024 -m 0 -L restaura -d $T $W/initrd.img 8192
gzip -9 -c $W/initrd.img > $W/boot/loader/initrd.gz; rm $W/initrd.img

# --- arranque: isolinux (el ISOLINUX 1.62 del CD de Compunauta busca todo en /boot/loader) y el kernel 2.0.35 de instalación de SuSE (IDE/ATAPI incluidos) ---
isoinfo -R -i "$ISO" -x /boot/loader/isolinux.bin > $W/boot/loader/isolinux.bin
isoinfo -R -i "$ISO" -x /boot/loader/linux53 > $W/boot/loader/vmlinuz
cat > $W/boot/loader/isolinux.cfg <<'EOF'
DEFAULT restaura
PROMPT 1
TIMEOUT 150
DISPLAY message
LABEL restaura
  KERNEL vmlinuz
  APPEND initrd=initrd.gz ramdisk_size=8192 root=/dev/ram0 rw vga=normal
EOF
cat > $W/boot/loader/message <<'EOF'

  Restaurador de S.u.S.E. Linux 5.3 (retroblog, laboratorio)
  KDE 1.0 + driver VESA de XFree86 3.3.2 ya configurado

  restaura (Enter): copia el sistema al primer disco IDE.
                    BORRA ese disco (antes pide escribir BORRAR).

  PCs con SATA: en el BIOS, SATA = IDE / Legacy / Compatible.
  (Linux 2.0 no conoce SATA ni AHCI.)

EOF

# --- el sistema ---
cp "$TGZ"/*.tgz.* $W/restaura/
echo "SuSE Linux 5.3 retroblog restaurador 1.0 ($(date +%F))" > $W/restaura/RESTAURA.ID
cat > $W/restaura/LEEME.TXT <<'EOF'
CD restaurador de SuSE Linux 5.3 (retroblog). SOLO PARA EL LABORATORIO: contiene SuSE Linux 5.3 completo.
Arranca, busca el CD, elige el primer disco IDE (>= 4.6 GB), pide escribir BORRAR, lo particiona
(1 /boot 200 MB, 2 swap 128 MB, 3 / el resto), extrae los tgz (con la base RPM), escribe fstab y lilo.conf,
instala LILO en el MBR y reinicia. Usuario retro sin contraseña; root: cambiame (expirada).
Para restaurar a mano desde otra distro: cat raiz.tgz.* | gzip -dc | tar -xpf - -C /mnt (y boot, usr, opt, home).
EOF
cp $AQUI/linuxrc $W/restaura/linuxrc.txt

genisoimage -quiet -r -J -l -V SUSE53_RESTAURA -A "Restaurador SuSE Linux 5.3 retroblog" \
    -b boot/loader/isolinux.bin -c boot/loader/isolinux.cat -no-emul-boot -boot-load-size 4 -boot-info-table \
    -o "$OUT" $W
rm -rf $W $T
ls -la "$OUT"
