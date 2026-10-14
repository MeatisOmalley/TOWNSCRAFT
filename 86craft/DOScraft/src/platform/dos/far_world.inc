/* Optional visual-only map. Base/edit/save offsets stay fixed. */
int dos_far_enabled;
u32 dos_far_version,dos_far_boundary_version;
u8 *dos_far_map;
static u32 farMapSector,farMapSum;
/* V2 media store the map as 1024 sectors of 16x16-cell tiles. */
static int farMapTiled;
/* RAM holds a dos_far_cells-square window of the 512x512 cell map at origin
 * (dos_far_x0,dos_far_z0), addressed toroidally so it can slide without
 * moving data (dos_far_index). 8 MiB and up hold the whole map at origin 0:
 * the original addresses and bounds. */
u32 dos_far_mask=511;
int dos_far_shift=9,dos_far_x0,dos_far_z0,dos_far_cells=512;
static u8 farRowBuffer[4*1024];

static void far_world_header(const u32 *h)
{
    farMapSector=0;
    if(h[18]!=DOS_FAR_MAGIC) return;
    farMapTiled=h[22]==2;
    if((!farMapTiled && h[19]!=DOS_FAR_SECTOR) || h[20]!=DOS_FAR_BYTES || (h[22]!=1 && h[22]!=2) ||
       !h[19] || hdd_sector_count()<h[19]+DOS_FAR_BYTES/512)
        fatal("Invalid distant terrain appendix");
    farMapSector=h[19]; farMapSum=h[21];
}
int dos_far_available(void) { return farMapSector!=0; }
const u8 *dos_far_ready_flags(void) { return residentReady; }
/* A 128-cell window reaches Medium (96 blocks plus the panorama's 4-block
 * rebuild distance); High needs the whole map. */
int dos_far_max_level(void)
{
#ifdef DOS_CPU386
    return DOS_FAR_LOW;
#else
    return dos_far_cells>=512 ? DOS_FAR_HIGH : DOS_FAR_MEDIUM;
#endif
}
void dos_far_change(int direction) {
    if(dos_far_available()) {
        int count=dos_far_max_level()+1;
        dos_far_enabled=(dos_far_enabled+direction+count)%count;
    }
}
void dos_far_toggle(void) { dos_far_change(1); }

/* Top cube, not plants/torches/doors. Two-by-two cells never cross a column. */
static int far_world_cell(int cx,int cz,int lx,int lz,const u8 *blocks)
{
    int h=0,b=0;
    for(int dz=0;dz<2;++dz) for(int dx=0;dx<2;++dx) {
        const u8 *p=blocks+((lz+dz)*CS+lx+dx)*WH;
        for(int y=WH-1;y>=h;--y) if(dos_far_cube(p[y])) {
            h=y+1; b=BLK_ID(p[y]); break;
        }
    }
    int mx=(cx*CS+lx)>>1,mz=(cz*CS+lz)>>1;
    if(!dos_far_in_window(mx,mz)) return 0; /* Reapplied when the window reaches it. */
    u32 i=dos_far_index(mx,mz);
    if(dos_far_map[i]==h && BLK_ID(dos_far_map[i+1])==b) return 0;
    dos_far_map[i]=h; dos_far_map[i+1]=b;
    return 1;
}
#ifdef DOS_FAR_DETAILS
/* Reuse the three spare material bits in RAM: two slope shades and one
 * stable world-cell variation bit. Disk records remain raw block IDs. */
