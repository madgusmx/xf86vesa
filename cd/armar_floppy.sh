#!/bin/sh
# armar_floppy.sh - the same xf86vesa add-on medium as armar_cd.sh, but on a 1.44 MB floppy image (ext2 rev 0,
# readable by Linux 2.0). In SuSE 5.3: mount -t ext2 /dev/fd0 /mnt, then in YaST choose the source medium
# «Installation from a directory» = /mnt (or just: rpm -i /mnt/suse/xsrv1/xf86vesa.rpm).
#   sh cd/armar_floppy.sh xf86vesa-1.0-1.i386.rpm [out.img]
set -e
RPM=${1:?usage: armar_floppy.sh xf86vesa-1.0-1.i386.rpm [out.img]}
OUT=${2:-xf86vesa-1.0-floppy.img}
T=$(mktemp -d)
SOLO_ARBOL=$T sh "$(dirname "$0")/armar_cd.sh" "$RPM"
rm -f "$OUT"
# rev 0 = the original ext2: no dir_index, no ext_attr, 128-byte inodes; 1 KB blocks, no reserved blocks
$(command -v mke2fs || echo /sbin/mke2fs) -q -t ext2 -E revision=0,root_owner=0:0 -b 1024 -m 0 -N 64 -L XF86VESA -d "$T" "$OUT" 1440
rm -rf $T
echo "$OUT"
