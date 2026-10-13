/* A 3x3 edit revision neighborhood covers the maximum 14-block horizontal
 * light influence. Cache only settled columns; reconcile seams on restore. */
static void stream_light_header(const u32 *h)
{
    streamLightBaked=0;
    if(h[13]!=STREAM_LIGHT_MAGIC) return;
    u32 indexSum=h[16];
    if(h[14]!=STREAM_LIGHT_BASE || h[15]!=13 || h[17]!=6400 ||
       hdd_sector_count()<STREAM_LIGHT_BASE+4096u*13)
        fatal("Invalid prebaked lighting medium");
    for(int s=0;s<32;s+=24) {
        int n=MIN(24,32-s);
        if(hdd_transfer_sync(STREAM_LIGHT_BASE-32+s,n,0,columnScratch)!=1)
            fatal("Cannot read prebaked lighting index");
        memcpy((u8 *)streamLightSum+s*512,columnScratch,n*512);
    }
    if(column_checksum((u8 *)streamLightSum,sizeof(streamLightSum))!=indexSum)
        fatal("Corrupt prebaked lighting index");
    if(!streamLightScratch) streamLightScratch=heap_alloc_high(13*512);
    streamLightBaked=1;
}
static void stream_light_allocate(void)
{
    if(terrainActive && g_ramMB>=8 && !streamLightCache)
        streamLightCache=heap_alloc_high(STREAM_LIGHT_CACHE*(COLUMN_CELLS+CS*CS));
}
static void stream_light_reset(void)
{
    streamLightRead=streamLightPrepared=-1;
    memset(streamLightTags,0xff,sizeof(streamLightTags));
    memset(streamLightVersion,0,sizeof(streamLightVersion));
    memset(streamLightDirty,0,sizeof(streamLightDirty));
    memset(streamLightAge,0,sizeof(streamLightAge));
    memset(g_streamLightStats,0,sizeof(g_streamLightStats)); streamLightClock=0;
}
static void stream_light_invalidate(int x,int z)
{
    int cx=x/CS,cz=z/CS;
    for(int nz=MAX(0,cz-1);nz<=MIN(g_NC-1,cz+1);++nz)
    for(int nx=MAX(0,cx-1);nx<=MIN(g_NC-1,cx+1);++nx) {
        int id=nz*g_NC+nx;
        ++streamLightVersion[id]; streamLightDirty[id]=1;
    }
    ++g_streamLightStats[3];
}
static int stream_light_quiesce(void)
{
    if(streamLightRead<0) return 1;
    int id=streamLightRead;
    int status=terrainBlocking ? hdd_transfer_sync(STREAM_LIGHT_BASE+id*13,13,0,streamLightScratch) :
                                hdd_transfer(STREAM_LIGHT_BASE+id*13,13,0,streamLightScratch);
    if(!status) return 0;
    streamLightRead=-1;
    if(status<0 || column_checksum(streamLightScratch,6400)!=streamLightSum[id]) {
        streamLightPrepared=-1; streamError=1; return -1;
    }
    for(int i=0;i<CS*CS;++i) if(streamLightScratch[i]>WH) { streamError=1; return -1; }
    streamLightPrepared=id; streamLightKind=1; return 1;
}
static int stream_light_prepare(int cx,int cz)
{
    int id=cz*g_NC+cx,status=stream_light_quiesce();
    if(status<=0) return 0;
    if(streamLightCache) for(int i=0;i<STREAM_LIGHT_CACHE;++i)
        if(streamLightTags[i]==id && streamLightRevision[i]==streamLightVersion[id]) {
            streamLightPrepared=id; streamLightKind=2; streamLightSlot=i; return 1;
        }
    if(!streamLightBaked || streamLightDirty[id]) { streamLightPrepared=-1; return 1; }
    if(streamLightPrepared==id && streamLightKind==1) return 1;
    streamLightRead=id; return stream_light_quiesce()>0;
}
static int stream_light_restore(int cx,int cz,int slot)
{
    int id=cz*g_NC+cx;
    if(streamLightPrepared!=id) { ++g_streamLightStats[2]; return 0; }
    if(streamLightKind==2) {
        int c=streamLightSlot;
        if(streamLightTags[c]!=id || streamLightRevision[c]!=streamLightVersion[id]) {
            ++g_streamLightStats[2]; return 0;
        }
        u8 *p=streamLightCache+c*(COLUMN_CELLS+CS*CS);
        memcpy(g_light+slot*COLUMN_CELLS,p,COLUMN_CELLS);
        memcpy(g_height+slot*CS*CS,p+COLUMN_CELLS,CS*CS);
        streamLightAge[c]=++streamLightClock; ++g_streamLightStats[1]; return 1;
    }
    if(streamLightDirty[id]) { ++g_streamLightStats[2]; return 0; }
    memcpy(g_height+slot*CS*CS,streamLightScratch,CS*CS);
    const u8 *p=streamLightScratch+CS*CS;
    u8 *dst=g_light+slot*COLUMN_CELLS;
    for(int i=0;i<COLUMN_CELLS/2;++i) { dst[i*2]=p[i]<<4; dst[i*2+1]=p[i]&0xf0; }
    /* Keep pristine light in the same bounded cache too. Otherwise a terrain
     * RAM hit on backtracking still makes a separate disk read for light.
     * Prefer replacing pristine entries so edited light is not crowded out. */
    if(streamLightCache) {
        int c=-1;
        for(int i=0;i<STREAM_LIGHT_CACHE;++i) {
            int tag=streamLightTags[i];
            if(tag==id || tag<0) { c=i; break; }
            if(!streamLightDirty[tag] && (c<0 || streamLightAge[i]<streamLightAge[c])) c=i;
        }
        if(c>=0) {
            u8 *out=streamLightCache+c*(COLUMN_CELLS+CS*CS);
            memcpy(out,dst,COLUMN_CELLS); memcpy(out+COLUMN_CELLS,g_height+slot*CS*CS,CS*CS);
            streamLightTags[c]=id; streamLightRevision[c]=streamLightVersion[id]; streamLightAge[c]=++streamLightClock;
        }
    }
    ++g_streamLightStats[0]; return 1;
}
static void stream_light_store_slot(int s)
{
    if(!streamLightCache || cacheLightSlot>=0 || pqHead!=pqTail || pendingLightEditCount) return;
        int cx=g_columnCoords[s*2],cz=g_columnCoords[s*2+1];
        if(cx<0 || !residentReady[s]) return;
        int id=cz*g_NC+cx;
        if(streamLightBaked && !streamLightDirty[id]) return;
        int c=0,found=0;
        for(int i=0;i<STREAM_LIGHT_CACHE;++i) {
            if(streamLightTags[i]==id) { c=i; found=streamLightRevision[i]==streamLightVersion[id]; break; }
            if(streamLightTags[i]<0 || streamLightAge[i]<streamLightAge[c]) c=i;
        }
        if(found) return;
        /* Missing neighbors can leave a border underlit. That is a safe
         * lower bound: restoring seeds both sides of the seams and floods
         * arrivals. Any geometry edit that could lower light changes this
         * column's revision, so stale bright light cannot be restored. */
        u8 *p=streamLightCache+c*(COLUMN_CELLS+CS*CS);
        memcpy(p,g_light+s*COLUMN_CELLS,COLUMN_CELLS);
        memcpy(p+COLUMN_CELLS,g_height+s*CS*CS,CS*CS);
        streamLightTags[c]=id; streamLightRevision[c]=streamLightVersion[id]; streamLightAge[c]=++streamLightClock;
}
static void stream_light_store_ready(void)
{ for(int s=0;s<g_residentColumns;++s) stream_light_store_slot(s); }

