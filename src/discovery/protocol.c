/* Copyright (c) 2026 Dalsin Limited. SPDX-License-Identifier: MIT */
/* Bounded DNS-SD/IPP codec. Network labels remain wire encoded. */
#include "oap_discovery.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
static unsigned u16(const unsigned char *p){return ((unsigned)p[0]<<8)|p[1];}
static uint32_t u32(const unsigned char *p){return ((uint32_t)p[0]<<24)|((uint32_t)p[1]<<16)|((uint32_t)p[2]<<8)|p[3];}
static void put16(unsigned char *p,unsigned n){p[0]=(unsigned char)(n>>8);p[1]=(unsigned char)n;}
static int eq(const char *a,const char *b){while(*a&&*b){if(tolower((unsigned char)*a++)!=tolower((unsigned char)*b++))return 0;}return !*a&&!*b;}
static void text(char *out,size_t cap,const unsigned char *in,size_t n){size_t i;if(!cap)return;if(n>=cap)n=cap-1;for(i=0;i<n;i++)out[i]=(in[i]<32||in[i]==127)?' ':(char)in[i];out[n]=0;}
static int nameeq(const OAPName *a,const OAPName *b){size_t i;if(a->len!=b->len)return 0;for(i=0;i<a->len;i++)if(tolower(a->wire[i])!=tolower(b->wire[i]))return 0;return 1;}
int oap_dns_name_text(const char *s,OAPName *n){size_t k=0;memset(n,0,sizeof(*n));while(*s){const char *e=strchr(s,'.');size_t z=e?(size_t)(e-s):strlen(s);if(!z||z>63||k+z+2>255)return 0;n->wire[k++]=(unsigned char)z;memcpy(n->wire+k,s,z);k+=z;if(!e)break;s=e+1;}n->wire[k++]=0;n->len=k;return 1;}
static int name_read(const unsigned char *p,size_t len,size_t *off,OAPName *n){size_t x=*off,end=0,k=0;unsigned hops=0;memset(n,0,sizeof(*n));while(x<len){unsigned z=p[x++];if((z&0xc0)==0xc0){size_t dest;if(x>=len||++hops>32)return 0;dest=((size_t)(z&63)<<8)|p[x++];if(dest>=len)return 0;if(!end)end=x;x=dest;continue;}if(z&0xc0||z>63||x+z>len||k+z+1>255)return 0;n->wire[k++]=(unsigned char)z;if(!z){n->len=k;*off=end?end:x;return 1;}memcpy(n->wire+k,p+x,z);k+=z;x+=z;}return 0;}
static void name_display(const OAPName *n,char *out,size_t cap){size_t x=0,k=0;while(x<n->len&&n->wire[x]){unsigned z=n->wire[x++],i;if(k&&k+1<cap)out[k++]='.';for(i=0;i<z&&x<n->len;i++){unsigned c=n->wire[x++];if(k+1<cap)out[k++]=(c<32||c==127)?'_':(char)c;}}if(cap)out[k]=0;}
static int service_type(const OAPName *n){OAPName ipp,ipps,suffix;size_t skip;if(!n->len||!n->wire[0])return 0;skip=1+n->wire[0];if(skip>=n->len)return 0;oap_dns_name_text("_ipp._tcp.local",&ipp);oap_dns_name_text("_ipps._tcp.local",&ipps);memset(&suffix,0,sizeof(suffix));suffix.len=n->len-skip;memcpy(suffix.wire,n->wire+skip,suffix.len);if(nameeq(&suffix,&ipp))return 1;if(nameeq(&suffix,&ipps))return 2;return 0;}
size_t oap_dns_query(const OAPName *n,unsigned type,uint16_t id,unsigned char *p,size_t cap){if(!n||n->len<1||n->len>255||cap<16+n->len)return 0;memset(p,0,12);put16(p,id);put16(p+4,1);memcpy(p+12,n->wire,n->len);put16(p+12+n->len,type);put16(p+14+n->len,1);return 16+n->len;}
static OAPDiscovered *service(OAPDiscovery *d,const OAPName *name){size_t i;int kind=service_type(name);OAPDiscovered *e;if(!kind)return NULL;for(i=0;i<d->count;i++)if(nameeq(&d->printers[i].service,name))return &d->printers[i];if(d->count==OAP_DISC_MAX)return NULL;e=&d->printers[d->count++];memset(e,0,sizeof(*e));e->service=*name;e->secure=kind==2;e->alive=1;e->caps.accepting=-1;text(e->label,sizeof(e->label),name->wire+1,name->wire[0]);strcpy(e->note,"Resolving advertised endpoint");return e;}
static int valid_path(const char *s){const unsigned char *p=(const unsigned char *)s;if(!s[0])return 0;for(;*p;p++)if(*p<=32||*p>=127||*p=='#'||*p=='\\')return 0;return 1;}
static void txt(OAPDiscovered *e,const unsigned char *p,size_t n){size_t x=0;while(x<n){unsigned z=p[x++];const unsigned char *v;size_t key;if(x+z>n)return;v=memchr(p+x,'=',z);if(v){key=(size_t)(v-(p+x));if(key==2&&tolower(p[x])=='r'&&tolower(p[x+1])=='p'){char b[256];text(b,sizeof(b),v+1,z-key-1);if(valid_path(b)&&strlen(b)+2<sizeof(e->path)){if(b[0]=='/')strcpy(e->path,b);else{e->path[0]='/';strcpy(e->path+1,b);}}}else if(key==2&&tolower(p[x])=='t'&&tolower(p[x+1])=='y')text(e->label,sizeof(e->label),v+1,z-key-1);}x+=z;}e->have_txt=1;}
static int dns_pass(OAPDiscovery *d,const unsigned char *p,size_t n,int pass){size_t x=12,i;unsigned total,qd;if(n<12||!(p[2]&0x80)||(p[2]&0x7a)||(p[3]&15))return 0;qd=u16(p+4);total=u16(p+6)+u16(p+8)+u16(p+10);if(qd>96||total>256)return 0;for(i=0;i<qd;i++){OAPName q;if(!name_read(p,n,&x,&q)||n-x<4)return 0;x+=4;}for(i=0;i<total;i++){OAPName name,target;size_t begin,end,y;unsigned type,cl,rd;uint32_t ttl;OAPDiscovered *e;
 if(!name_read(p,n,&x,&name)||n-x<10)return 0;
 type=u16(p+x);cl=u16(p+x+2)&0x7fff;ttl=u32(p+x+4);rd=u16(p+x+8);x+=10;begin=x;end=x+rd;if(end>n)return 0;
 if(type==12||type==33){y=begin+(type==33?6:0);if(y>end||!name_read(p,n,&y,&target)||y!=end)return 0;}
 if(type==16){y=begin;while(y<end){unsigned z=p[y++];if(y+z>end)return 0;y+=z;}}
 if(pass>=0&&cl==1){
  if(pass==0&&type==12){OAPName ipp,ipps;oap_dns_name_text("_ipp._tcp.local",&ipp);oap_dns_name_text("_ipps._tcp.local",&ipps);if(nameeq(&name,&ipp)||nameeq(&name,&ipps)){e=service(d,&target);if(e)e->alive=ttl!=0;}}
  if(pass==1&&(type==33||type==16)){e=service(d,&name);if(e&&ttl){if(type==33){e->target=target;e->port=u16(p+begin+4);e->have_srv=e->port!=0;name_display(&target,e->host,sizeof(e->host));}else txt(e,p+begin,rd);}}
  if(pass==2&&type==1&&rd==4){size_t j;for(j=0;j<d->host_count;j++)if(nameeq(&d->hosts[j].name,&name))break;if(j<d->host_count||j<OAP_HOST_MAX){OAPHost *h=&d->hosts[j];if(j==d->host_count)d->host_count++;h->name=name;if(ttl)snprintf(h->ip,sizeof(h->ip),"%u.%u.%u.%u",p[begin],p[begin+1],p[begin+2],p[begin+3]);else h->ip[0]=0;}}
 }
 x=end;
 }return 1;}
