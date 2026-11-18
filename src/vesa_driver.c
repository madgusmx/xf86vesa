/*
 * vesa_driver.c - driver VESA (VBE 2.0+) para el servidor XF86_SVGA de XFree86 3.3.2.
 *
 * «No quisquilloso»: no usa modelines, relojes ni frecuencias de monitor. Le pregunta al BIOS de video qué modos
 * tiene (VBE 4F00/4F01), los ofrece como modos propios del driver (ChipBuiltinModes) y los pone con el mismo BIOS
 * (4F02, framebuffer lineal), como hacían los juegos de DOS. El BIOS se llama en modo real con vm86() (vbe_rm.c).
 *
 * XF86Config mínimo:
 *     Section "Device"   Identifier "VESA"  Chipset "vesa"  EndSection
 *     Section "Screen"   Driver "svga"  Device "VESA"  Monitor "Cualquiera"  DefaultColorDepth 16
 *         SubSection "Display"  Depth 16  Modes "1024x768" "800x600"  EndSubSection   (Modes es opcional)
 *     EndSection
 * Sin «Modes», ofrece todos los modos del BIOS hasta 1280x1024 y arranca en el mas grande.
 *
 * Copyright (c) 2026 retroblog (canaldelasrecetas.com)
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy of this software and associated
 * documentation files (the "Software"), to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense, and/or sell copies of the Software, and
 * to permit persons to whom the Software is furnished to do so, subject to the following conditions:
 * The above copyright notice and this permission notice shall be included in all copies or substantial portions
 * of the Software.
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO
 * THE WARRANTIES OF MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE,
 * ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
 */

#include "X.h"
#include "input.h"
#include "screenint.h"

#include "compiler.h"
#include "xf86.h"
#include "xf86Priv.h"
#include "xf86_OSlib.h"
#include "xf86_HWlib.h"
#define XCONFIG_FLAGS_ONLY
#include "xf86_Config.h"
#include "vga.h"
#include "vbe_rm.h"

typedef struct {
    vgaHWRec std;               /* lo de siempre del VGA (para volver a texto con las fuentes) */
    int vbeModo;                /* modo VBE de este estado */
    int grafico;                /* TRUE: se pone con 4F02; FALSE: modo de texto + vgaHWRestore */
} vgaVESARec, *vgaVESAPtr;

static Bool  VESAProbe();
static char *VESAIdent();
static void  VESAEnterLeave();
static Bool  VESAInit();
static int   VESAValidMode();
static void *VESASave();
static void  VESARestore();
static void  VESAAdjust();
static int   VESAPitch();

vgaVideoChipRec VESA = {
    VESAProbe,
    VESAIdent,
    VESAEnterLeave,
    VESAInit,
    VESAValidMode,
    VESASave,
    VESARestore,
    VESAAdjust,
    vgaHWSaveScreen,
    (void (*)())NoopDDA,        /* GetMode */
    (void (*)())NoopDDA,        /* FbInit */
    (void (*)())NoopDDA,        /* SetRead: sin bancos, todo lineal */
    (void (*)())NoopDDA,
    (void (*)())NoopDDA,
    0x10000,
    0x10000,
    16,
    0xFFFF,
    0x00000, 0x10000,
    0x00000, 0x10000,
    FALSE,
    VGA_NO_DIVIDE_VERT,
    {0,},
    8,
    TRUE,                       /* ChipUseLinearAddressing */
    0,                          /* ChipLinearBase (lo pone el Probe) */
    0,                          /* ChipLinearSize */
    TRUE,                       /* 16 bpp */
    TRUE,                       /* 24 bpp */
    TRUE,                       /* 32 bpp */
    NULL,                       /* ChipBuiltinModes (los arma el Probe con lo que dice el BIOS) */
    1,
    1
};

#define new ((vgaVESAPtr)vgaNewVideoState)
#define MAXMODOS 64

