/*
 * vbe_rm.c - int 10h en modo real desde Linux 2.0 (syscall vm86, la 113 «vieja»).
 *
 * Como LRMI (la Linux Real Mode Interface de 1998), pero mínima y propia:
 *   - página 0 (vectores de interrupción + datos del BIOS) mapeada de /dev/mem en privado (copy-on-write);
 *   - 0x1000-0xA0000 de /dev/zero (memoria baja privada), con una copia del EBDA del BIOS;
 *   - 0xA0000-0x100000 (memoria de video y ROMs) de /dev/mem compartida.
 * El código del BIOS corre en modo virtual 8086; las instrucciones IN/OUT que el kernel no deja pasar vuelven
 * como VM86_UNKNOWN y se emulan aquí. La llamada termina en el «hlt» que sigue al «int 10h» del stub.
 *
 * Copyright (c) 2026 retroblog (canaldelasrecetas.com). Licencia estilo MIT/X11, ver vesa_driver.c.
 */
#include <stdio.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/mman.h>
#include <asm/vm86.h>
#include "vbe_rm.h"

char VBErmError[160];
int  VBErmVerbose = 0;
static int listo = 0;

#define LIN(seg, off)  ((unsigned char *)((((unsigned long)(seg)) << 4) + ((unsigned long)(off) & 0xFFFF)))

static int
vm86old_llamada(struct vm86_struct *v)
{
    int r;
    __asm__ __volatile__("pushl %%ebx\n\tmovl %2,%%ebx\n\tint $0x80\n\tpopl %%ebx"
                         : "=a" (r) : "0" (113), "r" (v) : "memory");
    return r;
}

int
VBErmInit(void)
{
    int fdm, fdz;
    caddr_t m;
    unsigned long ebda, tam;

    if (listo)
        return 0;
    fdm = open("/dev/mem", O_RDWR);
    fdz = open("/dev/zero", O_RDWR);
    if (fdm < 0 || fdz < 0) {
        sprintf(VBErmError, "no se pudo abrir /dev/mem o /dev/zero");
        return -1;
    }
    /* página 0: IVT + BDA, privada */
    m = mmap((caddr_t)0, 0x1000, PROT_READ | PROT_WRITE | PROT_EXEC, MAP_FIXED | MAP_PRIVATE, fdm, 0);
    if (m != (caddr_t)0) {
        sprintf(VBErmError, "mmap de la página 0 falló");
        return -1;
    }
    /* memoria baja privada en ceros */
    m = mmap((caddr_t)0x1000, 0xA0000 - 0x1000, PROT_READ | PROT_WRITE | PROT_EXEC,
             MAP_FIXED | MAP_PRIVATE, fdz, 0);
    if (m != (caddr_t)0x1000) {
        sprintf(VBErmError, "mmap de la memoria baja falló");
        return -1;
    }
    /* video + ROMs, compartido */
    m = mmap((caddr_t)0xA0000, 0x60000, PROT_READ | PROT_WRITE | PROT_EXEC, MAP_FIXED | MAP_SHARED,
             fdm, 0xA0000);
    if (m != (caddr_t)0xA0000) {
        sprintf(VBErmError, "mmap de 0xA0000-0xFFFFF falló");
        return -1;
    }
    /* copia del EBDA (segmento en 0x40:0x0E), si es razonable */
    ebda = (unsigned long)(*(unsigned short *)0x40E) << 4;
    if (ebda >= 0x80000 && ebda < 0xA0000) {
        unsigned long base = ebda & ~0xFFFUL;
        tam = 0xA0000 - base;
        m = mmap((caddr_t)0, tam, PROT_READ, MAP_SHARED, fdm, base);
        if (m != (caddr_t)-1) {
            memcpy((void *)base, (void *)m, tam);
            munmap(m, tam);
        }
    }
    close(fdz);
    close(fdm);
    /* stub: int 10h ; hlt */
    {
        unsigned char *s = LIN(VBE_SEG, VBE_OFF_STUB);
        s[0] = 0xCD; s[1] = 0x10; s[2] = 0xF4;
    }
    listo = 1;
    return 0;
}

static void
empujar16(struct vm86_regs *r, unsigned short v)
{
    r->esp = (r->esp - 2) & 0xFFFF;
    *(unsigned short *)LIN(r->ss, r->esp) = v;
}

static void
emular_int(struct vm86_regs *r, int n)
{
    unsigned short *ivt = (unsigned short *)0;
    empujar16(r, (unsigned short)r->eflags);
    empujar16(r, r->cs);
    empujar16(r, (unsigned short)r->eip);
    r->eflags &= ~(IF_MASK | TF_MASK);
    r->cs = ivt[n * 2 + 1];
    r->eip = ivt[n * 2];
}

