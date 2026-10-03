#include "storage/or_config.h"
#include <ctype.h>
#include <stdio.h>
#include <string.h>
typedef struct { const char *p; unsigned depth; OR_Config config; bool saw_values; } Parser;
static void space(Parser *p) { while (isspace((unsigned char)*p->p)) ++p->p; }
static bool string(Parser *p, char *out, size_t cap) {
    space(p); if (*p->p++ != '"') return false;
    size_t n=0; bool overflow=false;
    while (*p->p && *p->p!='"') {
        unsigned char c=(unsigned char)*p->p++;
        if (c<32) return false;
        if (c=='\\') {
            c=(unsigned char)*p->p++; if (!c) return false;
            if (c=='u') {
                for (int i=0;i<4;++i) { if (!isxdigit((unsigned char)*p->p)) return false; ++p->p; }
                c='?'; /* Escaped/unknown keys never match native settings. */
            } else if (!strchr("\"\\/bfnrt",c)) return false;
        }
        if (out && n+1<cap) out[n++]=(char)c; else if (out) overflow=true;
    }
    if (*p->p++!='"') return false;
    if (out) { out[n]=0; if (overflow) out[0]=0; }
    return true;
}
static bool value(Parser *p, bool settings);
static bool object(Parser *p, bool settings, bool root) {
    if (++p->depth>8) return false;
    ++p->p; space(p);
    if (*p->p=='}') { ++p->p; --p->depth; return true; }
    bool seen_dialog=false,seen_diag=false,seen_schema=false;
    for (;;) {
        char key[64]; if (!string(p,key,sizeof(key))) return false;
        space(p); if (*p->p++!=':') return false; space(p);
        if (root && !strcmp(key,"values")) {
            if (p->saw_values || *p->p!='{') return false;
            p->saw_values=true; if (!object(p,true,false)) return false;
        } else if (root && !strcmp(key,"schemaVersion")) {
            if (seen_schema || *p->p!='1') return false;
            seen_schema=true; ++p->p;
        } else if (settings && (!strcmp(key,"enableBossDialog") || !strcmp(key,"enableDiagnostics"))) {
            bool *seen=!strcmp(key,"enableBossDialog") ? &seen_dialog : &seen_diag;
            if (*seen) return false;
            *seen=true; bool v;
            if (!strncmp(p->p,"true",4)) { v=true; p->p+=4; }
            else if (!strncmp(p->p,"false",5)) { v=false; p->p+=5; }
            else return false;
            if (!strcmp(key,"enableBossDialog")) p->config.boss_dialog=v; else p->config.diagnostics=v;
        } else if (!value(p,false)) return false;
        space(p); if (*p->p=='}') { ++p->p; --p->depth; return true; }
        if (*p->p++!=',') return false;
        space(p);
    }
}
static bool value(Parser *p, bool settings) {
    space(p);
    if (*p->p=='{') return object(p,settings,false);
    if (*p->p=='"') return string(p,NULL,0);
    if (*p->p=='[') {
        if (++p->depth>8) return false;
        ++p->p; space(p);
        if (*p->p!=']') for (;;) {
            if (!value(p,false)) return false;
            space(p);
            if (*p->p==']') break;
            if (*p->p++!=',') return false;
        }
        ++p->p; --p->depth; return true;
    }
    if (!strncmp(p->p,"true",4)) { p->p+=4; return true; }
    if (!strncmp(p->p,"false",5)) { p->p+=5; return true; }
    if (!strncmp(p->p,"null",4)) { p->p+=4; return true; }
    const char *start=p->p;
    if (*p->p=='-') ++p->p;
    if (*p->p=='0') ++p->p;
    else { if (*p->p<'1'||*p->p>'9') return false; while (isdigit((unsigned char)*p->p)) ++p->p; }
    if (*p->p=='.') { ++p->p; if (!isdigit((unsigned char)*p->p)) return false; while (isdigit((unsigned char)*p->p)) ++p->p; }
    if (*p->p=='e'||*p->p=='E') {
        ++p->p; if (*p->p=='+'||*p->p=='-') ++p->p;
        if (!isdigit((unsigned char)*p->p)) return false;
        while (isdigit((unsigned char)*p->p)) ++p->p;
    }
    return p->p!=start;
}
OR_Config or_config_default(void) { return (OR_Config){true,true}; }
bool or_config_parse(const char *text, OR_Config *out) {
    if (!text || !out) return false;
    Parser p={text,0,or_config_default(),false}; space(&p);
    if (*p.p!='{' || !object(&p,false,true)) return false;
    space(&p); if (*p.p || !p.saw_values) return false;
    *out=p.config; return true;
}
bool or_config_load(const char *dir, OR_Config *out, bool *found) {
    *out=or_config_default(); *found=false;
    if (!dir || !*dir) return true;
    char path[1024],text[4097];
    int n=snprintf(path,sizeof(path),"%s/config.json",dir);
    if (n<0 || (size_t)n>=sizeof(path)) return false;
    FILE *f=fopen(path,"rb"); if (!f) return true;
    *found=true; size_t len=fread(text,1,sizeof(text)-1,f);
    bool ok=!ferror(f) && fgetc(f)==EOF; fclose(f); text[len]=0;
    if (!ok || memchr(text,0,len)) return false;
    return or_config_parse(text,out);
}
