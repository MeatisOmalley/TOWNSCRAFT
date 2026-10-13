/* Optional visual-only map. Base/edit/save offsets stay fixed. */
int dos_far_enabled;
u32 dos_far_version,dos_far_boundary_version;
u8 *dos_far_map;
static u32 farMapSector,farMapSum;
FarTreeGroup *dos_far_tree_groups;
u8 *dos_far_under;
u32 *dos_far_tree_columns;
FarTree *dos_far_trees;
u32 dos_far_tree_count,dos_far_ground_version;
static u8 *farTreeData;
static u32 farTreeBytes,farTreeCapacity,farTreeSum,farUnderCount;

static void far_world_header(const u32 *h)
{
    farMapSector=0;
    farTreeBytes=0;
    if(h[18]!=DOS_FAR_MAGIC) return;
    if(h[19]!=DOS_FAR_SECTOR || h[20]!=DOS_FAR_BYTES || h[22]!=1 ||
       hdd_sector_count()<DOS_FAR_SECTOR+DOS_FAR_BYTES/512)
        fatal("Invalid distant terrain appendix");
    farMapSector=h[19]; farMapSum=h[21];
    if(h[23]==DOS_TREE_MAGIC) {
        if(h[27]>262144 || h[28]>1048576) fatal("Invalid natural tree count");
        u32 bytes=((65536+h[27]*2+3)&~3u)+4097*4+h[28]*8;
        if(h[24]!=DOS_TREE_SECTOR || h[27]>262144 || h[28]>1048576 ||
           h[25]!=((bytes+511)&~511u) || hdd_sector_count()<DOS_TREE_SECTOR+h[25]/512)
            fatal("Invalid natural tree appendix");
        farTreeBytes=h[25];farTreeSum=h[26];farUnderCount=h[27];dos_far_tree_count=h[28];
    }
}
int dos_far_available(void) { return farMapSector!=0; }
const u8 *dos_far_ready_flags(void) { return residentReady; }
void dos_far_change(int direction) {
    if(dos_far_available()) dos_far_enabled=(dos_far_enabled+direction+4)%4;
}
void dos_far_toggle(void) { dos_far_change(1); }

