/* Copyright (c) 2026 Dalsin Limited. SPDX-License-Identifier: MIT */
#include "oap_discovery.h"
#include <string.h>
#include <ctype.h>
#include <stdint.h>
static size_t line_end(const unsigned char *p,size_t n,size_t s){size_t i;for(i=s;i+1<n;i++)if(p[i]=='\r'&&p[i+1]=='\n')return i;return n;}
static int equals(const unsigned char *p,size_t n,const char *s){size_t i;if(strlen(s)!=n)return 0;for(i=0;i<n;i++)if(tolower(p[i])!=tolower((unsigned char)s[i]))return 0;return 1;}
static int number(const unsigned char *p,size_t n,size_t *out,unsigned base){size_t x=0,i;if(!n)return 0;for(i=0;i<n;i++){unsigned v;if(p[i]>='0'&&p[i]<='9')v=p[i]-'0';else if(base==16&&tolower(p[i])>='a'&&tolower(p[i])<='f')v=tolower(p[i])-'a'+10;else return 0;if(v>=base||x>(OAP_HTTP_MAX-v)/base)return 0;x=x*base+v;}*out=x;return 1;}
int oap_http_response(const unsigned char *p,size_t n,int eof,unsigned char *body,size_t cap,size_t *len,int *status){size_t start=0,headers=0;if(n>OAP_HTTP_MAX)return -1;*len=0;for(;;){size_t e=line_end(p,n,start),x,cl=0;int have_cl=0,chunked=0,ct=0,st;if(e==n)return eof?-1:0;if(e-start<12||memcmp(p+start,"HTTP/1.",7)||p[start+8]!=' '||p[start+9]<'1'||p[start+9]>'5'||!isdigit(p[start+10])||!isdigit(p[start+11]))return -1;st=(p[start+9]-'0')*100+(p[start+10]-'0')*10+p[start+11]-'0';x=e+2;for(;;){size_t colon,v,ve,z;e=line_end(p,n,x);if(e==n)return eof?-1:0;if(e==x){x+=2;break;}if(++headers>256||e-x>8192)return -1;colon=x;while(colon<e&&p[colon]!=':')colon++;if(colon==e)return -1;v=colon+1;while(v<e&&(p[v]==' '||p[v]=='\t'))v++;ve=e;while(ve>v&&(p[ve-1]==' '||p[ve-1]=='\t'))ve--;
 if(equals(p+x,colon-x,"content-length")){if(!number(p+v,ve-v,&z,10)||z>cap||(have_cl&&z!=cl))return -1;cl=z;have_cl=1;}
 else if(equals(p+x,colon-x,"transfer-encoding")){if(chunked||!equals(p+v,ve-v,"chunked"))return -1;chunked=1;}
 else if(equals(p+x,colon-x,"content-type")){size_t end=v;while(end<ve&&p[end]!=';'&&p[end]!=' ')end++;ct=equals(p+v,end-v,"application/ipp");}
 x=e+2;
 }
 if(st>=100&&st<200){if(st==101||chunked||(have_cl&&cl))return -1;start=x;continue;}
 *status=st;if(st<200||st>=300)return -1;if(!ct||(have_cl&&chunked))return -1;
 if(chunked){size_t used=0;for(;;){size_t size,semi;e=line_end(p,n,x);if(e==n)return eof?-1:0;semi=x;while(semi<e&&p[semi]!=';')semi++;if(!number(p+x,semi-x,&size,16)||size>cap-used)return -1;x=e+2;if(!size){unsigned trailers=0;for(;;){e=line_end(p,n,x);if(e==n)return eof?-1:0;if(e==x){*len=used;return 1;}if(++trailers>64)return -1;x=e+2;}}if(n-x<size+2)return eof?-1:0;if(p[x+size]!='\r'||p[x+size+1]!='\n')return -1;memcpy(body+used,p+x,size);used+=size;x+=size+2;}}
 if(have_cl){if(n-x<cl)return eof?-1:0;memcpy(body,p+x,cl);*len=cl;return 1;}
 if(!eof)return 0;
 if(n-x>cap)return -1;
 memcpy(body,p+x,n-x);*len=n-x;return 1;
 }}

/* The value of header `name` in the final (not 1xx) response in p[0..n),
 * as text; 1 when found. For WWW-Authenticate after a 401. */
int oap_http_header(const unsigned char *p, size_t n, const char *name, char *out, size_t cap)
{
    size_t start = 0, nl = strlen(name);
    if (!cap)
        return 0;
    out[0] = 0;
    for (;;) {
        size_t e = line_end(p, n, start), x;
        int informational;
        if (e == n || e - start < 12 || memcmp(p + start, "HTTP/1.", 7))
            return 0;
        informational = p[start + 9] == '1';
        x = e + 2;
        for (;;) {
            size_t colon, v, ve;
            e = line_end(p, n, x);
            if (e == n)
                return 0;
            if (e == x) {
                x += 2;
                break;
            }
            colon = x;
            while (colon < e && p[colon] != ':')
                colon++;
            if (!informational && colon < e && equals(p + x, colon - x, name) && colon - x == nl) {
                size_t k = 0;
                v = colon + 1;
                while (v < e && (p[v] == ' ' || p[v] == '\t'))
                    v++;
                ve = e;
                while (ve > v && (p[ve - 1] == ' ' || p[ve - 1] == '\t'))
                    ve--;
                for (; v < ve && k + 1 < cap; v++)
                    out[k++] = p[v] < 32 ? ' ' : (char)p[v];
                out[k] = 0;
                return 1;
            }
            x = e + 2;
        }
        if (!informational)
            return 0;
        start = x;
    }
}
