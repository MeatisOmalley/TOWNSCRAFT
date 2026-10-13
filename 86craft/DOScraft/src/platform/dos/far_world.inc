/* Optional visual-only map. Base/edit/save offsets stay fixed. */
int dos_far_enabled;
u32 dos_far_version;
u8 *dos_far_map;
static u32 farMapSector,farMapSum;

static void far_world_header(const u32 *h)
{
    farMapSector=0;
    if(h[18]!=DOS_FAR_MAGIC) return;
    if(h[19]!=DOS_FAR_SECTOR || h[20]!=DOS_FAR_BYTES || h[22]!=1 ||
       hdd_sector_count()<DOS_FAR_SECTOR+DOS_FAR_BYTES/512)
        fatal("Invalid distant terrain appendix");
    farMapSector=h[19]; farMapSum=h[21];
}
int dos_far_available(void) { return farMapSector!=0; }
void dos_far_change(int direction) {
    if(dos_far_available()) dos_far_enabled=(dos_far_enabled+direction+4)%4;
}
void dos_far_toggle(void) { dos_far_change(1); }

/* Top cube, not plants/torches/doors. Two-by-two cells never cross a column. */
static void far_world_cell(int cx,int cz,int lx,int lz,const u8 *blocks)
{
    int h=0,b=0;
    for(int dz=0;dz<2;++dz) for(int dx=0;dx<2;++dx) {
        const u8 *p=blocks+((lz+dz)*CS+lx+dx)*WH;
        for(int y=WH-1;y>=h;--y) if(blk_flags(p[y])&BF_CUBE) {
            h=y+1; b=BLK_ID(p[y]); break;
        }
    }
    int i=(((cz*CS+lz)>>1)*512+((cx*CS+lx)>>1))*2;
    dos_far_map[i]=h; dos_far_map[i+1]=b;
}
static void far_world_column(int cx,int cz,const u8 *blocks)
{
    if(!dos_far_map) return;
    for(int z=0;z<CS;z+=2) for(int x=0;x<CS;x+=2)
        far_world_cell(cx,cz,x,z,blocks);
    ++dos_far_version;
}
static void far_world_edit(int x,int z)
{
    if(!dos_far_map) return;
    int cx=x/CS,cz=z/CS,s=g_columnMap[cz*g_NC+cx];
    if(s<0) return;
    far_world_cell(cx,cz,(x&15)&~1,(z&15)&~1,g_blocks+s*COLUMN_CELLS);
    ++dos_far_version;
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
    for(int id=0;id<TERRAIN_COLUMNS;++id) {
        ColumnRecord *r=&columnRecords[id];
        if(r->off==large_column_offset(id,0)) continue;
        int cx=id%g_NC,cz=id/g_NC;
        if(!column_prepare(cx,cz)) fatal("Cannot read distant terrain edits");
        column_restore_valid(cx,cz,columnEncodeScratch);
        far_world_column(cx,cz,columnEncodeScratch);
    }
    terrainBlocking=0; ++dos_far_version;
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
            *block=h ? BLK_ID(p[h-1]) : B_AIR;
            return h;
        }
    }
    int i=((z>>1)*512+(x>>1))*2;
    *block=dos_far_map[i+1]; return dos_far_map[i];
}
