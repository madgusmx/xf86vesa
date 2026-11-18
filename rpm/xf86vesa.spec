Name: xf86vesa
Version: 1.0
Release: 1
Copyright: MIT/X11
Group: X11/Servers
Summary: Servidor X con driver VESA (VBE 2.0) para XFree86 3.3.2: sin modelines ni frecuencias
Source: xf86vesa-1.0.tar.gz
Packager: retroblog <canaldelasrecetas.com>
BuildRoot: /var/tmp/xf86vesa-root

%description
XF86_VESA es el servidor XF86_SVGA de XFree86 3.3.2 con un driver nuevo, "vesa", que usa el BIOS de la
tarjeta de video (VESA VBE 2.0 o superior) para poner los modos graficos, como los juegos de DOS: no hacen
falta modelines, relojes ni frecuencias de monitor. Incluye vbeinfo (lista los modos del BIOS) y vesaconfig
(deja X con el driver VESA o lo deshace).

Para compilarlo hace falta el arbol de XFree86 3.3.2 ya compilado en /usr/src/packages/BUILD/xc
(rpm -bp xf86.spec; make World).

%prep
%setup

%build
X=/usr/src/packages/BUILD/xc
D=$X/programs/Xserver/hw/xfree86/vga256/drivers/vesa
mkdir -p $D
cp Imakefile vesa_driver.c vbe_rm.c vbe_rm.h $D/
grep -q "chips vesa generic" $X/config/cf/xfree86.cf || \
  ( cd $X/config/cf && cp xfree86.cf xfree86.cf.sin-vesa && \
    sed 's/s3v s3_svga chips generic/s3v s3_svga chips vesa generic/' xfree86.cf.sin-vesa > xfree86.cf )
( cd $X/programs/Xserver/hw/xfree86/vga256 && make Makefile && make Makefiles && rm -f vga256Conf.c && make )
( cd $X/programs/Xserver && rm -f XF86_SVGA && make XF86_SVGA )
gcc -O2 -o vbeinfo vbeinfo.c vbe_rm.c

%install
rm -rf $RPM_BUILD_ROOT
mkdir -p $RPM_BUILD_ROOT/usr/X11R6/bin $RPM_BUILD_ROOT/usr/X11R6/lib/X11 $RPM_BUILD_ROOT/usr/doc/packages/xf86vesa
install -m 4711 /usr/src/packages/BUILD/xc/programs/Xserver/XF86_SVGA $RPM_BUILD_ROOT/usr/X11R6/bin/XF86_VESA
install -m 755 vbeinfo vesaconfig $RPM_BUILD_ROOT/usr/X11R6/bin/
install -m 644 XF86Config.vesa $RPM_BUILD_ROOT/usr/X11R6/lib/X11/XF86Config.vesa
install -m 644 README xf86vesa-3.3.2.diff $RPM_BUILD_ROOT/usr/doc/packages/xf86vesa/

%post
/usr/X11R6/bin/vesaconfig --si-falta
echo "xf86vesa: para usar el driver VESA: vesaconfig   (deshacer: vesaconfig --deshacer)"

%files
%attr(4711,root,root) /usr/X11R6/bin/XF86_VESA
/usr/X11R6/bin/vbeinfo
/usr/X11R6/bin/vesaconfig
%config /usr/X11R6/lib/X11/XF86Config.vesa
%doc /usr/doc/packages/xf86vesa/README
%doc /usr/doc/packages/xf86vesa/xf86vesa-3.3.2.diff
