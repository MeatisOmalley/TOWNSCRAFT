/* REJECTED, UNINTEGRATED RESEARCH CHECKPOINT: not safe for production.
 * See README.md for the incomplete publication/material proof and decision.
 * Bounded world-space enclosure proof. No projection or screen coverage.
 * Caller supplies READ(x,y,z): -1 unknown/unpublished, 0 passable, 1 full
 * opaque cube. Unknown, search boundary and queue overflow always fail open.
 * This does NOT prove visibility through portals/open doors. */
#ifndef DOS_ROOM_VISIBILITY_H
#define DOS_ROOM_VISIBILITY_H
#include <string.h>
#define DOS_ROOM_EDGE 16
#define DOS_ROOM_CELLS 4096
#define DOS_ROOM_BUDGET 512
typedef struct {
    unsigned char seen[DOS_ROOM_CELLS/8];
    unsigned short queue[DOS_ROOM_BUDGET];
    int origin[3],lo[3],hi[3],valid,cells,reads;
} DosRoomProof;

static int dos_room_prove(DosRoomProof *p,int x,int y,int z,
                         int (*read_cell)(int,int,int))
{
    static const int delta[6][3]={{-1,0,0},{1,0,0},{0,-1,0},{0,1,0},{0,0,-1},{0,0,1}};
    int head=0,tail=1,k;
    p->valid=p->cells=p->reads=0;
    p->origin[0]=x-8; p->origin[1]=y-8; p->origin[2]=z-8;
    memset(p->seen,0,sizeof(p->seen));
    ++p->reads;
    if(read_cell(x,y,z)!=0) return 0;
    p->queue[0]=8+(8<<4)+(8<<8);
    p->seen[p->queue[0]>>3]|=1<<(p->queue[0]&7);
    p->lo[0]=p->hi[0]=x; p->lo[1]=p->hi[1]=y; p->lo[2]=p->hi[2]=z;
    while(head<tail) {
        int id=p->queue[head++],local[3]={id&15,(id>>4)&15,id>>8};
        ++p->cells;
        for(k=0;k<6;++k) {
            int a=local[0]+delta[k][0],b=local[1]+delta[k][1],c=local[2]+delta[k][2];
            int next,wx,wy,wz,type;
            if((unsigned)a>=16 || (unsigned)b>=16 || (unsigned)c>=16) return 0;
            next=a+(b<<4)+(c<<8);
            if(p->seen[next>>3]&(1<<(next&7))) continue;
            p->seen[next>>3]|=1<<(next&7);
            wx=p->origin[0]+a; wy=p->origin[1]+b; wz=p->origin[2]+c;
            ++p->reads; type=read_cell(wx,wy,wz);
            if(type<0) return 0;
            if(type==1) continue;
            if(tail==DOS_ROOM_BUDGET) return 0;
            p->queue[tail++]=next;
            if(wx<p->lo[0]) p->lo[0]=wx; if(wx>p->hi[0]) p->hi[0]=wx;
            if(wy<p->lo[1]) p->lo[1]=wy; if(wy>p->hi[1]) p->hi[1]=wy;
            if(wz<p->lo[2]) p->lo[2]=wz; if(wz>p->hi[2]) p->hi[2]=wz;
        }
    }
    /* Preserve boundary cubes and touching/overlapping merged faces. */
    for(k=0;k<3;++k) { --p->lo[k]; p->hi[k]+=2; }
    p->valid=1;
    return 1;
}

static int dos_room_outside(const DosRoomProof *p,int x0,int x1,int y0,int y1,int z0,int z1)
{
    return p->valid && (x1<p->lo[0] || x0>p->hi[0] ||
        y1<p->lo[1] || y0>p->hi[1] || z1<p->lo[2] || z0>p->hi[2]);
}
#endif
