/* Host-side checks for the exact calendar conversion used by the kernel. */
#include "../kernel/clock.h"
#include "../kernel/kernel_info.h"
extern int puts(const char *);
sysinfo_t g_sysinfo;
static uint64_t elapsed;
uint64_t get_uptime_ms(void) { return elapsed; }
static int check(int64_t base,uint64_t ms,int zone,int year,int month,int day,int hour,int minute,int second,int weekday) {
    g_sysinfo.boot_time=base; g_sysinfo.tsc_freq_hz=1000000; elapsed=ms;
    wall_time_t t;
    return clock_read(&t,zone) && t.year==year && t.month==month && t.day==day &&
        t.hour==hour && t.minute==minute && t.second==second && t.weekday==weekday;
}
int main(void) {
    if(!check(1798750799,1000,180,2027,1,1,0,0,0,4)) return 1;
    if(!check(1709164799,1000,0,2024,2,29,0,0,0,3)) return 2;
    if(!check(1709251199,1000,0,2024,3,1,0,0,0,4)) return 3;
    if(!check(1793923200,0,-60,2026,11,5,23,0,0,3)) return 4;
    if(!check(1793923200,3723000,180,2026,11,6,4,2,3,4)) return 5;
    wall_time_t t={2026,11,6,4,2,3,4}; char s[16];
    clock_time_text(&t,s);
    if(s[0]!='0'||s[1]!='4'||s[2]!=':'||s[3]!='0'||s[4]!='2'||s[6]!='0'||s[7]!='3'||s[8]) return 6;
    clock_date_text(&t,s);
    if(s[0]!='0'||s[1]!='6'||s[3]!='1'||s[4]!='1'||s[6]!='2'||s[9]!='6'||s[10]) return 7;
    puts("PASS clock: live seconds, UTC offsets, day/year rollover, leap day, weekday, formatting.");
    return 0;
}
