/*
 * ROGUE 6502 - A roguelike game for the MOS 6502 @ 3.375MHz
 *
 * Inspired by Rogue / NetHack. For Tang Nano 9K FPGA with SID 6581 audio.
 * Display: ANSI terminal via UART serial (115200 baud).
 * Compiled with CC65: cl65 -t none -O --cpu 6502
 *
 * Memory map:
 *   Zero page : $20-$7F  (reserved for runtime)
 *   RAM       : $0800-$3DFF (~13.5 KB)
 *   Stack     : $3E00-$3FFF
 */

#include <stdint.h>
#include "romapi.h"

#define MAP_W           27
#define MAP_H           10
#define MAX_ROOMS       6
#define MAX_MONSTERS    7
#define MAX_ITEMS       4
#define VIEW_RADIUS     5
#define MSG_LEN         30
#define NUM_MON_TYPES   8
#define NUM_ITEM_TYPES  11

#define T_WALL          0
#define T_FLOOR         1
#define T_STAIRS        2

#define COL_RESET       0
#define COL_WHITE       37
#define COL_BRIGHT_WHITE 97
#define COL_DARK_GRAY   90
#define COL_BRIGHT_GREEN 92
#define COL_BRIGHT_RED  91
#define COL_BRIGHT_YELLOW 93
#define COL_BRIGHT_CYAN 96
#define COL_BRIGHT_MAGENTA 95

#define SID             ((volatile uint8_t*)0xD400U)
#define NUM_DIRS        8

static const char MON_CHARS[NUM_MON_TYPES] = {
    'r','g','E','s','O','F','T','D'
};
static const uint8_t MON_HP[NUM_MON_TYPES] = {
    5,10,18,12,22,16,30,50
};
static const uint8_t MON_ATK[NUM_MON_TYPES] = {
    2,4,6,5,8,7,10,14
};
static const uint8_t MON_DEF[NUM_MON_TYPES] = {
    0,1,2,2,3,1,4,6
};
static const uint8_t MON_XP[NUM_MON_TYPES] = {
    3,6,12,8,16,20,25,50
};
static const uint8_t MON_MIN_F[NUM_MON_TYPES] = {
    1,1,2,3,4,5,7,9
};
static const char ITEM_CHARS[NUM_ITEM_TYPES] = {
    '!','!','!','?','?',')','[','=','%','*','"'
};

static uint8_t map[MAP_H][MAP_W];
static uint8_t vis[MAP_H][MAP_W];
static uint8_t rx[MAX_ROOMS], ry[MAX_ROOMS], rw[MAX_ROOMS], rh[MAX_ROOMS];
static uint8_t num_rooms;
static uint8_t px, py, hp, max_hp, ba, bd;
static uint8_t hw, ha, hr, has_amulet;
static uint8_t lvl, floor_n;
static uint16_t xp;
static uint8_t mx[MAX_MONSTERS], my[MAX_MONSTERS];
static uint8_t mt[MAX_MONSTERS], mhp[MAX_MONSTERS], md[MAX_MONSTERS];
static uint8_t nm;
static uint8_t ix[MAX_ITEMS], iy[MAX_ITEMS], it[MAX_ITEMS], iu[MAX_ITEMS];
static uint8_t ni;
static char msg[MSG_LEN];
static char mb[20];  /* Buffer global para mensajes */
static uint16_t rng;

static const int8_t dx8[8] = {0,1,1,1,0,-1,-1,-1};
static const int8_t dy8[8] = {-1,-1,0,1,1,1,0,-1};
static const int8_t dx4[4] = {0,1,0,-1};
static const int8_t dy4[4] = {-1,0,1,0};

static uint8_t prand(void);
static void uart_gotoxy(uint8_t row, uint8_t col);
static void u8_to_s(uint8_t v, char* b);
static int16_t abs16(int16_t v);
static void sid_init(void);
static void sid_play(uint16_t f, uint8_t d);
static void sid_pickup(void);
static void sid_lvlup(void);
static void sid_hit(void);
static void sid_die(void);
static void sid_win(void);
static void sid_stairs(void);
static void sid_sword(void);
static void sid_monster(void);
static void msg_set(const char* s);
static uint8_t tatl(void);
static uint8_t tdef(void);
static uint8_t mat(uint8_t y, uint8_t x);
static uint8_t iat(uint8_t y, uint8_t x);
static void gen_dun(uint8_t fn);
static void fov(void);
static void mv_mons(void);
static uint8_t patk(uint8_t mi);
static uint8_t matk(uint8_t mi, uint8_t vis2);
static void pickup(void);
static void rend(void);
static uint8_t loop(void);
static void show_help(void);
static void show_status(void);

