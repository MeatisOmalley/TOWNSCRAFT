/* Cached mesh light must catch up after an edit made during stream lighting,
   without requiring a second edit. Exercise both source changes and seams. */
#define main inherited_edit_main
#include "edit_test.c"
#undef main

static const int offsets[6][3]={{-1,0,0},{1,0,0},{0,-1,0},{0,1,0},{0,0,-1},{0,0,1}};

static int face_light(int x,int y,int z,int d)
{
    Chunk *c=&g_chunks[chunk_index(x/CS,y/CS,z/CS)];
    for(int j=0;j<c->count;++j)
    {
        u32 a=g_meshPool[(c->off+j)*2],b=g_meshPool[(c->off+j)*2+1];
        if(!(a&MQ_MODEL) && MQ_LX(a)==(x&15) && MQ_LY(a)==(y&15) &&
           MQ_LZ(a)==(z&15) && MQ_DIR(a)==d) return MQ_LIGHT(b)&15;
    }
    return -1;
}

static void check_faces(int x,int y,int z,int expected)
{
    for(int d=0;d<6;++d)
    {
        int actual=face_light(x+offsets[d][0],y+offsets[d][1],z+offsets[d][2],d^1);
        if(actual!=expected)
        {
            printf("FAIL: source (%d,%d,%d), face %d has %d, expected %d; RAM=%d\n",
                   x,y,z,d,actual,expected,EDIT_RAM);
            exit(1);
        }
    }
}

static void edit_during_stream(int x,int y,int z,u8 block,int before,int after)
{
    /* Return to the same resident window, then bring in an unrelated column.
       Its seam refresh cannot repair our test meshes incidentally. */
    world_stream(40,40,20,100000);
    cache_light_pump(0);
    world_update_dirty_chunks(1000000);
    cache_stream(60,40,1);
    if(!cache_light_pending()) { printf("FAIL: no background light job\n"); exit(1); }
    world_set(x,y,z,block);
    if(pendingLightEditCount!=1) { printf("FAIL: edit was not deferred\n"); exit(1); }
    world_update_dirty_chunks(1000000);
    check_faces(x,y,z,before);
    cache_light_pump(0);
    for(int n=0;n<500 && anyDirty;++n) world_update_dirty_chunks(1);
    if(anyDirty || pendingLightEditCount || (world_light_at(x,y,z)&15)!=after)
    { printf("FAIL: lighting/meshes did not settle\n"); exit(1); }
    check_faces(x,y,z,after);
}

int main(void)
{
    world_alloc(); world_generate(4242); world_stream(40,40,20,100000);
    static const int sources[][3]={{40,40,40},{47,31,40},{40,32,47}};
    for(int s=0;s<ARRAY_LEN(sources);++s)
    {
        int x=sources[s][0],y=sources[s][1],z=sources[s][2];
        for(int d=0;d<6;++d)
            world_set(x+offsets[d][0],y+offsets[d][1],z+offsets[d][2],B_STONE);
        world_set(x,y,z,B_AIR);
        u32 i=widx(x,y,z); lset(i,0,0); lset(i,1,0);
        world_update_dirty_chunks(1000000);
        check_faces(x,y,z,0);
        edit_during_stream(x,y,z,B_TORCH,0,14);
        edit_during_stream(x,y,z,B_AIR,14,0);
    }
    printf("PASS: deferred torch placement/removal refresh all six faces, including column/layer seams, RAM=%d\n",EDIT_RAM);
    return 0;
}