static DisplayModeRec modos[MAXMODOS];
static int numeroVBE[MAXMODOS];
static int nModos = 0;
static int pitchBytes = 0;
static char nombres[MAXMODOS][16];

/* E/S para la emulación de IN/OUT de vbe_rm.c, con las del servidor */
unsigned int vberm_inb(unsigned short p) { return inb(p); }
unsigned int vberm_inw(unsigned short p) { return inw(p); }
unsigned int vberm_inl(unsigned short p) { return inl(p); }
void vberm_outb(unsigned short p, unsigned int v) { outb(p, v); }
void vberm_outw(unsigned short p, unsigned int v) { outw(p, v); }
void vberm_outl(unsigned short p, unsigned int v) { outl(p, v); }

static int
vbe(VBERegs *r)
{
    int ret = VBErmInt10(r);
    if (ret)
        ErrorF("%s %s: error del BIOS: %s\n", XCONFIG_PROBED, vga256InfoRec.name, VBErmError);
    return ret;
}

static char *
VESAIdent(n)
int n;
{
    static char *chipsets[] = {"vesa"};
    if (n + 1 > sizeof(chipsets) / sizeof(char *))
        return NULL;
    return chipsets[n];
}

static void
VESAEnterLeave(enter)
Bool enter;
{
    unsigned char temp;

    if (enter) {
        xf86EnableIOPorts(vga256InfoRec.scrnIndex);
        vgaIOBase = (inb(0x3CC) & 0x01) ? 0x3D0 : 0x3B0;
        outb(vgaIOBase + 4, 0x11); temp = inb(vgaIOBase + 5);
        outb(vgaIOBase + 5, temp & 0x7F);
    } else {
        xf86DisableIOPorts(vga256InfoRec.scrnIndex);
    }
}

/* El modo pedido en XF86Config («1024x768») está en la lista del BIOS? Devuelve el índice o -1. */
static int
buscar(cand, n, nombre)
DisplayModePtr cand; int n; char *nombre;
{
    int i;
    for (i = 0; i < n; i++)
        if (!strcmp(cand[i].name, nombre))
            return i;
    return -1;
}

static void
llenar(m, ancho, alto, nombre)
DisplayModePtr m; int ancho, alto; char *nombre;
{
    /* tiempos inventados pero coherentes: el BIOS pone los suyos; X solo usa HDisplay/VDisplay */
    memset(m, 0, sizeof(*m));
    m->name = nombre;
    m->Clock = 0;
    m->HDisplay = ancho; m->HSyncStart = ancho + 16; m->HSyncEnd = ancho + 112; m->HTotal = ancho + 160;
    m->VDisplay = alto; m->VSyncStart = alto + 3; m->VSyncEnd = alto + 6; m->VTotal = alto + 30;
    m->CrtcHDisplay = m->HDisplay; m->CrtcHSyncStart = m->HSyncStart;
    m->CrtcHSyncEnd = m->HSyncEnd; m->CrtcHTotal = m->HTotal;
    m->CrtcVDisplay = m->VDisplay; m->CrtcVSyncStart = m->VSyncStart;
    m->CrtcVSyncEnd = m->VSyncEnd; m->CrtcVTotal = m->VTotal;
    m->SynthClock = 25175;
}