static void srand_c(uint16_t s) { rng = s ? s : 1; }
static uint8_t prand(void) {
    rng ^= rng << 7; rng ^= rng >> 9; rng ^= rng << 8;
    return (uint8_t)rng;
}
static uint8_t prr(uint8_t m) {
    if (!m) return 0;
    return (uint8_t)(((uint16_t)prand() * (uint16_t)m) >> 8);
}
static int16_t abs16(int16_t v) { return v < 0 ? -v : v; }

static void u8_to_s(uint8_t v, char* b) {
    if (v >= 200) { b[0]='2'; b[1]='0'+(uint8_t)((v-200)/10); b[2]='0'+(uint8_t)((v-200)%10); b[3]=0; }
    else if (v >= 100) { b[0]='1'; b[1]='0'+(uint8_t)((v-100)/10); b[2]='0'+(uint8_t)((v-100)%10); b[3]=0; }
    else if (v >= 10) { b[0]='0'+(uint8_t)(v/10); b[1]='0'+(uint8_t)(v%10); b[2]=0; }
    else { b[0]='0'+v; b[1]=0; }
}

static void uart_gotoxy(uint8_t row, uint8_t col) {
    char rb[4], cb[4];
    uint8_t i;
    rom_uart_putc(0x1B); rom_uart_putc('[');
    i = 0; u8_to_s(row+1, rb); while (rb[i]) { rom_uart_putc(rb[i]); i++; }
    rom_uart_putc(';');
    i = 0; u8_to_s(col+1, cb); while (cb[i]) { rom_uart_putc(cb[i]); i++; }
    rom_uart_putc('H');
}

static void sid_init(void) {
    SID[0x04]=0; SID[0x0B]=0; SID[0x12]=0; SID[0x18]=0x0F;
}
static void sid_play(uint16_t f, uint8_t d) {
    SID[0x00]=(uint8_t)(f&0xFF); SID[0x01]=(uint8_t)(f>>8);
    SID[0x05]=0x09; SID[0x06]=0xF9; SID[0x04]=0x41;
    rom_delay_ms(d); SID[0x04]=0x40; rom_delay_ms(2);
}
static void sid_hit(void) { sid_play(0x0800,30); }
static void sid_pickup(void) { sid_play(0x0620,20); rom_delay_ms(10); sid_play(0x0930,20); }
static void sid_lvlup(void) { sid_play(0x0620,60); rom_delay_ms(20); sid_play(0x07BA,60); rom_delay_ms(20); sid_play(0x0930,80); }
static void sid_die(void) { sid_play(0x0500,80); rom_delay_ms(20); sid_play(0x0300,80); rom_delay_ms(20); sid_play(0x0200,80); rom_delay_ms(20); sid_play(0x0100,150); }
static void sid_win(void) {
    uint8_t i; uint16_t n[5]={0x0620,0x07BA,0x0930,0x0C44,0x0930};
    for (i=0;i<5;i++) { sid_play(n[i],80); rom_delay_ms(15); }
}
static void sid_stairs(void) { sid_play(0x0800,50); rom_delay_ms(15); sid_play(0x0400,60); }
static void sid_sword(void) { sid_play(0x0E00,15); }
static void sid_monster(void) { sid_play(0x0200,50); }

static void msg_clear(void) { msg[0]=0; }
static void msg_set(const char* s) {
    uint8_t i=0;
    while (s[i] && i<MSG_LEN-1) { msg[i]=s[i]; i++; }
    msg[i]=0;
}

static uint8_t tatl(void) { return ba+(hw?2:0); }
static uint8_t tdef(void) { return bd+(ha?2:0)+(hr?1:0); }