/* Top cube, not plants/torches/doors. Two-by-two cells never cross a column. */
static int far_world_cell(int cx,int cz,int lx,int lz,const u8 *blocks)
{
    int h=0,b=0,ground=0,material=0;
    int i=(((cz*CS+lz)>>1)*512+((cx*CS+lx)>>1))*2;
    int index=dos_far_under_index(i/2);
    for(int dz=0;dz<2;++dz) for(int dx=0;dx<2;++dx) {
        const u8 *p=blocks+((lz+dz)*CS+lx+dx)*WH;
        for(int y=WH-1;y>=h;--y) if(blk_flags(p[y])&BF_CUBE) {
            h=y+1; b=BLK_ID(p[y]); break;
        }
        if(index>=0) for(int y=WH-1;y>=ground;--y)
            if(BLK_ID(p[y])!=B_LEAVES && (blk_flags(p[y])&BF_CUBE)) {
                ground=y+1;material=BLK_ID(p[y]);break;
            }
    }
    int changed=0;
    if(index>=0 && (dos_far_under[index*2]!=ground || BLK_ID(dos_far_under[index*2+1])!=material)) {
        dos_far_under[index*2]=ground;dos_far_under[index*2+1]=material;changed|=2;
    }
    if(dos_far_map[i]!=h || BLK_ID(dos_far_map[i+1])!=b) {
        dos_far_map[i]=h;dos_far_map[i+1]=b;changed|=1;
    }
    return changed;
}
static void far_tree_update_column(int cx,int cz,const u8 *blocks)
{
    if(!dos_far_trees) return;
    int column=cz*64+cx,changed=0;
    for(u32 i=dos_far_tree_columns[column];i<dos_far_tree_columns[column+1];++i) {
        FarTree *t=&dos_far_trees[i];const u8 *p=blocks+((t->z&15)*16+(t->x&15))*WH;
        int active=BLK_ID(p[t->top])==B_LEAVES && BLK_ID(p[t->top+1])==B_LEAVES;
        for(int y=t->base;y<t->top && active;++y)
            active=BLK_ID(p[y])==B_LOG && !(p[y]&DOS_LOG_PLACED);
        if(t->active!=active) { t->active=active;changed=1; }
    }
    if(changed) ++dos_far_boundary_version;
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
    int index=dos_far_under_index(i/2);
    if(index>=0) {
        u8 material;h=dos_far_under[index*2];b=BLK_ID(dos_far_under[index*2+1]);
        west=x ? dos_far_ground_cell(i/2-1,&material) : h;
        north=z ? dos_far_ground_cell(i/2-512,&material) : h;
        slope=CLAMP(1+((west+north-2*h)>>2),0,3);
        dos_far_under[index*2+1]=b|(slope<<5)|((noise&1)<<7);
    }
}
static void far_world_style_rect(int x0,int z0,int x1,int z1)
{
    for(int z=MAX(0,z0);z<=MIN(511,z1);++z)
    for(int x=MAX(0,x0);x<=MIN(511,x1);++x) far_world_style(x,z);
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
        if(changed&1) ++dos_far_version;
        if(changed&2) ++dos_far_ground_version;
    }
    far_tree_update_column(cx,cz,blocks);
}
static void far_world_edit(int x,int z)
{
    if(!dos_far_map) return;
    ++dos_far_boundary_version;
    int cx=x/CS,cz=z/CS,s=g_columnMap[cz*g_NC+cx];
    if(s<0) return;
    int changed=far_world_cell(cx,cz,(x&15)&~1,(z&15)&~1,g_blocks+s*COLUMN_CELLS);
    if(changed) {
#ifdef DOS_FAR_DETAILS
        far_world_style_rect((x>>1)-1,(z>>1)-1,(x>>1)+1,(z>>1)+1);
#endif
        if(changed&1) ++dos_far_version;
        if(changed&2) ++dos_far_ground_version;
    }
    far_tree_update_column(cx,cz,g_blocks+s*COLUMN_CELLS);
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
    if(farTreeBytes) {
        if(!farTreeData) { farTreeData=heap_alloc_high(farTreeBytes);farTreeCapacity=farTreeBytes; }
        if(farTreeBytes>farTreeCapacity) fatal("Natural tree seed changed");
        for(u32 s=0;s<farTreeBytes/512;s+=24) {
            int n=MIN(24,farTreeBytes/512-s);
            if(hdd_transfer_sync(DOS_TREE_SECTOR+s,n,0,farTreeData+s*512)!=1)
                fatal("Cannot read natural trees");
        }
        if(column_checksum(farTreeData,farTreeBytes)!=farTreeSum) fatal("Corrupt natural trees");
        dos_far_tree_groups=(FarTreeGroup *)farTreeData;dos_far_under=farTreeData+65536;
        dos_far_tree_columns=(u32 *)(farTreeData+((65536+farUnderCount*2+3)&~3u));
        dos_far_trees=(FarTree *)(dos_far_tree_columns+4097);
        u32 total=0;
        for(int i=0;i<8192;++i) {
            if(dos_far_tree_groups[i].first!=total) fatal("Invalid natural tree index");
            total+=dos_far_popcount(dos_far_tree_groups[i].mask);
        }
        if(total!=farUnderCount || dos_far_tree_columns[0] || dos_far_tree_columns[4096]!=dos_far_tree_count)
            fatal("Invalid natural tree count");
        for(u32 i=0;i<farUnderCount;++i)
            if(dos_far_under[i*2]>WH || dos_far_under[i*2+1]>=NUM_BLOCKS || dos_far_under[i*2+1]==B_LEAVES)
                fatal("Invalid leaf-free surface");
        for(int c=0;c<4096;++c) {
            if(dos_far_tree_columns[c]>dos_far_tree_columns[c+1] || dos_far_tree_columns[c+1]>dos_far_tree_count)
                fatal("Invalid natural tree column");
            for(u32 i=dos_far_tree_columns[c];i<dos_far_tree_columns[c+1];++i) {
                FarTree *t=&dos_far_trees[i];
                if(t->x>=1024 || t->z>=1024 || (t->z/16)*64+t->x/16!=c || t->top+1>=WH ||
                   t->top<t->base+4 || t->top>t->base+6 || t->active!=1 || t->reserved)
                    fatal("Invalid natural tree record");
            }
        }
    }
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
    terrainBlocking=0; ++dos_far_version; ++dos_far_ground_version;
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
    *block=BLK_ID(dos_far_map[i+1]); return dos_far_map[i];
}
