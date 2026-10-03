/* Bare 68000 helpers used by the printer driver. */
unsigned long __udivsi3(unsigned long n,unsigned long d)
{
    unsigned long q=0,r=0;
    int i;
    if(!d)return 0xffffffffUL;
    for(i=31;i>=0;i--){
        r=(r<<1)|((n>>i)&1UL);
        if(r>=d){r-=d;q|=(1UL<<i);}
    }
    return q;
}
unsigned long __umodsi3(unsigned long n,unsigned long d)
{
    unsigned long r=0;
    int i;
    if(!d)return n;
    for(i=31;i>=0;i--){
        r=(r<<1)|((n>>i)&1UL);
        if(r>=d)r-=d;
    }
    return r;
}
long __mulsi3(long a,long b)
{
    unsigned long x=(unsigned long)a,y=(unsigned long)b,r=0;
    while(y){if(y&1UL)r+=x;x<<=1;y>>=1;}
    return (long)r;
}