static Bool
VESAProbe()
{
    VBERegs r;
    unsigned char *info = VBE_LIN(VBE_SEG, VBE_OFF_INFO), *mi = VBE_LIN(VBE_SEG, VBE_OFF_MODE);
    unsigned short *lista, nums[256];
    DisplayModeRec cand[MAXMODOS];
    int candVBE[MAXMODOS], candPitch[MAXMODOS];
    int n = 0, nc = 0, i, j, bpp = vga256InfoRec.bitsPerPixel, verde;
    unsigned long lineal = 0;
    int memKB, maxAncho = 1280, maxAlto = 1024;

    xf86ClearIOPortList(vga256InfoRec.scrnIndex);
    xf86AddIOPorts(vga256InfoRec.scrnIndex, Num_VGA_IOPorts, VGA_IOPorts);

    if (vga256InfoRec.chipset && StrCaseCmp(vga256InfoRec.chipset, VESAIdent(0)))
        return FALSE;
    VESAEnterLeave(ENTER);

    if (VBErmInit()) {
        ErrorF("%s %s: vesa: %s\n", XCONFIG_PROBED, vga256InfoRec.name, VBErmError);
        VESAEnterLeave(LEAVE);
        return FALSE;
    }
    memcpy(info, "VBE2", 4);
    memset(&r, 0, sizeof r);
    r.eax = 0x4F00; r.es = VBE_SEG; r.edi = VBE_OFF_INFO;
    if (vbe(&r) || (r.eax & 0xFFFF) != 0x004F || memcmp(info, "VESA", 4) || info[5] < 2) {
        if (vga256InfoRec.chipset)
            ErrorF("%s %s: vesa: el BIOS no tiene VBE 2.0 o superior\n", XCONFIG_PROBED, vga256InfoRec.name);
        VESAEnterLeave(LEAVE);
        return FALSE;
    }
    memKB = *(unsigned short *)(info + 18) * 64;
    {
        unsigned char *oem = VBE_FAR(*(unsigned long *)(info + 6));
        char b[64];
        for (i = 0; i < 63 && oem[i]; i++) b[i] = oem[i];
        b[i] = 0;
        ErrorF("%s %s: vesa: VBE %d.%d, %d KB, OEM \"%s\"\n", XCONFIG_PROBED, vga256InfoRec.name,
               info[5], info[4], memKB, b);
    }
    lista = (unsigned short *)VBE_FAR(*(unsigned long *)(info + 14));
    while (n < 256 && lista[n] != 0xFFFF) { nums[n] = lista[n]; n++; }

    /* profundidad 15 o 16: lo dice el verde (5 o 6 bits) */
    verde = (bpp == 16) ? (vga256InfoRec.depth == 15 ? 5 : 6) : 0;
    if (vga256InfoRec.modes) {                       /* con Modes en XF86Config, el mas grande pedido es el tope */
        DisplayModePtr p = vga256InfoRec.modes;
        maxAncho = maxAlto = 0;
        do {
            int a, b;
            if (sscanf(p->name, "%dx%d", &a, &b) == 2) {
                if (a > maxAncho) maxAncho = a;
                if (b > maxAlto) maxAlto = b;
            }
            p = p->next;
        } while (p && p != vga256InfoRec.modes);
    }

    for (i = 0; i < n && nc < MAXMODOS; i++) {
        int ancho, alto, mbpp, modelo;
        memset(&r, 0, sizeof r);
        r.eax = 0x4F01; r.ecx = nums[i]; r.es = VBE_SEG; r.edi = VBE_OFF_MODE;
        if (vbe(&r) || (r.eax & 0xFFFF) != 0x004F)
            continue;
        if ((mi[0] & 0x91) != 0x91)                  /* soportado + gráfico + lineal */
            continue;
        ancho = *(unsigned short *)(mi + 18); alto = *(unsigned short *)(mi + 20);
        mbpp = mi[25]; modelo = mi[27];
        if (bpp == 8 && !(mbpp == 8 && modelo == 4)) continue;
        if (bpp == 16 && !((mbpp == 15 || mbpp == 16) && modelo == 6 && mi[33] == verde)) continue;
        if ((bpp == 24 || bpp == 32) && !(mbpp == bpp && modelo == 6)) continue;
        if (ancho > maxAncho || alto > maxAlto) continue;
        if ((long)ancho * alto * (bpp / 8) > (long)memKB * 1024) continue;
        sprintf(nombres[nc], "%dx%d", ancho, alto);
        if (buscar(cand, nc, nombres[nc]) >= 0) continue;      /* el mismo tamaño dos veces (15 y 16 bits) */
        llenar(&cand[nc], ancho, alto, nombres[nc]);
        candVBE[nc] = nums[i];
        candPitch[nc] = *(unsigned short *)(mi + 16);
        if (!lineal) lineal = *(unsigned long *)(mi + 40);
        nc++;
    }
    if (!nc || !lineal) {
        ErrorF("%s %s: vesa: el BIOS no tiene modos lineales de %d bpp\n", XCONFIG_PROBED,
               vga256InfoRec.name, bpp);
        VESAEnterLeave(LEAVE);
        return FALSE;
    }

    /* orden: el de XF86Config si hay Modes; si no, del mas grande al mas chico */
    nModos = 0;
    if (vga256InfoRec.modes) {
        DisplayModePtr p = vga256InfoRec.modes;
        do {
            j = buscar(cand, nc, p->name);
            if (j >= 0 && nModos < MAXMODOS) {
                modos[nModos] = cand[j]; numeroVBE[nModos] = candVBE[j]; nModos++;
            } else if (j < 0)
                ErrorF("%s %s: vesa: el BIOS no tiene el modo %s a %d bpp, se omite\n", XCONFIG_PROBED,
                       vga256InfoRec.name, p->name, bpp);
            p = p->next;
        } while (p && p != vga256InfoRec.modes);
    }
    if (!nModos) {
        int usado[MAXMODOS];
        memset(usado, 0, sizeof usado);
        for (i = 0; i < nc; i++) {
            int mejor = -1;
            for (j = 0; j < nc; j++)
                if (!usado[j] && (mejor < 0 ||
                    (long)cand[j].HDisplay * cand[j].VDisplay > (long)cand[mejor].HDisplay * cand[mejor].VDisplay))
                    mejor = j;
            usado[mejor] = 1;
            modos[nModos] = cand[mejor]; numeroVBE[nModos] = candVBE[mejor]; nModos++;
            break;      /* sin Modes: un solo modo, el mayor (sin pantalla virtual ni paneo, como un juego de DOS) */
        }
    }
    for (i = 0; i < nModos; i++) {                  /* lista circular */
        modos[i].next = &modos[(i + 1) % nModos];
        modos[i].prev = &modos[(i + nModos - 1) % nModos];
        modos[i].Private = (INT32 *)&numeroVBE[i];
        ErrorF("%s %s: vesa: modo %s = VBE 0x%x\n", XCONFIG_PROBED, vga256InfoRec.name,
               modos[i].name, numeroVBE[i]);
    }
    VESA.ChipBuiltinModes = &modos[0];

    if (!vga256InfoRec.videoRam)
        vga256InfoRec.videoRam = memKB;
    VESA.ChipLinearBase = lineal;
    VESA.ChipLinearSize = vga256InfoRec.videoRam * 1024;
    ErrorF("%s %s: vesa: framebuffer lineal en 0x%lx\n", XCONFIG_PROBED, vga256InfoRec.name, lineal);
    vgaSetPitchAdjustHook(VESAPitch);

    if (!vga256InfoRec.clocks) {                    /* nadie mide relojes: el BIOS pone los suyos */
        vga256InfoRec.clocks = 1;
        vga256InfoRec.clock[0] = 25175;
    }
    vga256InfoRec.maxClock = 1000000;
    vga256InfoRec.chipset = VESAIdent(0);
    vga256InfoRec.bankedMono = FALSE;
    return TRUE;
}