static void far_world_style(int x,int z)
{
    u32 i=dos_far_index(x,z);
    int h=dos_far_map[i],b=BLK_ID(dos_far_map[i+1]);
    int west=dos_far_in_window(x-1,z) ? dos_far_map[dos_far_index(x-1,z)] : h;
    int north=dos_far_in_window(x,z-1) ? dos_far_map[dos_far_index(x,z-1)] : h;
    int slope=CLAMP(1+((west+north-2*h)>>2),0,3);
    u32 noise=(u32)x*0x45d9f3bu^(u32)z*0x27d4eb2du;
    noise^=noise>>16;
    dos_far_map[i+1]=b|(slope<<5)|((noise&1)<<7);
}
static void far_world_style_rect(int x0,int z0,int x1,int z1)
{
    for(int z=MAX(dos_far_z0,z0);z<=MIN(dos_far_z0+dos_far_cells-1,z1);++z)
    for(int x=MAX(dos_far_x0,x0);x<=MIN(dos_far_x0+dos_far_cells-1,x1);++x) far_world_style(x,z);
}
#endif
static void far_world_column(int cx,int cz,const u8 *blocks)
{
    if(!dos_far_map) return;
    int changed=0;
    for(int z=0;z<CS;z+=2) for(int x=0;x<CS;x+=2)
        changed|=far_world_cell(cx,cz,x,z,blocks);
    if(changed) {
#ifdef DOS_FAR_DETAILS
        far_world_style_rect(cx*8-1,cz*8-1,cx*8+8,cz*8+8);
#endif
        ++dos_far_version;
    }
}
static void far_world_edit(int x,int z)
{
    if(!dos_far_map) return;
    ++dos_far_boundary_version;
    int cx=x/CS,cz=z/CS,s=g_columnMap[cz*g_NC+cx];
    if(s<0) return;
    if(far_world_cell(cx,cz,(x&15)&~1,(z&15)&~1,g_blocks+s*COLUMN_CELLS)) {
#ifdef DOS_FAR_DETAILS
        far_world_style_rect((x>>1)-1,(z>>1)-1,(x>>1)+1,(z>>1)+1);
#endif
        ++dos_far_version;
    }
}
/* Edited columns overlapping cells [x0,x1)x[z0,z1) over the base map.
 * Resident copies are used only while playing (farUseResident): on a world
 * load the cache still holds the previous session's columns. */
static int farUseResident;
static void far_world_apply_edits(int x0,int z0,int x1,int z1)
{
    terrainBlocking=1;
    for(int cz=z0/8;cz<=(z1-1)/8;++cz)
    for(int cx=x0/8;cx<=(x1-1)/8;++cx) {
        int id=cz*g_NC+cx,s=g_columnMap[id];
        if(columnRecords[id].off==large_column_offset(id,0)) continue;
        if(farUseResident && s>=0 && residentReady[s]) { far_world_column(cx,cz,g_blocks+s*COLUMN_CELLS); continue; }
        if(!column_prepare(cx,cz)) fatal("Cannot read distant terrain edits");
        column_restore_valid(cx,cz,columnEncodeScratch);
        far_world_column(cx,cz,columnEncodeScratch);
    }
    terrainBlocking=0;
}
/* Base cells [x0,x1)x[z0,z1) into the window; rows are 1 KiB, 4 per read. */
static void far_cell_store(int x,int z,const u8 *cell)
{
    if(cell[0]>WH || cell[1]>=NUM_BLOCKS) fatal("Invalid distant terrain cell");
    u32 i=dos_far_index(x,z);
    dos_far_map[i]=cell[0]; dos_far_map[i+1]=cell[1];
}
static void far_window_load(int x0,int z0,int x1,int z1)
{
    if(x0>=x1 || z0>=z1) return;
    if(farMapTiled) {
        /* One tile row at a time, up to 8 adjacent tiles per read. */
        for(int tz=z0>>4;tz<=(z1-1)>>4;++tz)
        for(int tx=x0>>4;tx<=(x1-1)>>4;tx+=8) {
            int n=MIN(8,((x1-1)>>4)-tx+1);
            if(hdd_transfer_sync(farMapSector+tz*32+tx,n,0,farRowBuffer)!=1)
                fatal("Cannot read distant terrain");
            for(int k=0;k<n;++k)
            for(int z=MAX(z0,tz*16);z<MIN(z1,tz*16+16);++z)
            for(int x=MAX(x0,(tx+k)*16);x<MIN(x1,(tx+k)*16+16);++x)
                far_cell_store(x,z,farRowBuffer+k*512+((z&15)*16+(x&15))*2);
        }
    } else
    for(int z=z0;z<z1;z+=4) {
        int rows=MIN(4,z1-z);
        if(hdd_transfer_sync(farMapSector+z*2,rows*2,0,farRowBuffer)!=1)
            fatal("Cannot read distant terrain");
        for(int r=0;r<rows;++r) for(int x=x0;x<x1;++x)
            far_cell_store(x,z+r,farRowBuffer+r*1024+x*2);
    }
    far_world_apply_edits(x0,z0,x1,z1);
#ifdef DOS_FAR_DETAILS
    far_world_style_rect(x0,z0,x1,z1);
#endif
}
/* Window origin, 16-cell aligned, roughly centred on cell (cx,cz). */
static int far_window_origin(int c)
{
    return CLAMP((c-dos_far_cells/2+8)&~15,0,512-dos_far_cells);
}
/* Keep the window around the camera (block coordinates). Slides once the
 * camera is 24 cells off centre: the 36-cell Low reach plus 24 stays inside
 * the 64-cell half window (Medium: 50+12). Waits while terrain I/O is busy. */
