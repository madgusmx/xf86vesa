/*
 * vbe_rm.h - llamadas al BIOS de video en modo real (int 10h) desde Linux 2.0 con vm86().
 *
 * Copyright (c) 2026 retroblog (canaldelasrecetas.com).
 * Licencia: la misma de XFree86 (estilo MIT/X11). Ver vesa_driver.c.
 */
#ifndef VBE_RM_H
#define VBE_RM_H

typedef struct {
    unsigned long eax, ebx, ecx, edx, esi, edi, ebp;
    unsigned short es, ds;
} VBERegs;

/* Segmento de trabajo en la memoria baja privada (lineal 0x10000): stub, buffers y pila. */
#define VBE_SEG        0x1000
#define VBE_OFF_STUB   0x0000      /* int 10h ; hlt */
#define VBE_OFF_INFO   0x0100      /* VbeInfoBlock (512 bytes) */
#define VBE_OFF_MODE   0x0400      /* ModeInfoBlock (256 bytes) */
#define VBE_STACK_TOP  0xF000

#define VBE_LIN(seg, off)   ((unsigned char *)(((unsigned long)(seg) << 4) + (unsigned long)(off)))
#define VBE_FAR(p)          VBE_LIN((p) >> 16, (p) & 0xFFFF)   /* puntero lejano seg:off guardado como dword */

int  VBErmInit(void);                       /* mapea el primer MB; 0 = bien */
int  VBErmInt10(VBERegs *r);                /* 0 = bien, -1 = error de vm86 */
extern char VBErmError[160];
extern int  VBErmVerbose;

/* Funciones de E/S que el módulo usa para emular IN/OUT (las da quien lo enlaza). */
unsigned int  vberm_inb(unsigned short port);
unsigned int  vberm_inw(unsigned short port);
unsigned int  vberm_inl(unsigned short port);
void vberm_outb(unsigned short port, unsigned int v);
void vberm_outw(unsigned short port, unsigned int v);
void vberm_outl(unsigned short port, unsigned int v);

#endif
