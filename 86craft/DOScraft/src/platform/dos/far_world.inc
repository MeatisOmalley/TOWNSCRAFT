/* Optional visual-only map. Base/edit/save offsets stay fixed. */
int dos_far_enabled;
u32 dos_far_version,dos_far_boundary_version;
u8 *dos_far_map;
int dos_far_compact;
static u32 farMapSector,farMapSum;
static int farRebuilding;

static void far_world_header(const u32 *h)
{
    farMapSector=0;
    dos_far_compact=h[23]==DOS_FAR_COMPACT_MAGIC;
    if(h[18]!=DOS_FAR_MAGIC) return;
    if(h[19]!=DOS_FAR_SECTOR || h[20]!=DOS_FAR_BYTES || h[22]!=1 ||
       hdd_sector_count()<DOS_FAR_SECTOR+DOS_FAR_BYTES/512)
        fatal("Invalid distant terrain appendix");
    farMapSector=h[19]; farMapSum=h[21];
}
int dos_far_available(void) { return farMapSector!=0; }
const u8 *dos_far_ready_flags(void) { return residentReady; }
void dos_far_change(int direction) {
    if(dos_far_available()) dos_far_enabled=(dos_far_enabled+direction+4)%4;
}
void dos_far_toggle(void) { dos_far_change(1); }

/* Compact media keeps actual leaves only in the trunk's 3x3 footprint.
 * Root heights are temporary edit/load scratch, never a rendering cache.
 * Missing streaming neighbors remain unknown; no frame-loop disk reads. */
static void far_roots(u8 roots[18*18],int cx,int cz,const u8 *blocks)
{
    memset(roots,0,18*18);
    for(int bz=MAX(0,cz-1);bz<=MIN(g_NC-1,cz+1);++bz)
    for(int bx=MAX(0,cx-1);bx<=MIN(g_NC-1,cx+1);++bx) {
        int x0=MAX(0,cx*CS-1-bx*CS),x1=MIN(15,cx*CS+16-bx*CS);
        int z0=MAX(0,cz*CS-1-bz*CS),z1=MIN(15,cz*CS+16-bz*CS);
        const u8 *p=NULL;int raw=1,id=bz*g_NC+bx,s=g_columnMap[id];
        if(bx==cx && bz==cz) p=blocks;
        else if(s>=0 && !farRebuilding) p=g_blocks+s*COLUMN_CELLS;
        else if(terrainBlocking) {
            if(!column_prepare(bx,bz)) fatal("Cannot read compact canopy roots");
            p=column_bytes(id);raw=columnRecords[id].raw;
        }
        if(!p || raw) {
            for(int z=z0;z<=z1;++z) for(int x=x0;x<=x1;++x) {
                int at=(bz*CS+z-cz*CS+1)*18+bx*CS+x-cx*CS+1,h=0;
                if(!p) h=255;
                else for(int y=WH-1;y>=0;--y) if(BLK_ID(p[(z*CS+x)*WH+y])==B_LOG) { h=y+1;break; }
                roots[at]=h;
            }
        } else {
            /* Read only root heights from a checked compressed record;
             * retain the caller's decoded column in columnEncodeScratch. */
            u32 pos=0;
            for(u32 i=0;i<columnRecords[id].len;i+=2) {
                u32 end=pos+p[i];
                if(BLK_ID(p[i+1])==B_LOG) for(u32 at=pos;at<end;) {
                    int cell=at/WH,x=cell&15,z=cell>>4;
                    u32 next=MIN(end,(cell+1)*WH);
                    if(x>=x0 && x<=x1 && z>=z0 && z<=z1)
                        roots[(bz*CS+z-cz*CS+1)*18+bx*CS+x-cx*CS+1]=next-cell*WH;
                    at=next;
                }
                pos=end;
            }
        }
    }
}
static int far_keep_leaf(const u8 *roots,int x,int y,int z)
{
    int unknown=0;
    for(int dz=0;dz<3;++dz) for(int dx=0;dx<3;++dx) {
        int h=roots[(z+dz)*18+x+dx];
        if(h==255) unknown=1;
        else if(h && y>=h-2 && y<h+3) return 1;
    }
    return unknown ? 2 : 0;
}
/* Top cube, not plants/torches/doors. Two-by-two cells never cross a column. */
static int far_world_cell(int cx,int cz,int lx,int lz,const u8 *blocks,const u8 *roots)
{
    int h=0,b=0,unknown=0;
    for(int dz=0;dz<2;++dz) for(int dx=0;dx<2;++dx) {
        const u8 *p=blocks+((lz+dz)*CS+lx+dx)*WH;
        for(int y=WH-1;y>=h;--y) if(blk_flags(p[y])&BF_CUBE) {
            if(roots && BLK_ID(p[y])==B_LEAVES) {
                int keep=far_keep_leaf(roots,lx+dx,y,lz+dz);
                if(keep!=1) { if(keep==2) unknown=MAX(unknown,y+1);continue; }
            }
            h=y+1; b=BLK_ID(p[y]); break;
        }
    }
    if(unknown>h) return 0; /* Keep the previously baked/edited edge sample. */
    int i=(((cz*CS+lz)>>1)*512+((cx*CS+lx)>>1))*2;
    if(dos_far_map[i]==h && BLK_ID(dos_far_map[i+1])==b) return 0;
    dos_far_map[i]=h; dos_far_map[i+1]=b;
    return 1;
}
#ifdef DOS_FAR_DETAILS
/* Reuse the three spare material bits in RAM: two slope shades and one
 * stable world-cell variation bit. Disk records remain raw block IDs. */