/* ancho de línea en pixeles: múltiplo de 8 del virtual; el BIOS lo ajusta con 4F06 al poner cada modo */
static int
VESAPitch()
{
    int w = (vga256InfoRec.virtualX + 7) & ~7;
    pitchBytes = w * (vga256InfoRec.bitsPerPixel / 8);
    return w;
}

static void *
VESASave(save)
vgaVESAPtr save;
{
    VBERegs r;
    save = (vgaVESAPtr)vgaHWSave((vgaHWPtr)save, sizeof(vgaVESARec));
    memset(&r, 0, sizeof r);
    r.eax = 0x4F03;
    if (!vbe(&r) && (r.eax & 0xFFFF) == 0x004F)
        save->vbeModo = r.ebx & 0x3FFF;
    else
        save->vbeModo = 3;
    save->grafico = (save->vbeModo & 0x1FF) >= 0x100;
    return (void *)save;
}

static void
VESARestore(restore)
vgaVESAPtr restore;
{
    VBERegs r;

    if (restore->grafico) {
        memset(&r, 0, sizeof r);
        r.eax = 0x4F02; r.ebx = (restore->vbeModo & 0x1FF) | 0xC000;      /* lineal y sin borrar la memoria: X no redibuja al cambiar de modo */
        if (vbe(&r) || (r.eax & 0xFFFF) != 0x004F) {
            ErrorF("%s %s: vesa: 4F02 fallo con el modo 0x%x (ax=%04lx)\n", XCONFIG_PROBED,
                   vga256InfoRec.name, restore->vbeModo, r.eax & 0xFFFF);
            return;
        }
        if (vga256InfoRec.displayWidth) {
            memset(&r, 0, sizeof r);
            r.eax = 0x4F06; r.ebx = 0; r.ecx = vga256InfoRec.displayWidth;
            vbe(&r);
            if ((r.eax & 0xFFFF) != 0x004F || (int)(r.ecx & 0xFFFF) != vga256InfoRec.displayWidth)
                ErrorF("%s %s: vesa: el BIOS dejo la linea en %ld pixeles (se pidieron %d)\n",
                       XCONFIG_PROBED, vga256InfoRec.name, r.ecx & 0xFFFF, vga256InfoRec.displayWidth);
        }
    } else {
        memset(&r, 0, sizeof r);
        if (restore->vbeModo < 0x100)
            r.eax = restore->vbeModo & 0xFF;          /* int 10h AH=0: modo de texto clásico */
        else {
            r.eax = 0x4F02; r.ebx = restore->vbeModo;
        }
        vbe(&r);
        vgaProtect(TRUE);
        vgaHWRestore((vgaHWPtr)restore);          /* registros y fuentes tal como estaban */
        vgaProtect(FALSE);
    }
}

