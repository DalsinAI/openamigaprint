#include "oap.h"
#include <stdio.h>
#include <string.h>

int oap_pdf_write_demo(const char *path,const char *title)
{
    FILE *f; long off[6],xref; char stream[768]; int n,i;
    if(!path)return 0;
    if(!title)title="OpenPrint";
    f=fopen(path,"wb"); if(!f)return 0;
    fprintf(f,"%%PDF-1.4\n%%OAP1\n");
    off[1]=ftell(f); fprintf(f,"1 0 obj << /Type /Catalog /Pages 2 0 R >> endobj\n");
    off[2]=ftell(f); fprintf(f,"2 0 obj << /Type /Pages /Kids [3 0 R] /Count 1 >> endobj\n");
    off[3]=ftell(f); fprintf(f,"3 0 obj << /Type /Page /Parent 2 0 R /MediaBox [0 0 595 842] /Resources << /Font << /F1 5 0 R >> >> /Contents 4 0 R >> endobj\n");
    n=snprintf(stream,sizeof(stream),"0.94 g 40 560 515 210 re f 0 g 1 w 40 560 515 210 re S BT /F1 24 Tf 72 720 Td (OpenPrint) Tj 0 -40 Td /F1 13 Tf (%s) Tj 0 -30 Td (PDF generation + native IPP printing first light) Tj ET\n",title);
    if(n<0||(size_t)n>=sizeof(stream)){fclose(f);return 0;}
    off[4]=ftell(f); fprintf(f,"4 0 obj << /Length %d >> stream\n",n); fwrite(stream,1,n,f); fprintf(f,"endstream\nendobj\n");
    off[5]=ftell(f); fprintf(f,"5 0 obj << /Type /Font /Subtype /Type1 /BaseFont /Helvetica >> endobj\n");
    xref=ftell(f); fprintf(f,"xref\n0 6\n0000000000 65535 f \n");
    for(i=1;i<=5;i++)fprintf(f,"%010ld 00000 n \n",off[i]);
    fprintf(f,"trailer << /Size 6 /Root 1 0 R >>\nstartxref\n%ld\n%%%%EOF\n",xref);
    fclose(f); return 1;
}
