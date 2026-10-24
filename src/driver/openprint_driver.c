/* Copyright (c) 2026 AmigaChrome Team. SPDX-License-Identifier: MIT */
#include <exec/types.h>
#include <exec/execbase.h>
#include <exec/memory.h>
#include <devices/printer.h>
#include <devices/prtbase.h>
#include <devices/prtgfx.h>
#include <intuition/preferences.h>
#include <proto/exec.h>

/* The object and page tables grow as a job needs (24 October 2026: they were 128 objects and 32 pages); object
 * numbers are UWORDs, so a job ends its pages at OAP_MAX_OBJECTS. */
#define OAP_MAX_OBJECTS 65000UL
#define OAP_TOP 42                      /* the first text line's baseline, below the page's top, in points */
#define OAP_BOTTOM 36                   /* the lowest baseline, above the page's bottom */
#define OAP_LEFT 40

#define OAP_PRS_INIT 0
#define OAP_PRS_TRANSFER 1
#define OAP_PRS_FLUSH 2
#define OAP_PRS_CLEAR 3
#define OAP_PRS_CLOSE 4
#define OAP_PRS_PREINIT 5

struct PrinterData *PD;
struct ExecBase *SysBase;
extern struct PrinterExtendedData oap_ped;

char oap_printer_name[] = "OpenPrint";
static UBYTE special_cmd[] = { 0xff, 0 };
#define S (STRPTR)special_cmd
STRPTR oap_commands[77] = {
 S,S,S,S,S,S,S,S,S,S,S,S,S,S,S,S,S,S,S,S,
 S,S,S,S,S,S,S,S,S,S,S,S,S,S,S,S,S,S,S,S,
 S,S,S,S,S,S,S,S,S,S,S,S,S,S,S,S,S,S,S,S,
 S,S,S,S,S,S,S,S,S,S,S,S,S,S,S,S,S
};
#undef S

static ULONG g_offset;
static ULONG *g_obj_offset;             /* AllocMem'd, g_obj_cap entries */
static ULONG g_obj_cap;
static UWORD g_next_obj;
static UWORD *g_page_ids;               /* AllocMem'd, g_page_cap entries */
static ULONG g_page_cap;
static UWORD g_page_count;
static UWORD g_line, g_lines;           /* the text line on this page, and lines to a page */
static UWORD g_font, g_lead10;          /* Courier's size and the line spacing in tenths, both in points */
static UWORD g_content_obj, g_length_obj, g_page_obj;
static ULONG g_content_start;
static UWORD g_page_w, g_page_h;
static UWORD g_dpi;
static UWORD g_img_w, g_img_h;
static ULONG g_img_row_len, g_img_row_cap;
static UBYTE g_doc_open, g_page_open, g_text_open, g_img_open;

static ULONG slen(const char *s)
{
    ULONG n=0; while(s&&s[n])n++; return n;
}
static void pw_n(const void *data, ULONG len)
{
    if(!PD||!data||!len)return;
    PD->pd_PWrite((APTR)data,(LONG)len);
    PD->pd_PBothReady();
    g_offset += len;
}
static void pw(const char *s){pw_n(s,slen(s));}
static void pw_u(ULONG v)
{
    char b[11]; int n=0,i; if(!v){pw("0");return;}
    while(v&&n<10){b[n++]=(char)('0'+(v%10));v/=10;}
    for(i=n-1;i>=0;i--)pw_n(&b[i],1);
}
static void pw_fixed10(ULONG v)
{
    char b[10]; int i;
    for(i=9;i>=0;i--){b[i]=(char)('0'+(v%10));v/=10;}
    pw_n(b,10);
}
/* a table of n entries of size bytes grown so entry need fits; 0 when there's no memory */
static int grow(void **t,ULONG *cap,ULONG need,ULONG size)
{
    ULONG n;APTR m;
    if(need<*cap)return 1;
    for(n=*cap?*cap:64;n<=need;n*=2);
    m=AllocMem(n*size,MEMF_PUBLIC|MEMF_CLEAR);if(!m)return 0;
    if(*t){CopyMem(*t,m,*cap*size);FreeMem(*t,*cap*size);}
    *t=m;*cap=n;return 1;
}
static void free_tables(void)
{
    if(g_obj_offset)FreeMem(g_obj_offset,g_obj_cap*sizeof(ULONG));
    if(g_page_ids)FreeMem(g_page_ids,g_page_cap*sizeof(UWORD));
    g_obj_offset=0;g_obj_cap=0;g_page_ids=0;g_page_cap=0;
}
static void obj_start(UWORD id)
{
    if(grow((void **)&g_obj_offset,&g_obj_cap,id,sizeof(ULONG)))g_obj_offset[id]=g_offset;
    pw_u(id);pw(" 0 obj\n");
}
static UWORD alloc_obj(void)
{
    if(g_next_obj>=OAP_MAX_OBJECTS||!grow((void **)&g_obj_offset,&g_obj_cap,g_next_obj,sizeof(ULONG)))return 0;
    return g_next_obj++;
}
/* The paper from the prefs: A4 (and A3, A5), US Letter or US Legal. Text pages are upright; a page a picture
 * starts is turned when the prefs' aspect is horizontal, as the picture is wider than tall (24 October 2026: text
 * pages were turned too, and 66 lines don't fit across). */