/* Emula la instrucción de E/S en cs:ip. 1 = emulada, 0 = no era E/S. */
static int
emular_io(struct vm86_regs *r)
{
    unsigned char *p = LIN(r->cs, r->eip), *ini = p;
    int op32 = 0, rep = 0, seg = -1, cuenta, paso;
    unsigned short port;

    for (;;) {
        if (*p == 0x66) { op32 = 1; p++; }
        else if (*p == 0x67) { p++; }
        else if (*p == 0xF3 || *p == 0xF2) { rep = 1; p++; }
        else if (*p == 0x26) { seg = r->es; p++; }
        else if (*p == 0x2E) { seg = r->cs; p++; }
        else if (*p == 0x36) { seg = r->ss; p++; }
        else if (*p == 0x3E) { seg = r->ds; p++; }
        else break;
    }
    switch (*p) {
    case 0xE4: port = p[1]; r->eax = (r->eax & ~0xFFUL) | (vberm_inb(port) & 0xFF); p += 2; break;
    case 0xE5: port = p[1];
        if (op32) r->eax = vberm_inl(port);
        else r->eax = (r->eax & ~0xFFFFUL) | (vberm_inw(port) & 0xFFFF);
        p += 2; break;
    case 0xE6: vberm_outb(p[1], r->eax & 0xFF); p += 2; break;
    case 0xE7: if (op32) vberm_outl(p[1], r->eax); else vberm_outw(p[1], r->eax & 0xFFFF); p += 2; break;
    case 0xEC: r->eax = (r->eax & ~0xFFUL) | (vberm_inb(r->edx & 0xFFFF) & 0xFF); p++; break;
    case 0xED:
        if (op32) r->eax = vberm_inl(r->edx & 0xFFFF);
        else r->eax = (r->eax & ~0xFFFFUL) | (vberm_inw(r->edx & 0xFFFF) & 0xFFFF);
        p++; break;
    case 0xEE: vberm_outb(r->edx & 0xFFFF, r->eax & 0xFF); p++; break;
    case 0xEF: if (op32) vberm_outl(r->edx & 0xFFFF, r->eax); else vberm_outw(r->edx & 0xFFFF, r->eax & 0xFFFF);
        p++; break;
    case 0x6C: case 0x6D: case 0x6E: case 0x6F:
        paso = (*p == 0x6C || *p == 0x6E) ? 1 : (op32 ? 4 : 2);
        if (r->eflags & 0x400) paso = -paso;           /* DF */
        cuenta = rep ? (int)(r->ecx & 0xFFFF) : 1;
        port = r->edx & 0xFFFF;
        while (cuenta-- > 0) {
            if (*p == 0x6C || *p == 0x6D) {             /* INS: es:di */
                unsigned char *d = LIN(r->es, r->edi);
                if (*p == 0x6C) *d = vberm_inb(port);
                else if (op32) *(unsigned long *)d = vberm_inl(port);
                else *(unsigned short *)d = vberm_inw(port);
                r->edi = (r->edi & ~0xFFFFUL) | ((r->edi + paso) & 0xFFFF);
            } else {                                      /* OUTS: ds:si (o segmento forzado) */
                unsigned char *s = LIN(seg >= 0 ? seg : r->ds, r->esi);
                if (*p == 0x6E) vberm_outb(port, *s);
                else if (op32) vberm_outl(port, *(unsigned long *)s);
                else vberm_outw(port, *(unsigned short *)s);
                r->esi = (r->esi & ~0xFFFFUL) | ((r->esi + paso) & 0xFFFF);
            }
            if (rep) r->ecx = (r->ecx & ~0xFFFFUL) | ((r->ecx - 1) & 0xFFFF);
        }
        p++; break;
    default:
        return 0;
    }
    r->eip = (r->eip + (p - ini)) & 0xFFFF;
    return 1;
}

int
VBErmInt10(VBERegs *e)
{
    struct vm86_struct v;
    struct vm86_regs *r = &v.regs;
    int ret, vueltas = 0;

    if (VBErmInit())
        return -1;
    memset(&v, 0, sizeof(v));
    r->eax = e->eax; r->ebx = e->ebx; r->ecx = e->ecx; r->edx = e->edx;
    r->esi = e->esi; r->edi = e->edi; r->ebp = e->ebp;
    r->es = e->es; r->ds = e->ds; r->fs = 0; r->gs = 0;
    r->cs = VBE_SEG; r->eip = VBE_OFF_STUB;
    r->ss = VBE_SEG; r->esp = VBE_STACK_TOP;
    r->eflags = IF_MASK | 0x2;
    v.cpu_type = CPU_586;

    for (;;) {
        if (++vueltas > 5000000) {
            sprintf(VBErmError, "el BIOS no volvió (cs:ip %04x:%04lx)", r->cs, r->eip);
            return -1;
        }
        ret = vm86old_llamada(&v);
        if (ret < 0) {
            sprintf(VBErmError, "vm86 devolvió el error %d", -ret);
            return -1;
        }
        switch (VM86_TYPE(ret)) {
        case VM86_SIGNAL:
        case VM86_STI:
        case VM86_PICRETURN:
            continue;
        case VM86_INTx:
            emular_int(r, VM86_ARG(ret));
            continue;
        case VM86_UNKNOWN: {
            unsigned char *p = LIN(r->cs, r->eip);
            if (*p == 0xF4 && r->cs == VBE_SEG && r->eip == VBE_OFF_STUB + 2)
                goto fin;                                /* el hlt del stub: terminó */
            if (emular_io(r))
                continue;
            sprintf(VBErmError, "instrucción desconocida %02x %02x %02x en %04x:%04lx",
                    p[0], p[1], p[2], r->cs, r->eip);
            return -1;
        }
        default:
            sprintf(VBErmError, "vm86: retorno inesperado %d", ret);
            return -1;
        }
    }
fin:
    e->eax = r->eax; e->ebx = r->ebx; e->ecx = r->ecx; e->edx = r->edx;
    e->esi = r->esi; e->edi = r->edi; e->ebp = r->ebp;
    e->es = r->es; e->ds = r->ds;
    return 0;
}