static int stream_light_clean_seams(int cx,int cz)
{
    if(!streamLightBaked) return 0;
    for(int z=MAX(0,cz-1);z<=MIN(g_NC-1,cz+1);++z)
    for(int x=MAX(0,cx-1);x<=MIN(g_NC-1,cx+1);++x)
        if(streamLightDirty[z*g_NC+x]) return 0;
    return 1;
}

/* Scripted acceptance only: invalidate both sides of a seam, evict cold,
 * and verify no removed torch or opened skylight is resurrected from cache. */
int world_verify_stream_light(void)
{
    int x=(g_spawnX/CS)*CS+15,z=(g_spawnZ/CS)*CS+8,y=WH-2;
    if(!streamLightBaked || x+1>=g_W) return 0;
    terrainBlocking=1; cache_stream(g_spawnX,g_spawnZ,100000);
    u8 old=wget(x,y,z);
    world_set(x,y,z,B_TORCH);
    if((world_light_at(x+1,y,z)&15)!=13) return 0;
    stream_light_store_ready();
    u32 hits=g_streamLightStats[1];
    if(!world_verify_large_travel(g_spawnX,g_spawnZ) ||
       (world_light_at(x+1,y,z)&15)!=13 || g_streamLightStats[1]<=hits) return 0;
    world_set(x,y,z,B_AIR);
    if((world_light_at(x+1,y,z)&15)!=0) return 0;
    if(!world_verify_large_travel(g_spawnX,g_spawnZ) || (world_light_at(x+1,y,z)&15)!=0) return 0;
    world_set(x,y,z,B_STONE);
    if((world_light_at(x,y-1,z)>>4)>=15) return 0;
    if(!world_verify_large_travel(g_spawnX,g_spawnZ) || (world_light_at(x,y-1,z)>>4)>=15) return 0;
    world_set(x,y,z,old);
    if(!world_verify_large_travel(g_spawnX,g_spawnZ) ||
       (world_light_at(x,y-1,z)>>4)!=15) return 0;
    return !streamError && g_streamLightStats[0]>0;
}
