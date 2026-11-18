#!/bin/sh
# armar_cd.sh - builds the xf86vesa add-on CD for YaST 0.97 (SuSE Linux 5.3): an ISO that YaST accepts as its
# installation medium and that only lists xf86vesa (series xsrv). No SuSE files inside: the catalog is ours.
#   sh cd/armar_cd.sh xf86vesa-1.0-1.i386.rpm [out.iso]
# Needs genisoimage (or mkisofs). YaST checks three things on the medium:
#   suse/setup/descr/{common,english,german,french}.pkd   package descriptions (here: only xf86vesa)
#   suse/setup/descr/*.ser                                series descriptions (here: only xsrv)
#   /.S.u.S.E-disk-001                                    disc marker; without it YaST says «Wrong CD»
set -e
RPM=${1:?usage: armar_cd.sh xf86vesa-1.0-1.i386.rpm [out.iso]}
OUT=${2:-xf86vesa-1.0-cd.iso}
IDENT='S.u.S.E.-Linux-FTP-DE-i386-5.3.0#0'      # the DIST_IDENT of SuSE 5.3 (FTP edition)
T=$(mktemp -d)
D=$T/suse/setup/descr
mkdir -p $D $T/suse/setup/du $T/suse/xsrv1
cp "$RPM" $T/suse/xsrv1/xf86vesa.rpm
RSIZE=$(wc -c < "$RPM")

printf '%s\n1\n' "$IDENT" > $T/.S.u.S.E-disk-001
cat > $D/info <<EOF
ELF
MIN_YAST_VERSION 0.90
DIST_STRING      S.u.S.E.
DIST_IDENT       $IDENT
MIN_DIST_VERSION 5.3
HAS_LIVE_CD=0
EOF

cat > $D/common.ser <<'EOF'
##--------------------------------------------------------------------
## xf86vesa add-on CD - series
##--------------------------------------------------------------------
Series: xsrv
Status:
Version:
##--------------------------------------------------------------------
EOF
for L in english german french; do
cat > $D/$L.ser <<'EOF'
##--------------------------------------------------------------------
## xf86vesa add-on CD - series
##--------------------------------------------------------------------
Series: xsrv
Label: X servers (xf86vesa add-on CD)
Notify:
Yfiton:
Description:
Noitpircsed:
##--------------------------------------------------------------------
EOF
done

cat > $D/common.pkd <<EOF
##--------------------------------------------------------------------
## xf86vesa add-on CD - packages
##--------------------------------------------------------------------
##
## -----> xf86vesa <-----
##
Filename:      xf86vesa.rpm
Series:        xsrv
InstPath:      01 /xsrv1/
Size:          3280858 $RSIZE
Buildtime:     907648800
DepAND:        xf86  shlibs  ldso  libz
DepOR:
DepExcl:
Flag:          A(xf86vesa VESA ) ELF
Category:
Copyright:     MIT/X11 (c) 2026 retroblog
AuthorName:    retroblog
AuthorEmail:
AuthorAddress: canaldelasrecetas.com
Version:       1.0-1
StartCommand:
EOF

desc() {    # $1 = file, $2 = label, $3 = text
cat > $D/$1 <<EOF
##--------------------------------------------------------------------
## xf86vesa add-on CD - package descriptions
##--------------------------------------------------------------------
##
## -----> xf86vesa <-----
##
Filename:      xf86vesa.rpm
Label:         $2
Notify:
Yfiton:
Delnotify:
Yfitonled:
Description:
$3

Contains: xf86vesa-1.0
Noitpircsed:
EOF
}
EN="X server with a VESA (VBE 2.0) driver for any video card whose BIOS supports VBE 2.0: no modelines, no clocks, no monitor frequencies. The driver asks the video BIOS for its modes and sets them with the same BIOS, like DOS games. Run 'vesaconfig' to use it and 'vbeinfo' to list the modes of your card. https://github.com/madgusmx/xf86vesa"
desc english.pkd "VESA X server (VBE 2.0, no modelines)" "$EN"
desc german.pkd "VESA X-Server (VBE 2.0, ohne Modelines)" "X-Server mit VESA-Treiber (VBE 2.0) fuer jede Grafikkarte mit VBE-2.0-BIOS: keine Modelines, keine Taktfrequenzen, keine Monitorfrequenzen. Benutzen mit 'vesaconfig', Modi anzeigen mit 'vbeinfo'. https://github.com/madgusmx/xf86vesa"
desc french.pkd "Serveur X VESA (VBE 2.0, sans modelines)" "$EN"

cat > $T/suse/setup/du/xf86vesa.fil <<'EOF'
##
## xf86vesa add-on CD - files
##
Files:
usr/X11R6/bin/XF86_VESA 3163
usr/X11R6/bin/vbeinfo 9
usr/X11R6/bin/vesaconfig 2
usr/X11R6/lib/X11/XF86Config.vesa 2
usr/doc/packages/xf86vesa/README 2
usr/doc/packages/xf86vesa/xf86vesa-3.3.2.diff 22
Selif:
EOF

cat > $T/README.TXT <<'EOF'
xf86vesa 1.0 - add-on CD for SuSE Linux 5.3 (YaST 0.97)

1. Put this CD in the drive (or attach the ISO to the virtual machine).
2. As root: yast -> Choose/Install packages -> Change/create configuration -> series xsrv -> mark xf86vesa
   -> F10 -> Start installation.  (Or simply: mount the CD and rpm -i suse/xsrv1/xf86vesa.rpm)
3. Run vesaconfig and startx.  To go back: vesaconfig --deshacer.

YaST only sees xf86vesa on this CD; the rest of SuSE stays as installed.
https://github.com/madgusmx/xf86vesa  -  https://www.canaldelasrecetas.com/cms/retro-suse53-driver-vesa-en/
EOF

if [ -n "$SOLO_ARBOL" ]; then                   # used by armar_floppy.sh: just leave the tree there
    cp -a $T/. "$SOLO_ARBOL"/; rm -rf $T; exit 0
fi
MK=$(command -v genisoimage || command -v mkisofs)
$MK -quiet -r -J -V XF86VESA -A "xf86vesa 1.0 add-on CD for SuSE Linux 5.3" -o "$OUT" $T
rm -rf $T
echo "$OUT"