static uint8_t mat(uint8_t y, uint8_t x) {
    uint8_t i;
    for (i=0;i<nm;i++) { if (!md[i] && my[i]==y && mx[i]==x) return i+1; }
    return 0;
}
static uint8_t iat(uint8_t y, uint8_t x) {
    uint8_t i;
    for (i=0;i<ni;i++) { if (!iu[i] && iy[i]==y && ix[i]==x) return i+1; }
    return 0;
}

static void gen_dun(uint8_t fn) {
    uint8_t i,j,y,x,a=200,r,rr,rw2,rh2,ox,oy,ov,d,vt[NUM_MON_TYPES],nv,ch,pl,nh,ir,it2;
    ni=0; nm=0; num_rooms=0;
    for (y=0;y<MAP_H;y++) for (x=0;x<MAP_W;x++) { map[y][x]=T_WALL; vis[y][x]=0; }
    srand_c((uint16_t)(fn*137+42));
    r = 5+prr(4); if (r>MAX_ROOMS) r=MAX_ROOMS;
    a=0;
    while (num_rooms<r && a<200) {
        a++; rw2=4+prr(7); rh2=3+prr(4);
        ox=1+prr((uint8_t)(MAP_W-rw2-2)); oy=1+prr((uint8_t)(MAP_H-rh2-2));
        ov=0;
        for (rr=0;rr<num_rooms&&!ov;rr++) {
            if (ox<rx[rr]+rw[rr]+2 && ox+rw2+2>rx[rr] && oy<ry[rr]+rh[rr]+2 && oy+rh2+2>ry[rr]) ov=1;
        }
        if (ov) continue;
        rx[num_rooms]=ox; ry[num_rooms]=oy; rw[num_rooms]=rw2; rh[num_rooms]=rh2;
        for (j=0;j<rh2;j++) for (i=0;i<rw2;i++) map[oy+j][ox+i]=T_FLOOR;
        num_rooms++;
    }
    for (rr=1;rr<num_rooms;rr++) {
        uint8_t cx1=rx[rr-1]+rw[rr-1]/2, cy1=ry[rr-1]+rh[rr-1]/2;
        uint8_t cx2=rx[rr]+rw[rr]/2, cy2=ry[rr]+rh[rr]/2;
        if (prand()&1) {
            y=cy1; while ((int16_t)y!=(int16_t)cy2) { if (cx1<MAP_W) map[y][cx1]=T_FLOOR; y+=(cy2>cy1)?1:-1; }
            x=cx1; while ((int16_t)x!=(int16_t)cx2) { if (y<MAP_H) map[y][x]=T_FLOOR; x+=(cx2>cx1)?1:-1; }
        } else {
            x=cx1; while ((int16_t)x!=(int16_t)cx2) { if (cy1<MAP_H) map[cy1][x]=T_FLOOR; x+=(cx2>cx1)?1:-1; }
            y=cy1; while ((int16_t)y!=(int16_t)cy2) { if (x<MAP_W) map[y][x]=T_FLOOR; y+=(cy2>cy1)?1:-1; }
        }
    }
    if (num_rooms>0) { px=rx[0]+rw[0]/2; py=ry[0]+rh[0]/2; }
    if (num_rooms>0) { uint8_t ls=num_rooms-1; map[ry[ls]+rh[ls]/2][rx[ls]+rw[ls]/2]=T_STAIRS; }
    for (rr=1;rr<num_rooms&&nm<MAX_MONSTERS;rr++) {
        nh=1+prr(2); if (nh>MAX_MONSTERS-nm) nh=MAX_MONSTERS-nm;
        for (pl=0;pl<nh&&nm<MAX_MONSTERS;pl++) {
            a=0; ch=0;
            while (!ch&&a<20) {
                a++; x=rx[rr]+prr(rw[rr]); y=ry[rr]+prr(rh[rr]);
                if (map[y][x]==T_FLOOR && !mat(y,x) && !(x==px&&y==py)) ch=1;
            }
            if (ch) {
                nv=0; for (d=0;d<NUM_MON_TYPES;d++) if (MON_MIN_F[d]<=fn) vt[nv++]=d;
                if (nv) {
                    d=vt[prr(nv)]; mx[nm]=x; my[nm]=y; mt[nm]=(uint8_t)(d+1);
                    mhp[nm]=MON_HP[d]; md[nm]=0; nm++;
                }
            }
        }
    }
    ir=1+prr(3); if (ir>MAX_ITEMS) ir=MAX_ITEMS;
    for (i=0;i<ir&&ni<MAX_ITEMS;i++) {
        rr=prr(num_rooms); a=0; ch=0;
        while (!ch&&a<30) {
            a++; x=rx[rr]+prr(rw[rr]); y=ry[rr]+prr(rh[rr]);
            if (map[y][x]==T_FLOOR && !iat(y,x) && !mat(y,x) && !(x==px&&y==py)) ch=1;
        }
        if (ch) {
            uint8_t r2=prr(100);
            if (r2<20) it2=1; else if (r2<30) it2=2; else if (r2<40) it2=3;
            else if (r2<50) it2=4; else if (r2<55) it2=5;
            else if (r2<65) it2=6; else if (r2<72) it2=7;
            else if (r2<78) it2=8; else if (r2<85) it2=9;
            else if (r2<97) it2=10; else it2=11;
            if (it2==11 && fn<10) it2=10;
            ix[ni]=x; iy[ni]=y; it[ni]=it2; iu[ni]=0; ni++;
        }
    }
    fov();
}