static void geometry(int picture)
{
    UWORD w=595,h=842,ps=PD?PD->pd_Preferences.PaperSize:EURO_A4;
    if(ps==US_LETTER){w=612;h=792;}
    else if(ps==US_LEGAL){w=612;h=1008;}
    else if(ps==EURO_A3){w=842;h=1191;}
    else if(ps==EURO_A5){w=420;h=595;}
    if(picture && PD && PD->pd_Preferences.PrintAspect==ASPECT_HORIZ){UWORD t=w;w=h;h=t;}
    g_page_w=w;g_page_h=h;
}
/* Text: Courier (fixed pitch, so old programs' columns line up) at the prefs' pitch, pica 10 point (6 points a
 * character), elite 8 and fine 6; lines at the prefs' spacing (6 or 8 to the inch), closer when the prefs' paper
 * length in lines (66 by default) needs it to fit the page; a page ends after that many lines. */
static void text_geometry(void)
{
    ULONG room,lead,n=66;
    UWORD pitch=PD?PD->pd_Preferences.PrintPitch:PICA;
    g_font=(UWORD)(pitch==ELITE?8:pitch==FINE?6:10);
    lead=(PD&&PD->pd_Preferences.PrintSpacing==EIGHT_LPI)?90:120;
    if(PD&&PD->pd_Preferences.PaperLength)n=PD->pd_Preferences.PaperLength;
    if(n<2)n=2;
    if(n>999)n=999;
    room=(g_page_h>OAP_TOP+OAP_BOTTOM+10)?(ULONG)(g_page_h-OAP_TOP-OAP_BOTTOM)*10:10;
    if(room/(n-1)<lead)lead=room/(n-1);
    if(!lead)lead=1;
    g_lead10=(UWORD)lead;g_lines=(UWORD)n;
}
static void reset_state(void)
{
    g_offset=0;g_next_obj=4;g_page_count=0;g_line=0;
    g_doc_open=g_page_open=g_text_open=g_img_open=0;
    g_img_w=g_img_h=0;g_img_row_len=g_img_row_cap=0;g_dpi=300;
    free_tables();
    geometry(0);text_geometry();
}
static void begin_doc(void)
{
    if(g_doc_open)return;
    pw("%PDF-1.4\n% OpenPrint\n");
    obj_start(1);pw("<< /Type /Catalog /Pages 2 0 R >>\nendobj\n");
    obj_start(3);pw("<< /Type /Font /Subtype /Type1 /BaseFont /Courier >>\nendobj\n");
    g_doc_open=1;
}
static void begin_page(int picture)
{
    if(g_page_open)return;
    begin_doc();
    if(!grow((void **)&g_page_ids,&g_page_cap,g_page_count,sizeof(UWORD)))return;
    g_content_obj=alloc_obj();g_length_obj=alloc_obj();g_page_obj=alloc_obj();
    if(!g_content_obj||!g_length_obj||!g_page_obj)return;
    geometry(picture);text_geometry();
    obj_start(g_content_obj);
    pw("<< /Length ");pw_u(g_length_obj);pw(" 0 R >>\nstream\n");
    g_content_start=g_offset;g_page_open=1;g_text_open=0;g_line=0;
}
static void close_text(void)
{
    if(g_text_open){pw(") Tj ET\n");g_text_open=0;}
}
static void ensure_text(void)
{
    ULONG top;
    if(!g_page_open)begin_page(0);
    if(g_text_open||!g_page_open)return;
    top=(g_page_h>OAP_TOP+8)?(ULONG)g_page_h-OAP_TOP:10;
    if(g_line)top-=(ULONG)g_line*g_lead10/10;       /* after a picture on the same page */
    pw("BT /F1 ");pw_u(g_font);pw(" Tf ");pw_u(OAP_LEFT);pw(" ");pw_u(top);pw(" Td (");
    g_text_open=1;
}
static void end_page(void);
/* The next line; past the page's last line, a new page (the next character starts it). */
static void text_newline(void)
{
    ensure_text();
    if(!g_text_open)return;
    if(g_line+1>=g_lines){end_page();return;}
    g_line++;
    pw(") Tj 0 -");pw_u(g_lead10/10);
    if(g_lead10%10){pw(".");pw_u(g_lead10%10);}
    pw(" Td (");
}
static void end_page(void)
{
    ULONG len;
    if(!g_page_open)return;
    if(g_img_open){pw(">\nEI\nQ\n");g_img_open=0;}
    close_text();
    len=g_offset-g_content_start;
    pw("endstream\nendobj\n");
    obj_start(g_length_obj);pw_u(len);pw("\nendobj\n");
    obj_start(g_page_obj);
    pw("<< /Type /Page /Parent 2 0 R /MediaBox [0 0 ");
    pw_u(g_page_w);pw(" ");pw_u(g_page_h);
    pw("] /Resources << /Font << /F1 3 0 R >> >> /Contents ");
    pw_u(g_content_obj);pw(" 0 R >>\nendobj\n");
    g_page_ids[g_page_count++]=g_page_obj;
    g_page_open=0;g_text_open=0;g_line=0;
}
static void finish_doc(void)
{
    UWORD i,maxobj; ULONG xref;
    if(!g_doc_open)return;
    end_page();
    obj_start(2);pw("<< /Type /Pages /Count ");pw_u(g_page_count);pw(" /Kids [");
    for(i=0;i<g_page_count;i++){pw_u(g_page_ids[i]);pw(" 0 R ");}
    pw("] >>\nendobj\n");
    xref=g_offset;maxobj=(UWORD)(g_next_obj-1);
    pw("xref\n0 ");pw_u((ULONG)maxobj+1);pw("\n");
    pw("0000000000 65535 f \n");
    for(i=1;i<=maxobj;i++){pw_fixed10(g_obj_offset[i]);pw(" 00000 n \n");}
    pw("trailer << /Size ");pw_u((ULONG)maxobj+1);pw(" /Root 1 0 R >>\n");
    pw("startxref\n");pw_u(xref);pw("\n%%EOF\n");
    PD->pd_PBothReady();
    g_doc_open=0;
    free_tables();
}
static void text_char(UBYTE c)
{
    char q[4];
    ensure_text(); if(!g_text_open)return;
    if(c=='('||c==')'||c=='\\'){q[0]='\\';q[1]=(char)c;pw_n(q,2);return;}
    if(c>=32&&c<127){q[0]=(char)c;pw_n(q,1);return;}
    q[0]='\\';q[1]=(char)('0'+((c>>6)&7));q[2]=(char)('0'+((c>>3)&7));q[3]=(char)('0'+(c&7));
    pw_n(q,4);
}
static UBYTE hx(UBYTE v){v&=15;return (UBYTE)(v<10?'0'+v:'A'+v-10);}
static void set_density(ULONG flags)
{
    switch(flags&SPECIAL_DENSITYMASK){
    case SPECIAL_DENSITY1:g_dpi=72;break;
    case SPECIAL_DENSITY2:g_dpi=100;break;
    case SPECIAL_DENSITY3:g_dpi=150;break;
    case SPECIAL_DENSITY4:g_dpi=200;break;
    case SPECIAL_DENSITY5:g_dpi=300;break;
    case SPECIAL_DENSITY6:g_dpi=600;break;
    case SPECIAL_DENSITY7:g_dpi=1200;break;
    default:g_dpi=300;break;
    }
}
LONG PRT_STDARGS oap_init(struct PrinterData *pd)
{
    /*
     * The published PrinterExtendedData prototype declares ped_Init VOID,
     * but AmigaOS 3.2.x printer.device tests D0 after calling it and treats
     * a non-zero value as unit-initialisation failure.  Do not let a C tail
     * call leak geometry()/reset_state()'s working value into D0.
     */
    PD=pd;
    /* Classic printer-driver init glue obtains ExecBase from _AbsExecBase
     * (the canonical pointer stored at absolute address 4), not from
     * PrinterData.  OS 3.2.x leaves pd_Device.dd_ExecBase clear here. */
    /* (an asm read: GCC 12 and later reject a C read of address 4 as out of bounds) */
#ifdef __mc68000__
    __asm__ volatile("move.l 4.w,%0" : "=a"(SysBase));
#endif
    if(!SysBase&&pd->pd_Device.dd_ExecBase)SysBase=(struct ExecBase *)pd->pd_Device.dd_ExecBase;
    reset_state();
    return 0;
}
void PRT_STDARGS oap_expunge(void){free_tables();PD=0;}
int PRT_STDARGS oap_open(struct IORequest *ior)
{
    (void)ior;reset_state();return 0;
}
void PRT_STDARGS oap_close(struct IORequest *ior)
{
    (void)ior;finish_doc();
    if(PD&&PD->pd_PrintBuf){FreeMem(PD->pd_PrintBuf,g_img_row_cap);PD->pd_PrintBuf=0;}
}
LONG PRT_STDARGS oap_conv(STRPTR buf,TEXT c,LONG crlf)
{
    (void)buf;(void)crlf;
    /* Leave ESC/CSI to printer.device so ANSI commands such as ESC#1
     * reach DoSpecial instead of leaking their printable tail into the PDF. */
    if((UBYTE)c==0x1b || (UBYTE)c==0x9b)return -1;
    if(c=='\n'){text_newline();return 0;}
    if(c=='\r')return 0;
    if(c=='\014'){end_page();return 0;}
    if(c=='\t'){text_char(' ');text_char(' ');text_char(' ');text_char(' ');return 0;}
    if((UBYTE)c<32)return 0;
    text_char((UBYTE)c);return 0;
}
LONG PRT_STDARGS oap_special(UWORD *command,UBYTE out[],BYTE *pos,BYTE *spacing,BYTE *crlf,STRPTR params)
{
    (void)out;(void)pos;(void)spacing;(void)crlf;(void)params;
    if(!command)return -2;
    switch(*command){
    case aRIN:begin_doc();break;
    case aIND:
    case aNEL:text_newline();break;
    case aRIS:break;
    default:break;
    }
    /* All printer control commands are consumed here.  Never leak them into PDF. */
    return 0;
}
static LONG render_preinit(struct IODRPReq *io,LONG flags)
{
    ULONG x,y;(void)io;set_density((ULONG)flags);geometry(1);
    x=(ULONG)g_page_w*g_dpi/72;y=(ULONG)g_page_h*g_dpi/72;
    oap_ped.ped_XDotsInch=g_dpi;oap_ped.ped_YDotsInch=g_dpi;
    oap_ped.ped_MaxXDots=x;oap_ped.ped_MaxYDots=y;
    oap_ped.ped_MaxColumns=80;return PDERR_NOERR;
}
static LONG render_init(struct IODRPReq *io,LONG width,LONG height)
{
    ULONG draw_w,draw_h,y;
    (void)io;if(width<=0||height<=0)return PDERR_BADDIMENSION;
    begin_page(1);close_text();
    g_img_w=(UWORD)width;g_img_h=(UWORD)height;
    g_img_row_cap=(ULONG)g_img_w*6+1;
    g_img_row_len=g_img_row_cap;
    PD->pd_PrintBuf=AllocMem(g_img_row_cap,MEMF_PUBLIC);
    if(!PD->pd_PrintBuf)return PDERR_BUFFERMEMORY;
    draw_w=(g_page_w>80)?g_page_w-80:g_page_w;
    draw_h=draw_w*(ULONG)g_img_h/g_img_w;
    if(draw_h>(ULONG)g_page_h-80){draw_h=g_page_h-80;draw_w=draw_h*(ULONG)g_img_w/g_img_h;}
    y=(g_page_h>draw_h+40)?g_page_h-draw_h-40:0;
    pw("q\n");pw_u(draw_w);pw(" 0 0 ");pw_u(draw_h);pw(" 40 ");pw_u(y);pw(" cm\n");
    pw("BI /W ");pw_u(g_img_w);pw(" /H ");pw_u(g_img_h);
    pw(" /CS /RGB /BPC 8 /F /AHx ID\n");
    g_img_open=1;return PDERR_NOERR;
}
static void put_rgb_hex(UBYTE *dst,ULONG *pos,UBYTE r4,UBYTE g4,UBYTE b4)
{
    UBYTE r=(UBYTE)((r4&15)*17),g=(UBYTE)((g4&15)*17),b=(UBYTE)((b4&15)*17);
    dst[(*pos)++]=hx(r>>4);dst[(*pos)++]=hx(r);
    dst[(*pos)++]=hx(g>>4);dst[(*pos)++]=hx(g);
    dst[(*pos)++]=hx(b>>4);dst[(*pos)++]=hx(b);
}
static LONG render_clear(void)
{
    ULONG i;if(!PD->pd_PrintBuf||!g_img_row_cap)return PDERR_CANCEL;
    for(i=0;i+1<g_img_row_cap;i++)PD->pd_PrintBuf[i]='F';
    PD->pd_PrintBuf[g_img_row_cap-1]='\n';g_img_row_len=g_img_row_cap;
    return PDERR_NOERR;
}
static LONG render_transfer(struct PrtInfo *pi,LONG row)
{
    ULONG src_i,out,p,i;union colorEntry *src;UWORD *scale;
    (void)row;
    if(!pi||!PD->pd_PrintBuf||!pi->pi_ColorInt)return PDERR_CANCEL;
    /* printer.device supplies a full destination row and pi_xpos tells the
     * driver where the scaled source begins (for centering/margins). */
    for(i=0;i+1<g_img_row_cap;i++)PD->pd_PrintBuf[i]='F';
    PD->pd_PrintBuf[g_img_row_cap-1]='\n';
    out=pi->pi_xpos;if(out>g_img_w)out=g_img_w;p=out*6;
    src=pi->pi_ColorInt;scale=pi->pi_ScaleX;
    for(src_i=0;src_i<pi->pi_width && out<g_img_w;src_i++,src++){
        UWORD repeat=scale?scale[src_i]:1;UWORD n;
        UBYTE r=src->colorByte[PCMRED],g=src->colorByte[PCMGREEN],b=src->colorByte[PCMBLUE];
        if(!repeat)repeat=1;
        for(n=0;n<repeat && out<g_img_w;n++,out++)put_rgb_hex(PD->pd_PrintBuf,&p,r,g,b);
    }
    g_img_row_len=g_img_row_cap;
    return PDERR_NOERR;
}
static LONG render_flush(LONG rows)
{
    if(rows<=0)return PDERR_NOERR;
    if(PD->pd_PrintBuf&&g_img_row_len)pw_n(PD->pd_PrintBuf,g_img_row_len);
    return PDERR_NOERR;
}
static LONG render_close(LONG error,ULONG flags)
{
    if(error!=PDERR_CANCEL){
        if(g_img_open){pw(">\nEI\nQ\n");g_img_open=0;}
        if(!(flags&SPECIAL_NOFORMFEED))end_page();
    } else {
        g_img_open=0;g_page_open=0;g_text_open=0;g_doc_open=0;
    }
    if(PD)PD->pd_PBothReady();
    if(PD&&PD->pd_PrintBuf){FreeMem(PD->pd_PrintBuf,g_img_row_cap);PD->pd_PrintBuf=0;}
    g_img_row_len=g_img_row_cap=0;
    return PDERR_NOERR;
}

LONG PRT_STDARGS oap_render(LONG ct,LONG x,LONG y,LONG status,...)
{
    switch(status){
    case OAP_PRS_PREINIT:return render_preinit((struct IODRPReq *)ct,x);
    case OAP_PRS_INIT:return render_init((struct IODRPReq *)ct,x,y);
    case OAP_PRS_TRANSFER:return render_transfer((struct PrtInfo *)ct,y);
    case OAP_PRS_FLUSH:return render_flush(y);
    case OAP_PRS_CLEAR:return render_clear();
    case OAP_PRS_CLOSE:return render_close(ct,(ULONG)x);
    default:return PDERR_NOERR;
    }
}