int oap_dns_packet(OAPDiscovery *d,const unsigned char *p,size_t n){int pass;if(!dns_pass(d,p,n,-1))return 0;for(pass=0;pass<3;pass++)dns_pass(d,p,n,pass);oap_discovery_resolve(d);return 1;}
void oap_discovery_resolve(OAPDiscovery *d){size_t i,j;for(i=0;i<d->count;i++){OAPDiscovered *e=&d->printers[i];e->ip[0]=0;e->uri[0]=0;for(j=0;j<d->host_count;j++)if(nameeq(&e->target,&d->hosts[j].name)){strcpy(e->ip,d->hosts[j].ip);break;}if(e->alive&&e->have_srv&&e->ip[0]&&e->path[0]){char uri[640];snprintf(uri,sizeof(uri),"%s://%s:%u%s",e->secure?"ipps":"ipp",e->secure?e->host:e->ip,e->port,e->path);strcpy(e->uri,uri);}}}
static int attr(unsigned char *p,size_t cap,size_t *x,unsigned tag,const char *name,const char *v){size_t a=strlen(name),b=strlen(v);if(a>65535||b>65535||*x+5+a+b>cap)return 0;p[(*x)++]=(unsigned char)tag;put16(p+*x,(unsigned)a);*x+=2;memcpy(p+*x,name,a);*x+=a;put16(p+*x,(unsigned)b);*x+=2;memcpy(p+*x,v,b);*x+=b;return 1;}
size_t oap_ipp_query_request(const char *uri,uint32_t id,unsigned char *p,size_t cap){size_t x=9,i;static const char *a[]={"document-format-supported","printer-name","printer-make-and-model","printer-location","printer-state","printer-state-reasons","printer-is-accepting-jobs","color-supported","sides-supported"};if(cap<9)return 0;p[0]=1;p[1]=1;p[2]=0;p[3]=11;p[4]=(unsigned char)(id>>24);p[5]=(unsigned char)(id>>16);p[6]=(unsigned char)(id>>8);p[7]=(unsigned char)id;p[8]=1;if(!attr(p,cap,&x,0x47,"attributes-charset","utf-8")||!attr(p,cap,&x,0x48,"attributes-natural-language","en")||!attr(p,cap,&x,0x45,"printer-uri",uri))return 0;for(i=0;i<sizeof(a)/sizeof(*a);i++)if(!attr(p,cap,&x,0x44,i?"":"requested-attributes",a[i]))return 0;if(x==cap)return 0;p[x++]=3;return x;}
int oap_ipp_parse_caps(const unsigned char *p,size_t n,uint32_t id,OAPCaps *out){OAPCaps c;size_t x=8;char name[256]="";unsigned group=0;int formats=0,depth=0;memset(&c,0,sizeof(c));c.accepting=-1;if(n<9||(p[0]!=1&&p[0]!=2)||u32(p+4)!=id)return 0;c.status=u16(p+2);if(c.status>=0x0100)return 0;while(x<n){unsigned tag=p[x++],a,b;const unsigned char *v;if(tag==3){if(depth)return 0;if(formats&&!c.pdf)c.pdf=OAP_PDF_NO;*out=c;return 1;}if(tag<0x10){group=tag;name[0]=0;continue;}if(n-x<2)return 0;a=u16(p+x);x+=2;if(a>255||n-x<a+2)return 0;if(a){text(name,sizeof(name),p+x,a);x+=a;}else if(!name[0])return 0;b=u16(p+x);x+=2;if(n-x<b)return 0;v=p+x;x+=b;if(tag==0x34){if(++depth>16)return 0;continue;}if(tag==0x37){if(!depth)return 0;depth--;continue;}if(group==2&&!depth&&eq(name,"job-id")&&tag==0x21&&b==4)c.job_id=u32(v);if(group!=4||depth)continue;
 if(eq(name,"document-format-supported")&&tag==0x49){formats=1;if(b==15&&memcmp(v,"application/pdf",15)==0)c.pdf=OAP_PDF_YES;}
 else if(eq(name,"printer-name")&&(tag==0x42||tag==0x41))text(c.name,sizeof(c.name),v,b);
 else if(eq(name,"printer-make-and-model")&&tag==0x41)text(c.model,sizeof(c.model),v,b);
 else if(eq(name,"printer-location")&&tag==0x41)text(c.location,sizeof(c.location),v,b);
 else if(eq(name,"printer-state")&&tag==0x23&&b==4)c.state=(int)u32(v);
 else if(eq(name,"printer-is-accepting-jobs")&&tag==0x22&&b==1)c.accepting=v[0]?1:0;
 else if(eq(name,"color-supported")&&tag==0x22&&b==1)c.color=v[0]?1:0;
 else if(eq(name,"sides-supported")&&tag==0x44&&b>=10&&memcmp(v,"two-sided-",10)==0)c.duplex=1;
 else if(eq(name,"printer-state-reasons")&&tag==0x44){size_t used=strlen(c.reasons);if(used+2<sizeof(c.reasons)){if(used){c.reasons[used++]=',';c.reasons[used++]=' ';}text(c.reasons+used,sizeof(c.reasons)-used,v,b);}}
 }return 0;}