static Bool
VESAInit(mode)
DisplayModePtr mode;
{
    if (!vgaHWInit(mode, sizeof(vgaVESARec)))
        return FALSE;
    new->vbeModo = mode->Private ? *(int *)mode->Private : numeroVBE[0];
    new->grafico = TRUE;
    return TRUE;
}

static void
VESAAdjust(x, y)
int x, y;
{
    static int avisado = 0;
    VBERegs r, q;
    memset(&r, 0, sizeof r);
    r.eax = 0x4F07; r.ebx = 0; r.ecx = x; r.edx = y;
    VBErmInt10(&r);
    if (!avisado && (x || y)) {
        memset(&q, 0, sizeof q); q.eax = 0x4F07; q.ebx = 1;      /* leer el comienzo que quedo */
        VBErmInt10(&q);
        if ((int)(q.ecx & 0xFFFF) != x || (int)(q.edx & 0xFFFF) != y) {
            ErrorF("%s %s: vesa: el BIOS no respeta 4F07 (se pidio %d,%d y quedo %ld,%ld): el paneo de la "
                   "pantalla virtual se vera corrido; use un solo modo en Modes\n", XCONFIG_PROBED,
                   vga256InfoRec.name, x, y, q.ecx & 0xFFFF, q.edx & 0xFFFF);
            avisado = 1;
        }
    }
}

static int
VESAValidMode(mode, verbose, flag)
DisplayModePtr mode;
Bool verbose;
int flag;
{
    return MODE_OK;
}
