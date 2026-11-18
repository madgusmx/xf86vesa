/*
 * vbeinfo - lista los modos VESA (VBE) del BIOS de video, desde Linux 2.0, con vm86().
 *   vbeinfo            información del BIOS y la lista de modos
 *   vbeinfo -s MODO    pone el modo (hex, p. ej. 117) 3 segundos y vuelve a texto (prueba)
 * Hay que correrlo como root (iopl, /dev/mem). Copyright (c) 2026 retroblog. Licencia estilo MIT/X11.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
extern int iopl(int);
#include "vbe_rm.h"

unsigned int vberm_inb(unsigned short p) { unsigned char v; __asm__ __volatile__("inb %w1,%0" : "=a"(v) : "Nd"(p)); return v; }
unsigned int vberm_inw(unsigned short p) { unsigned short v; __asm__ __volatile__("inw %w1,%0" : "=a"(v) : "Nd"(p)); return v; }
unsigned int vberm_inl(unsigned short p) { unsigned int v; __asm__ __volatile__("inl %w1,%0" : "=a"(v) : "Nd"(p)); return v; }
void vberm_outb(unsigned short p, unsigned int v) { __asm__ __volatile__("outb %b0,%w1" : : "a"(v), "Nd"(p)); }
void vberm_outw(unsigned short p, unsigned int v) { __asm__ __volatile__("outw %w0,%w1" : : "a"(v), "Nd"(p)); }
void vberm_outl(unsigned short p, unsigned int v) { __asm__ __volatile__("outl %0,%w1" : : "a"(v), "Nd"(p)); }

static const char *
cadena(unsigned long farptr)
{
    static char b[80];
    unsigned char *s = VBE_FAR(farptr);
    int i;
    if (!farptr) return "";
    for (i = 0; i < 79 && s[i]; i++) b[i] = s[i];
    b[i] = 0;
    return b;
}

int
main(int argc, char **argv)
{
    VBERegs r;
    unsigned char *info = VBE_LIN(VBE_SEG, VBE_OFF_INFO), *mi = VBE_LIN(VBE_SEG, VBE_OFF_MODE);
    unsigned short *lista;
    unsigned short modos[256];
    int n = 0, i;

    if (iopl(3)) { perror("iopl"); return 1; }
    if (VBErmInit()) { fprintf(stderr, "vbeinfo: %s\n", VBErmError); return 1; }

    if (argc > 2 && !strcmp(argv[1], "-s")) {
        int m = strtol(argv[2], NULL, 16);
        memset(&r, 0, sizeof r); r.eax = 0x4F02; r.ebx = m | 0x4000;
        if (VBErmInt10(&r)) { fprintf(stderr, "%s\n", VBErmError); return 1; }
        i = r.eax & 0xFFFF;
        sleep(argc > 3 ? atoi(argv[3]) : 3);
        memset(&r, 0, sizeof r); r.eax = 0x0003; VBErmInt10(&r);
        printf("4F02 modo %x: ax=%04x\n", m, i);
        return 0;
    }

    memcpy(info, "VBE2", 4);
    memset(&r, 0, sizeof r);
    r.eax = 0x4F00; r.es = VBE_SEG; r.edi = VBE_OFF_INFO;
    if (VBErmInt10(&r)) { fprintf(stderr, "vbeinfo: %s\n", VBErmError); return 1; }
    if ((r.eax & 0xFFFF) != 0x004F || memcmp(info, "VESA", 4)) {
        printf("No hay VESA (ax=%04lx)\n", r.eax & 0xFFFF);
        return 1;
    }
    printf("VBE %d.%d  memoria %d KB  OEM: %s\n", info[5], info[4], *(unsigned short *)(info + 18) * 64,
           cadena(*(unsigned long *)(info + 6)));
    if (info[5] >= 2)
        printf("Fabricante: %s  Producto: %s  Revision: %s\n", cadena(*(unsigned long *)(info + 22)),
               cadena(*(unsigned long *)(info + 26)), cadena(*(unsigned long *)(info + 30)));
    lista = (unsigned short *)VBE_FAR(*(unsigned long *)(info + 14));
    while (n < 256 && lista[n] != 0xFFFF) { modos[n] = lista[n]; n++; }   /* copiar antes de pisar el buffer */

    printf("modo  resolucion   bpp  modelo  lineal      bytes/linea\n");
    for (i = 0; i < n; i++) {
        memset(&r, 0, sizeof r);
        r.eax = 0x4F01; r.ecx = modos[i]; r.es = VBE_SEG; r.edi = VBE_OFF_MODE;
        if (VBErmInt10(&r) || (r.eax & 0xFFFF) != 0x004F) continue;
        if (!(mi[0] & 0x10)) continue;                       /* solo gráficos */
        printf("%4x  %4dx%-4d    %2d    %d     %08lx%s  %d\n", modos[i], *(unsigned short *)(mi + 18),
               *(unsigned short *)(mi + 20), mi[25], mi[27], *(unsigned long *)(mi + 40),
               (mi[0] & 0x80) ? " L" : "  ", *(unsigned short *)(mi + 16));
    }
    return 0;
}
