#include "gps_model.h"
#include <math.h>
#include <stdlib.h>
#include <string.h>

static int hex(char c)
{
    if (c>='0' && c<='9') return c-'0';
    if (c>='A' && c<='F') return c-'A'+10;
    if (c>='a' && c<='f') return c-'a'+10;
    return -1;
}
static bool decimal(const char *s,double *out)
{
    if (!s || !*s) return false;
    bool dot=false;
    for (const char *p=s;*p;++p) {
        if (*p=='.' && !dot && p!=s && p[1]) dot=true;
        else if (*p<'0' || *p>'9') return false;
    }
    char *end; *out=strtod(s,&end); return !*end && isfinite(*out);
}
static bool coordinate(const char *s,const char *hem,bool latitude,double *out)
{
    unsigned digits=latitude?4:5;
    if (strlen(s)<digits || (s[digits] && s[digits]!='.') || strlen(hem)!=1) return false;
    if (latitude ? (*hem!='N' && *hem!='S') : (*hem!='E' && *hem!='W')) return false;
    double raw; if (!decimal(s,&raw) || raw>(latitude?9000:18000)) return false;
    unsigned degrees=(unsigned)(raw/100); double minutes=raw-degrees*100;
    if (minutes>=60 || degrees>(latitude?90u:180u) || (degrees==(latitude?90u:180u) && minutes>0)) return false;
    *out=(degrees+minutes/60)*((*hem=='S' || *hem=='W')?-1:1); return true;
}
static bool parse(char *line,uint32_t now,gps_fix_t *fix)
{
    char *star=strchr(line,'*');
    if (!star || strlen(star)!=3 || hex(star[1])<0 || hex(star[2])<0) return false;
    unsigned sum=0; for (char *p=line+1;p<star;++p) sum^=(unsigned char)*p;
    if (sum!=(unsigned)(hex(star[1])*16+hex(star[2]))) return false;
    *star=0;
    char *f[20]; unsigned n=0; f[n++]=line;
    for (char *p=line;*p;++p) if (*p==',') { *p=0; if (n>=20) return false; f[n++]=p+1; }
    if (n<10 || strlen(f[0])!=6 || strcmp(f[0]+3,"RMC")) return false;
    if (f[0][1]<'A' || f[0][1]>'Z' || f[0][2]<'A' || f[0][2]>'Z') return false;
    if (strcmp(f[2],"A") && strcmp(f[2],"V")) return false;
    double utc;
    /* A cold receiver can publish V before it knows UTC. Clear the old fix. */
    if ((*f[1] || *f[2]=='A') && (strlen(f[1])<6 || (f[1][6] && f[1][6]!='.') || !decimal(f[1],&utc) ||
        utc>=240000 || (int)(utc/100)%100>=60 || fmod(utc,100)>=61)) return false;
    gps_fix_t next={.enabled=true,.received_ms=now,.sequence=fix->sequence+1};
    bool unusable=*f[2]=='V' || (n>12 && (*f[12]=='N' || *f[12]=='S' || *f[12]=='M'));
    if (!unusable) {
        if (!coordinate(f[3],f[4],true,&next.latitude) || !coordinate(f[5],f[6],false,&next.longitude)) return false;
        double knots;
        if (*f[7] && (!decimal(f[7],&knots) || knots>1000)) return false;
        if (*f[7]) { next.speed_kph=(float)(knots*1.852); next.speed_valid=true; }
        next.valid=true;
    }
    *fix=next; return true;
}
bool gps_nmea_feed(gps_nmea_t *p,char c,uint32_t now,gps_fix_t *fix)
{
    if (!p || !fix) return false;
    if (c=='$') { p->length=0; p->collecting=true; }
    if (!p->collecting) return false;
    if (c=='\r' || c=='\n') {
        p->collecting=false; p->line[p->length]=0;
        return parse(p->line,now,fix);
    }
    if ((unsigned char)c<32 || (unsigned char)c>126 || p->length>=sizeof(p->line)-1) {
        p->collecting=false; p->length=0; return false;
    }
    p->line[p->length++]=c; return false;
}
bool gps_fix_current(const gps_fix_t *fix,uint32_t now)
{
    return fix && fix->enabled && !fix->error && fix->valid &&
        (uint32_t)(now-fix->received_ms)<GPS_FIX_TTL_MS &&
        isfinite(fix->latitude) && isfinite(fix->longitude) &&
        fabs(fix->latitude)<=85.05112878 && fabs(fix->longitude)<=180;
}
