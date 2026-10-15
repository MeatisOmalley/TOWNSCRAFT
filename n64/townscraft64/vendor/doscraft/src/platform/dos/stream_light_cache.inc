/* Restore the bake only where the column and its neighbors are untouched.
 * Edited neighborhoods retain the original initialization/flood-fill path. */
static void stream_light_header(const u32 *h)
{
    streamLightBaked=streamLightCompact=0;
    if(h[13]!=STREAM_LIGHT_MAGIC) return;
    u32 indexSum=h[16];
    if(h[15]==0) {
        /* V2: index of u32 byte offset, length and sum per column at h[32]. */
        u32 sum=2166136261u,region=h[14],end=h[33]*512u;
        if(h[17]!=STREAM_LIGHT_BYTES || h[32]!=97 || region<h[30] || region>=h[33])
            fatal("Invalid prebaked lighting medium");
        for(int s=0;s<96;s+=24) {
            if(hdd_transfer_sync(h[32]+s,24,0,columnScratch)!=1)
                fatal("Cannot read prebaked lighting index");
            for(u32 i=0;i<24*512;++i) sum=(sum^columnScratch[i])*16777619u;
            for(int e=0;e<1024;++e) {
                const u32 *entry=(const u32 *)columnScratch+e*3;
                int id=s/24*1024+e;
                if(entry[0]%512 || entry[0]/512<region || !entry[1] ||
                   entry[1]>STREAM_LIGHT_BYTES || entry[0]+entry[1]>end)
                    fatal("Corrupt prebaked lighting index");
                streamLightOff[id]=entry[0]; streamLightLen[id]=entry[1]; streamLightSum[id]=entry[2];
            }
        }
        if(sum!=indexSum) fatal("Corrupt prebaked lighting index");
        if(!streamLightScratch) streamLightScratch=heap_alloc_high(13*512);
        if(!streamLightPacked) streamLightPacked=heap_alloc_high(13*512);
        streamLightCompact=streamLightBaked=1;
        return;
    }
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
/* V2 stored record to the 6400-byte light layout; 0 if malformed. */
static int stream_light_unpack(int id)
{
    u32 len=streamLightLen[id],n=0;
    const u8 *p=streamLightPacked;
    if(len==STREAM_LIGHT_BYTES) { memcpy(streamLightScratch,p,len); return 1; }
    if(len&1) return 0;
    for(u32 i=0;i<len;i+=2) {
        if(!p[i] || n+p[i]>STREAM_LIGHT_BYTES) return 0;
        memset(streamLightScratch+n,p[i+1],p[i]); n+=p[i];
    }
    return n==STREAM_LIGHT_BYTES;
}
static int stream_light_quiesce(void)
{
    if(streamLightRead<0) return 1;
    int id=streamLightRead,status;
    if(streamLightCompact) {
        u32 lba=streamLightOff[id]/512,count=(streamLightLen[id]+511)/512;
        status=terrainBlocking ? hdd_transfer_sync(lba,count,0,streamLightPacked) :
                                 hdd_transfer(lba,count,0,streamLightPacked);
    } else
        status=terrainBlocking ? hdd_transfer_sync(STREAM_LIGHT_BASE+id*13,13,0,streamLightScratch) :
                                 hdd_transfer(STREAM_LIGHT_BASE+id*13,13,0,streamLightScratch);
    if(!status) return 0;
    streamLightRead=-1;
    if(status<0 || (streamLightCompact ?
           column_checksum(streamLightPacked,streamLightLen[id])!=streamLightSum[id] || !stream_light_unpack(id) :
           column_checksum(streamLightScratch,6400)!=streamLightSum[id])) {
        streamLightPrepared=-1; streamError=1; return -1;
    }
    for(int i=0;i<CS*CS;++i) if(streamLightScratch[i]>WH) { streamError=1; return -1; }
    streamLightPrepared=id; streamLightKind=1; return 1;
}
static int stream_light_prepare(int cx,int cz)
{
    int id=cz*g_NC+cx,status=stream_light_quiesce();
    if(status<=0) return 0;
    /* Conservative admission avoids the costly edited-cache seam pass.
     * The 3x3 dirty mask already covers light reach; checking its neighbors
     * also keeps the original path on both sides of an edited seam. */
    if(!streamLightBaked || !stream_light_clean_seams(cx,cz)) {
        streamLightPrepared=-1; return 1;
    }
    if(streamLightCache) for(int i=0;i<STREAM_LIGHT_CACHE;++i)
        if(streamLightTags[i]==id && streamLightRevision[i]==streamLightVersion[id]) {
            streamLightPrepared=id; streamLightKind=2; streamLightSlot=i; return 1;
        }
    if(streamLightPrepared==id && streamLightKind==1) return 1;
    streamLightRead=id; return stream_light_quiesce()>0;
}
static int stream_light_restore(int cx,int cz,int slot)
{
    int id=cz*g_NC+cx;
    if(streamLightPrepared!=id || !stream_light_clean_seams(cx,cz)) {
        ++g_streamLightStats[2]; return 0;
    }
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
    /* Keep pristine light in RAM for backtracking. Edited tags are invalid
     * and can be replaced immediately; computed edited light is not cached. */
    if(streamLightCache) {
        int c=-1;
        for(int i=0;i<STREAM_LIGHT_CACHE;++i) {
            int tag=streamLightTags[i];
            if(tag==id || tag<0 || streamLightDirty[tag]) { c=i; break; }
            if(c<0 || streamLightAge[i]<streamLightAge[c]) c=i;
        }
        if(c>=0) {
            u8 *out=streamLightCache+c*(COLUMN_CELLS+CS*CS);
            memcpy(out,dst,COLUMN_CELLS); memcpy(out+COLUMN_CELLS,g_height+slot*CS*CS,CS*CS);
            streamLightTags[c]=id; streamLightRevision[c]=streamLightVersion[id]; streamLightAge[c]=++streamLightClock;
        }
    }
    ++g_streamLightStats[0]; return 1;
}
static int stream_light_clean_seams(int cx,int cz)
{
    if(!streamLightBaked) return 0;
    for(int z=MAX(0,cz-1);z<=MIN(g_NC-1,cz+1);++z)
    for(int x=MAX(0,cx-1);x<=MIN(g_NC-1,cx+1);++x)
        if(streamLightDirty[z*g_NC+x]) return 0;
    return 1;
}

/* Scripted acceptance: exercise normal lighting on cold loads without mob
 * pins retaining the test's outgoing columns. No production caller. */
static int stream_light_verify_travel(int x,int z)
{
    int (*pin)(int,int)=columnPin;
    int (*needed)(int *,int *)=columnNeeded;
    columnPin=NULL; columnNeeded=NULL;
    cache_stream(8,8,100000);
    int near=world_column_sim_ready(0,0);
    cache_stream(g_W-8,g_W-8,100000);
    int far=world_column_sim_ready(g_NC-1,g_NC-1);
    cache_stream(x,z,100000);
    int back=world_column_sim_ready(x/CS,z/CS);
    columnPin=pin; columnNeeded=needed; terrainBlocking=0;
    return near && far && back && !streamError;
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
    u32 recomputations=g_streamLightStats[2];
    if(!stream_light_verify_travel(g_spawnX,g_spawnZ) ||
       (world_light_at(x+1,y,z)&15)!=13 || g_streamLightStats[2]<=recomputations) return 0;
    world_set(x,y,z,B_AIR);
    if((world_light_at(x+1,y,z)&15)!=0) return 0;
    if(!stream_light_verify_travel(g_spawnX,g_spawnZ) || (world_light_at(x+1,y,z)&15)!=0) return 0;
    world_set(x,y,z,B_STONE);
    if((world_light_at(x,y-1,z)>>4)>=15) return 0;
    if(!stream_light_verify_travel(g_spawnX,g_spawnZ) || (world_light_at(x,y-1,z)>>4)>=15) return 0;
    world_set(x,y,z,old);
    if(!stream_light_verify_travel(g_spawnX,g_spawnZ) ||
       (world_light_at(x,y-1,z)>>4)!=15) return 0;
    return !streamError && g_streamLightStats[0]>0;
}