void dos_far_follow(int x,int z)
{
    if(!dos_far_map || dos_far_cells>=512) return;
    int cx=x>>1,cz=z>>1,half=dos_far_cells/2;
    int slack=dos_far_enabled>=DOS_FAR_MEDIUM ? 12 : 24;
    int nx=dos_far_x0,nz=dos_far_z0;
    if(ABS(cx-(nx+half))>slack) nx=far_window_origin(cx);
    if(ABS(cz-(nz+half))>slack) nz=far_window_origin(cz);
    if(nx==dos_far_x0 && nz==dos_far_z0) return;
    if(terrainReadID>=0 || terrainWriteID>=0) return;
    int ox=dos_far_x0,oz=dos_far_z0,w=dos_far_cells;
    dos_far_x0=nx; dos_far_z0=nz;
    farUseResident=1;
    if(ABS(nx-ox)>=w || ABS(nz-oz)>=w) far_window_load(nx,nz,nx+w,nz+w);
    else {
        /* Rows that entered, across the new width; then the entering columns. */
        int rz0=nz<oz ? nz : oz+w,rz1=nz<oz ? oz : nz+w;
        if(nz!=oz) far_window_load(nx,rz0,nx+w,rz1);
        int kz0=MAX(nz,oz),kz1=MIN(nz,oz)+w;
        int cx0=nx<ox ? nx : ox+w,cx1=nx<ox ? ox : nx+w;
        if(nx!=ox) far_window_load(cx0,kz0,cx1,kz1);
    }
    farUseResident=0;
    ++dos_far_version; ++dos_far_boundary_version;
}
static void far_world_reset(void)
{
    if(!farMapSector) return;
    if(!dos_far_map) {
        /* Below 8 MiB, a 128-cell window (32 KiB) instead of 512 KiB. */
        dos_far_cells=g_ramMB>=8 ? 512 : 128;
        dos_far_shift=dos_far_cells==512 ? 9 : 7;
        dos_far_mask=dos_far_cells-1;
        dos_far_map=heap_alloc_high((u32)dos_far_cells*dos_far_cells*2);
    }
    if(dos_far_enabled>dos_far_max_level()) dos_far_enabled=dos_far_max_level();
    if(dos_far_cells<512 || farMapTiled) {
        /* Check the whole on-disk map once, then load the window (or, for a
         * tiled whole map, every tile through the same loader). */
        u32 sum=2166136261u; /* column_checksum, streamed */
        for(u32 s=0;s<DOS_FAR_BYTES/512;s+=8) {
            if(hdd_transfer_sync(farMapSector+s,8,0,farRowBuffer)!=1)
                fatal("Cannot read distant terrain");
            for(u32 n=0;n<4096;++n) sum=(sum^farRowBuffer[n])*16777619u;
        }
        if(sum!=farMapSum) fatal("Corrupt distant terrain");
        dos_far_x0=dos_far_cells<512 ? far_window_origin(g_spawnX>>1) : 0;
        dos_far_z0=dos_far_cells<512 ? far_window_origin(g_spawnZ>>1) : 0;
        far_window_load(dos_far_x0,dos_far_z0,dos_far_x0+dos_far_cells,dos_far_z0+dos_far_cells);
        ++dos_far_version;
        return;
    }
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
#ifdef DOS_FAR_DETAILS
    far_world_style_rect(0,0,511,511);
#endif
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
            while(h && !dos_far_cube(p[h-1])) --h;
            *block=h ? BLK_ID(p[h-1]) : B_AIR;
            return h;
        }
    }
    if(!dos_far_in_window(x>>1,z>>1)) return 0;
    u32 i=dos_far_index(x>>1,z>>1);
    *block=BLK_ID(dos_far_map[i+1]); return dos_far_map[i];
}