static void fov(void) {
    int16_t dy2,dx2,tx,ty;
    uint8_t y,x;
    for (y=0;y<MAP_H;y++) for (x=0;x<MAP_W;x++) if (vis[y][x]==2) vis[y][x]=1;
    for (dy2=-VIEW_RADIUS;dy2<=(int16_t)VIEW_RADIUS;dy2++) {
        for (dx2=-VIEW_RADIUS;dx2<=(int16_t)VIEW_RADIUS;dx2++) {
            tx=(int16_t)px+dx2; ty=(int16_t)py+dy2;
            if (tx<0||tx>=MAP_W||ty<0||ty>=MAP_H) continue;
            if (abs16(dx2)+abs16(dy2)>(int16_t)VIEW_RADIUS) continue;
            vis[(uint8_t)ty][(uint8_t)tx]=2;
        }
    }
}

static void mv_mons(void) {
    uint8_t i,d,nx,ny,cs,bd2,bd3;
    for (i=0;i<nm;i++) {
        if (md[i]) continue;
        cs=0;
        if (vis[my[i]][mx[i]]==2 && abs16((int16_t)my[i]-(int16_t)py)+abs16((int16_t)mx[i]-(int16_t)px)<=VIEW_RADIUS)
            cs=1;
        if (cs) {
            bd2=0; bd3=255;
            for (d=0;d<8;d++) {
                nx=mx[i]+(uint8_t)dx8[d]; ny=my[i]+(uint8_t)dy8[d];
                if (nx>=MAP_W||ny>=MAP_H) continue;
                if (nx==px&&ny==py) { matk(i,1); goto done; }
                if (map[ny][nx]==T_WALL||mat(ny,nx)) continue;
                { int16_t md2=abs16((int16_t)ny-(int16_t)py)+abs16((int16_t)nx-(int16_t)px); if ((uint8_t)md2<bd3) { bd3=(uint8_t)md2; bd2=d; } }
            }
            if (bd3<255) { mx[i]+=(uint8_t)dx8[bd2]; my[i]+=(uint8_t)dy8[bd2]; }
        } else {
            if (prr(100)<50) {
                d=prr(4); nx=mx[i]+(uint8_t)dx4[d]; ny=my[i]+(uint8_t)dy4[d];
                if (nx<MAP_W&&ny<MAP_H&&map[ny][nx]!=T_WALL&&!mat(ny,nx)&&!(nx==px&&ny==py)) { mx[i]=nx; my[i]=ny; }
            }
        }
        done: ;
    }
}