int oap_pdf_eligible(const OAPDiscovered *p){return p&&p->alive&&!p->secure&&p->uri[0]&&p->caps.pdf==OAP_PDF_YES;}
static void safe_field(FILE *f,const char *s){while(*s){unsigned char c=(unsigned char)*s++;fputc(c<32||c==127?' ':c,f);}}
int oap_discovery_save(const char *path,const OAPDiscovery *d,int done,const char *note){char tmp[768];FILE *f;size_t i;if(strlen(path)+5>=sizeof(tmp))return 0;snprintf(tmp,sizeof(tmp),"%s.tmp",path);f=fopen(tmp,"w");if(!f)return 0;fprintf(f,"OAPB1\t%d\t",done);safe_field(f,note);fputc('\n',f);for(i=0;i<d->count;i++){const OAPDiscovered *p=&d->printers[i];if(!p->alive)continue;fprintf(f,"P\t%d\t%d\t%d\t",p->caps.pdf,p->caps.accepting,p->secure);safe_field(f,p->caps.model[0]?p->caps.model:p->label);fputc('\t',f);safe_field(f,p->uri);fputc('\t',f);safe_field(f,p->note);fputc('\n',f);}{int bad=ferror(f);if(fclose(f))bad=1;if(bad){remove(tmp);return 0;}}
#ifdef __amigaos__
 remove(path);
#endif
 if(rename(tmp,path)){remove(tmp);return 0;}return 1;}