static void far_world_style(int x,int z)
{
    int i=(z*512+x)*2,h=dos_far_map[i],b=BLK_ID(dos_far_map[i+1]);
    int west=x ? dos_far_map[i-2] : h,north=z ? dos_far_map[i-1024] : h;
    int slope=CLAMP(1+((west+north-2*h)>>2),0,3);
    u32 noise=(u32)x*0x45d9f3bu^(u32)z*0x27d4eb2du;
    noise^=noise>>16;
    dos_far_map[i+1]=b|(slope<<5)|((noise&1)<<7);
}
static void far_world_style_rect(int x0,int z0,int x1,int z1)
{
    for(int z=MAX(0,z0);z<=MIN(511,z1);++z)
    for(int x=MAX(0,x0);x<=MIN(511,x1);++x) far_world_style(x,z);
}
#endif
static void far_world_area(int x0,int z0,int x1,int z1,int cx,int cz,const u8 *blocks)
{
    if(!dos_far_map) return;
    x0=MAX(0,x0);z0=MAX(0,z0);x1=MIN(g_W-1,x1);z1=MIN(g_W-1,z1);
    int changed=0;u8 roots[18*18];
    /* Process the supplied decoded column first; later disk restorations
     * may reuse that same scratch buffer during committed-save rebuilding. */
    for(int pass=0;pass<2;++pass)
    for(int bz=z0/CS;bz<=z1/CS;++bz) for(int bx=x0/CS;bx<=x1/CS;++bx) {
        int own=bx==cx && bz==cz;
        if(own!=(pass==0)) continue;
        const u8 *p=blocks;
        if(!own) {
            int s=g_columnMap[bz*g_NC+bx];
            if(s>=0 && !farRebuilding) p=g_blocks+s*COLUMN_CELLS;
            else {
                if(!terrainBlocking) continue;
                if(!column_prepare(bx,bz)) fatal("Cannot read compact canopy edge");
                column_restore_valid(bx,bz,columnEncodeScratch);p=columnEncodeScratch;
            }
        }
        int lx0=MAX(0,x0-bx*CS)&~1,lz0=MAX(0,z0-bz*CS)&~1;
        int lx1=MIN(15,x1-bx*CS)&~1,lz1=MIN(15,z1-bz*CS)&~1;
        int trim=0;
        if(dos_far_compact) {
            /* Ground/building edits often have no visible leaf candidate.
             * Avoid gathering a whole root halo when it cannot be used. */
            for(int z=lz0;z<=lz1+1 && !trim;++z) for(int x=lx0;x<=lx1+1 && !trim;++x)
                for(int y=WH-1;y>=0;--y) if(blk_flags(p[(z*CS+x)*WH+y])&BF_CUBE) {
                    trim=BLK_ID(p[(z*CS+x)*WH+y])==B_LEAVES;break;
                }
            if(trim) far_roots(roots,bx,bz,p);
        }
        for(int z=lz0;z<=lz1;z+=2) for(int x=lx0;x<=lx1;x+=2)
            changed|=far_world_cell(bx,bz,x,z,p,trim ? roots : NULL);
    }
    if(changed) {
#ifdef DOS_FAR_DETAILS
        far_world_style_rect((x0>>1)-1,(z0>>1)-1,(x1>>1)+1,(z1>>1)+1);
#endif
        ++dos_far_version;
    }
}
static void far_world_column(int cx,int cz,const u8 *blocks)
{
    int border=dos_far_compact ? 1 : 0;
    far_world_area(cx*CS-border,cz*CS-border,cx*CS+15+border,cz*CS+15+border,cx,cz,blocks);
}
static void far_world_edit(int x,int z)
{
    if(!dos_far_map) return;
    ++dos_far_boundary_version;
    int cx=x/CS,cz=z/CS,s=g_columnMap[cz*g_NC+cx];
    if(s<0) return;
    int border=dos_far_compact ? 1 : 0;
    far_world_area(x-border,z-border,x+border,z+border,cx,cz,g_blocks+s*COLUMN_CELLS);
}
static void far_world_reset(void)
{
    if(!farMapSector) return;
    if(!dos_far_map) dos_far_map=heap_alloc_high(DOS_FAR_BYTES);
    for(u32 s=0;s<DOS_FAR_BYTES/512;s+=24) {
        int n=MIN(24,DOS_FAR_BYTES/512-s);
        if(hdd_transfer_sync(farMapSector+s,n,0,dos_far_map+s*512)!=1)
            fatal("Cannot read distant terrain");
    }
    if(column_checksum(dos_far_map,DOS_FAR_BYTES)!=farMapSum)
        fatal("Corrupt distant terrain");
    for(u32 i=0;i<DOS_FAR_BYTES;i+=2)
        if(dos_far_map[i]>WH || dos_far_map[i+1]>=NUM_BLOCKS)
            fatal("Invalid distant terrain cell");
    /* Reconstruct summaries from the committed edit directory on load.
     * Only edited columns are read; none of this runs in the frame loop. */
    terrainBlocking=1;
    farRebuilding=1;
    for(int id=0;id<TERRAIN_COLUMNS;++id) {
        ColumnRecord *r=&columnRecords[id];
        if(r->off==large_column_offset(id,0)) continue;
        int cx=id%g_NC,cz=id/g_NC;
        if(!column_prepare(cx,cz)) fatal("Cannot read distant terrain edits");
        column_restore_valid(cx,cz,columnEncodeScratch);
        far_world_column(cx,cz,columnEncodeScratch);
    }
#ifdef DOS_FAR_DETAILS
    far_world_style_rect(0,0,511,511);
#endif
    farRebuilding=0;terrainBlocking=0; ++dos_far_version;
}
int dos_far_sample(int x,int z,int detailed,u8 *block)
{
    if(!dos_far_map || (unsigned)x>=(unsigned)g_W || (unsigned)z>=(unsigned)g_W) return 0;
    if(detailed) {
        int s=g_columnMap[(z/CS)*g_NC+x/CS];
        if(s>=0 && residentReady[s]) {
            int cell=(z&15)*CS+(x&15),h=g_height[s*CS*CS+cell];
            const u8 *p=g_blocks+s*COLUMN_CELLS+cell*WH;
            while(h && !(blk_flags(p[h-1])&BF_CUBE)) --h;
            if(dos_far_compact && h && BLK_ID(p[h-1])==B_LEAVES) {
                /* Preserve clipped coverage at the near/far boundary, using
                 * the same map sample rather than searching trunks per ray. */
                int i=((z>>1)*512+(x>>1))*2;
                if(BLK_ID(dos_far_map[i+1])==B_LEAVES) {
                    *block=B_LEAVES;return dos_far_map[i];
                }
                while(h && (!(blk_flags(p[h-1])&BF_CUBE) || BLK_ID(p[h-1])==B_LEAVES)) --h;
            }
            *block=h ? BLK_ID(p[h-1]) : B_AIR;
            return h;
        }
    }
    int i=((z>>1)*512+(x>>1))*2;
    *block=BLK_ID(dos_far_map[i+1]); return dos_far_map[i];
}