static uint8_t patk(uint8_t mi) {
    uint8_t ti=mt[mi]-1, dmg, r=prr(6);
    char b[4];
    int16_t r2=(int16_t)tatl()+(int16_t)r-(int16_t)MON_DEF[ti];
    dmg=(r2<1)?1:(uint8_t)r2;
    mhp[mi]=(mhp[mi]>dmg)?(mhp[mi]-dmg):0;
    { uint8_t mi2=0; const char*p="Atk!"; while(*p)mb[mi2++]=*p++; mb[mi2++]=' '; u8_to_s(dmg,b); { uint8_t di=0; while(b[di])mb[mi2++]=b[di++]; } mb[mi2]='d'; mi2++; mb[mi2]=0; }
    msg_set(mb); if(hw)sid_sword();else sid_hit();
    if (mhp[mi]==0) {
        md[mi]=1; xp+=MON_XP[ti];
        { uint8_t mi2=0; const char*p="+"; while(*p)mb[mi2++]=*p++; u8_to_s(MON_XP[ti],b); { uint8_t di=0; while(b[di])mb[mi2++]=b[di++]; } mb[mi2++]=0; }
        msg_set(mb);
        { uint16_t xn=(uint16_t)lvl*10; while (xp>=xn) { xp-=xn; lvl++; max_hp+=4; hp=max_hp; ba++; bd++; sid_lvlup(); { uint8_t mi2=0; const char*p="Nv!"; while(*p)mb[mi2++]=*p++; u8_to_s(lvl,b); { uint8_t di=0; while(b[di])mb[mi2++]=b[di++]; } mb[mi2]=0; } msg_set(mb); xn=(uint16_t)lvl*10; } }
        return 1;
    }
    return 0;
}

static uint8_t matk(uint8_t mi, uint8_t v2) {
    uint8_t ti=mt[mi]-1, dmg, r=prr(6);
    char b[4];
    int16_t r2=(int16_t)MON_ATK[ti]+(int16_t)r-(int16_t)tdef();
    dmg=(r2<1)?1:(uint8_t)r2;
    if (dmg>hp) dmg=hp; hp-=dmg;
    if (v2) { uint8_t mi2=0; const char*p="Dmg!"; while(*p)mb[mi2++]=*p++; u8_to_s(dmg,b); { uint8_t di=0; while(b[di])mb[mi2++]=b[di++]; } mb[mi2]=0; msg_set(mb); }
    sid_monster();
    return hp==0;
}

static void pickup(void) {
    uint8_t ii=iat(py,px), ti, r;
    if (!ii) { msg_set("Nada."); return; }
    ii--; ti=it[ii]; r=ti-1;
    iu[ii]=1;
    if (ti==10) { msg_set("Oro!"); sid_pickup(); return; }
    if (ti==11) { has_amulet=1; msg_set("Amuleto!"); sid_win(); return; }
    if (ti==1) { uint8_t h=10, oh=hp; hp+=h; if (hp>max_hp) hp=max_hp; msg_set("Curacion!"); sid_pickup(); return; }
    if (ti==2) { ba++; msg_set("Fuerza!"); sid_pickup(); return; }
    if (ti==3) { bd++; msg_set("Defensa!"); sid_pickup(); return; }
    if (ti==4) { uint8_t y,x; for (y=0;y<MAP_H;y++) for (x=0;x<MAP_W;x++) if (!vis[y][x]) vis[y][x]=1; msg_set("Mapeo!"); sid_pickup(); return; }
    if (ti==5) { for (r=0;r<100;r++) { uint8_t tx=prr(MAP_W), ty=prr(MAP_H); if (map[ty][tx]==T_FLOOR&&!mat(ty,tx)) { px=tx; py=ty; break; } } msg_set("TP!"); sid_pickup(); return; }
    if (ti==6) { if (!hw) { hw=1; msg_set("Espada!"); } else msg_set("Ya tienes espada."); sid_pickup(); return; }
    if (ti==7) { if (!ha) { ha=1; msg_set("Armadura!"); } else msg_set("Ya tienes armadura."); sid_pickup(); return; }
    if (ti==8) { if (!hr) { hr=1; msg_set("Anillo!"); } else msg_set("Ya tienes anillo."); sid_pickup(); return; }
    if (ti==9) { uint8_t oh=hp; hp+=5; if (hp>max_hp) hp=max_hp; msg_set("Comida!"); sid_pickup(); return; }
}

static void rcol(uint8_t c) {
    rom_uart_putc(0x1B); rom_uart_putc('[');
    if (c>=100) { rom_uart_putc('0'+(uint8_t)(c/100)); rom_uart_putc('0'+(uint8_t)((c/10)%10)); rom_uart_putc('0'+(uint8_t)(c%10)); }
    else if (c>=10) { rom_uart_putc('0'+(uint8_t)(c/10)); rom_uart_putc('0'+(uint8_t)(c%10)); }
    else rom_uart_putc('0'+c);
    rom_uart_putc('m');
}
static void rcolr(void) { rom_uart_puts("\033[0m"); }

