#include "clock.h"
#include "kernel_info.h"

static unsigned rtc(unsigned reg) { outb(0x70,reg); return inb(0x71); }
static int decimal(unsigned x,int binary) { return binary ? x : (x>>4)*10+(x&15); }
static int64_t rtc_epoch(void) {
    unsigned s=0,m=0,h=0,d=0,mo=0,y=0,mode=0;
    int stable=0;
    for(unsigned tries=0;tries<8;tries++) {
        if(rtc(0x0a)&0x80) continue;
        s=rtc(0); m=rtc(2); h=rtc(4); d=rtc(7); mo=rtc(8); y=rtc(9); mode=rtc(0x0b);
        if(!(rtc(0x0a)&0x80) && s==rtc(0)) { stable=1; break; }
    }
    if(!stable) return 0;
    int binary=mode&4, pm=h&0x80;
    int hour=decimal(h&127,binary);
    if(!(mode&2)) hour=hour%12+(pm?12:0);
    int year=2000+decimal(y,binary), month=decimal(mo,binary), day=decimal(d,binary);
    if(month<1||month>12||day<1||day>31||hour>23) return 0;
    year-=month<=2;
    int era=year/400, yoe=year-era*400;
    int doy=(153*(month+(month>2?-3:9))+2)/5+day-1;
    int64_t days=(int64_t)era*146097+yoe*365+yoe/4-yoe/100+doy-719468;
    return days*86400+hour*3600+decimal(m,binary)*60+decimal(s,binary);
}
int clock_read(wall_time_t *out,int offset) {
    int64_t stamp;
    if(g_sysinfo.boot_time>0 && g_sysinfo.tsc_freq_hz>=1000)
        stamp=g_sysinfo.boot_time+(int64_t)(get_uptime_ms()/1000);
    else stamp=rtc_epoch();
    if(stamp<=0) return 0;
    stamp+=offset*60;
    int64_t days=stamp/86400, seconds=stamp%86400;
    out->hour=seconds/3600; out->minute=seconds%3600/60; out->second=seconds%60;
    out->weekday=(days+3)%7; /* Monday = 0 */
    int64_t z=days+719468,era=z/146097,doe=z-era*146097;
    int64_t yoe=(doe-doe/1460+doe/36524-doe/146096)/365;
    int64_t year=yoe+era*400,doy=doe-(365*yoe+yoe/4-yoe/100),mp=(5*doy+2)/153;
    out->day=doy-(153*mp+2)/5+1; out->month=mp+(mp<10?3:-9);
    out->year=year+(out->month<=2);
    return 1;
}
static void two(char *p,int n) { p[0]='0'+n/10%10; p[1]='0'+n%10; }
void clock_time_text(const wall_time_t *t,char *out) {
    two(out,t->hour); out[2]=':'; two(out+3,t->minute); out[5]=':'; two(out+6,t->second); out[8]=0;
}
void clock_date_text(const wall_time_t *t,char *out) {
    two(out,t->day); out[2]='.'; two(out+3,t->month); out[5]='.';
    two(out+6,t->year/100); two(out+8,t->year%100); out[10]=0;
}