int oap_discovery_load(const char *path,OAPDiscovery *d,int *done,char *note,size_t cap){FILE *f;char line[1600];size_t count=0;f=fopen(path,"r");if(!f)return 0;if(!fgets(line,sizeof(line),f)||strncmp(line,"OAPB1\t",6)){fclose(f);return 0;}*done=atoi(line+6);{char *p=strchr(line+6,'\t');if(p){p++;p[strcspn(p,"\r\n")]=0;text(note,cap,(unsigned char *)p,strlen(p));}}memset(d,0,sizeof(*d));while(count<OAP_DISC_MAX&&fgets(line,sizeof(line),f)){char *parts[7],*p=line;int j;for(j=0;j<7;j++){parts[j]=p;p=strchr(p,'\t');if(j<6&&!p)break;if(p)*p++=0;}if(j<7||strcmp(parts[0],"P"))continue;{OAPDiscovered *e=&d->printers[count++];e->alive=1;e->caps.pdf=atoi(parts[1]);e->caps.accepting=atoi(parts[2]);e->secure=atoi(parts[3]);text(e->label,sizeof(e->label),(unsigned char *)parts[4],strlen(parts[4]));text(e->uri,sizeof(e->uri),(unsigned char *)parts[5],strlen(parts[5]));parts[6][strcspn(parts[6],"\r\n")]=0;text(e->note,sizeof(e->note),(unsigned char *)parts[6],strlen(parts[6]));}}d->count=count;fclose(f);return 1;}