static void rend(void) {
    uint8_t y,x,hc; char b[6];
    uart_gotoxy(0,0); rcolr();
    rom_uart_puts("\033[97;1m=== ROGUE ===");
    rcolr(); rom_uart_puts(" \033[37mNv:"); u8_to_s(lvl,b); rom_uart_puts(b);
    rom_uart_puts(" HP:"); hc=92; if (hp<=max_hp/4) hc=91; else if (hp<=max_hp/2) hc=93;
    rcol(hc); u8_to_s(hp,b); rom_uart_puts(b); rcolr();
    rom_uart_puts("/"); rcol(hc); u8_to_s(max_hp,b); rom_uart_puts(b); rcolr();
    rom_uart_puts("\033[37m F:"); u8_to_s(floor_n,b); rom_uart_puts(b); rom_uart_puts("/10");
    rom_uart_puts(" ATK:"); u8_to_s(tatl(),b); rom_uart_puts(b);
    rom_uart_puts(" DEF:"); u8_to_s(tdef(),b); rom_uart_puts(b);
    rom_uart_puts("\033[K");
    for (y=0;y<MAP_H;y++) {
        uint8_t lc=0;
        uart_gotoxy((uint8_t)(y+1),0);
        for (x=0;x<MAP_W;x++) {
            if (!vis[y][x]) { if (lc) { rcolr(); lc=0; } rom_uart_putc(' '); }
            else {
                uint8_t cc=0,ch;
                if (vis[y][x]==1) { ch=(map[y][x]==T_WALL)?'#':(map[y][x]==T_STAIRS)?'>':'.'; cc=90; }
                else {
                    if (x==px&&y==py) { cc=92; ch='@'; }
                    else { uint8_t mi=mat(y,x), ii=iat(y,x);
                        if (mi) { cc=91; ch=MON_CHARS[mt[mi-1]-1]; }
                        else if (ii) { uint8_t ti2=it[ii-1]; ch=ITEM_CHARS[ti2-1]; cc=(ti2<=3||ti2>=9)?93:(ti2>=4&&ti2<=5)?93:(ti2>=6&&ti2<=8)?96:95; }
                        else { ch=(map[y][x]==T_WALL)?'#':(map[y][x]==T_STAIRS)?'>':'.'; cc=90; }
                    }
                }
                if (cc!=lc) { if (lc) rcolr(); rcol(cc); lc=cc; }
                rom_uart_putc(ch);
            }
        }
        if (lc) rcolr(); rom_uart_puts("\033[K");
    }
    uart_gotoxy(11,0); rcolr(); rom_uart_puts("\033[90m+---------------------+\033[K");
    uart_gotoxy(12,0); rcolr(); rom_uart_puts("> "); rom_uart_puts(msg); rom_uart_puts("\033[K");
    uart_gotoxy(13,0); rcolr(); rom_uart_puts("\033[90mWASD:Mov Q:Tomar E:Eq G:Esc H:Ayu ESC:Salir\033[K");
    uart_gotoxy(14,0); rcolr(); rom_uart_puts("\033[K");
}

static uint8_t loop(void) {
    char k;
    uint8_t tu;
    while (1) {
        rend(); k=rom_uart_getc(); tu=0;
        switch (k) {
            case 'w': case 'W':
                if (py>0) {
                    uint8_t mi=mat(py-1,px);
                    if (mi) { patk(mi-1); tu=1; }
                    else if (map[py-1][px]!=T_WALL) { py--; tu=1; }
                } break;
            case 's': case 'S':
                if (py<MAP_H-1) {
                    uint8_t mi=mat(py+1,px);
                    if (mi) { patk(mi-1); tu=1; }
                    else if (map[py+1][px]!=T_WALL) { py++; tu=1; }
                } break;
            case 'a': case 'A':
                if (px>0) {
                    uint8_t mi=mat(py,px-1);
                    if (mi) { patk(mi-1); tu=1; }
                    else if (map[py][px-1]!=T_WALL) { px--; tu=1; }
                } break;
            case 'd': case 'D':
                if (px<MAP_W-1) {
                    uint8_t mi=mat(py,px+1);
                    if (mi) { patk(mi-1); tu=1; }
                    else if (map[py][px+1]!=T_WALL) { px++; tu=1; }
                } break;
            case 'q': case 'Q':
                pickup(); tu=1; break;
            case '>': case '.': case 'g': case 'G':
                if (map[py][px]==T_STAIRS) {
                    floor_n++;
                    if (floor_n>10) { floor_n=10; msg_set("No mas."); }
                    else { uint8_t mi2=0; const char*p="Piso "; char fb[4]; uint8_t di=0; while(*p)mb[mi2++]=*p++; u8_to_s(floor_n,fb); while(fb[di])mb[mi2++]=fb[di++]; mb[mi2]=0; msg_set(mb); sid_stairs(); gen_dun(floor_n); }
                } else msg_set("No escaleras.");
                break;
            case 'h': case 'H': case '?':
                show_help(); break;
            case 'e': case 'E':
                show_status(); break;
            case 0x1B:
                msg_set("ESC otra vez."); rend(); k=rom_uart_getc();
                if (k==0x1B) return 2;
                msg_set("Continua...");
                break;
            default: break;
        }
        if (tu) {
            if (!hp) { msg_set("Muerto!"); sid_die(); rend(); return 0; }
            mv_mons();
            if (!hp) { msg_set("Muerto!"); sid_die(); rend(); return 0; }
            fov();
            if (has_amulet && floor_n>=10) { msg_set("GANASTE! Amuleto!"); sid_win(); rend(); return 1; }
        }
    }
}

static void show_status(void) {
    uint8_t i=0;
    mb[0]=0;
    if(hw){mb[i++]='E'; mb[i++]='s'; mb[i++]='p'; mb[i++]=' ';}
    if(ha){mb[i++]='A'; mb[i++]='r'; mb[i++]='m'; mb[i++]=' ';}
    if(hr){mb[i++]='A'; mb[i++]='n'; mb[i++]='i'; mb[i++]=' ';}
    if(has_amulet){mb[i++]='Y'; mb[i++]='e'; mb[i++]='n'; mb[i++]='d'; mb[i++]='!'; mb[i++]=' ';}
    if(!i){mb[i++]='('; mb[i++]='-'; mb[i++]=')'; mb[i++]=' ';}
    mb[i]=0; msg_set(mb);
}

static void show_help(void) {
    rom_uart_puts("\033[2J== SIMBOLOS ==\r\n");
    rom_uart_puts("\033[97m@\033[0m=Jugador\r\n");
    rom_uart_puts("\033[91mMONSTRUOS: r=Rata g=Gob E=Esq s=Ser O=Orco F=Fan T=Trol D=Dragon\033[0m\r\n");
    rom_uart_puts("\033[93mITEMS: !=Poc ?=Perg )=Esp [=Arm ==Ani %=Com *=Oro \"=Amu\033[0m\r\n");
    rom_uart_puts("\033[90mTERRENO: #=Pared .=Suelo >=Esc\033[0m\r\n");
    rom_uart_puts("[Tecla]");
    rom_uart_getc(); rom_uart_puts("\033[2J");
}

static void init_game(void) {
    srand_c(42);
    hp=20; max_hp=20; ba=2; bd=1; hw=0; ha=0; hr=0; has_amulet=0;
    lvl=1; xp=0; floor_n=1;
    msg_clear(); sid_init(); gen_dun(1); msg_set("Bienvenido!");
}

int main(void) {
    uint8_t r;
    rom_uart_puts("\033[2J\033[H");
    rom_uart_puts("\033[32;1m=== ROGUE 6502 ===\033[0m\r\n");
    rom_uart_puts("WASD:Mov Q:Tomar >:Bajar\r\n");
    rom_uart_puts("Busca el Amuleto de Yendor!\r\n\r\n");
    rom_uart_getc();
    rom_uart_puts("\033[2J\033[H");
    init_game();
    r=loop();
    rom_uart_puts("\033[2J\033[H");
    if(r==1)rom_uart_puts("\033[33;1mVICTORIA!\033[0m\r\n");
    else if(r==0)rom_uart_puts("\033[31;1mGAME OVER\033[0m\r\n");
    rom_uart_puts("Gracias por jugar!\r\n");
    return 0;
}
