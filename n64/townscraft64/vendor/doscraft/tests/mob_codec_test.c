/* Native diagnostic, following Towns tests/mob_regions_test.c's inclusion
   pattern. mobs.c is the byte-identical pinned staged source, not a codec
   replacement. Inclusion permits fixture setup/inspection of private dormant
   owners and region metadata without modifying production declarations.

   Link real staged blocks, fmath, libc and generated lookup tables. The heap
   callback supplies one bounded diagnostic arena. Terrain is never generated:
   region migration uses the production manager with every region visited and
   the legacy NULL column map. Unused renderer/audio/game/physics/world callbacks
   below fail on invocation; only TNT spawn's audio notification is permitted.
   This exercises serialization, not DOS storage, collision, or gameplay. */
#include <stdio.h>
#include <stdlib.h>
#include "mobs.c"

enum { RECORD_WORDS=21, BUFFER_WORDS=5+256+(MAX_MOBS+MAX_DORMANT_MOBS)*RECORD_WORDS };
enum { ID, TYPE, HEALTH, X, Y, Z, VX, VY, VZ, FALL, FLAGS,
       YAW, TARGET, AI, WALKING, HURT, ATTACK, PANIC, FUSE, BURN, PHASE };
static u32 wire[BUFFER_WORDS], baseline[BUFFER_WORDS];
static int written, available, readPos, eof, calls, baselineWords;
static const char *caseName;
static DormantMob arena[MAX_DORMANT_MOBS];
static int allocations, audioCalls;

int g_W=96,g_NC=6;
u32 g_ramMB=4;
Player g_player;
u8 *g_blocks,*g_light,*g_columnReady;
int *g_zOff;
short *g_columnMap,*g_columnCoords;
int g_residentColumns,g_allocChunkCount;

static void check(int ok,const char *message)
{
    if(!ok) { fprintf(stderr,"FAIL %s W=%d: %s\n",caseName,g_W,message); exit(1); }
}
static void unused(const char *name) { check(0,name); }
void *heap_alloc_low(u32 bytes)
{
    check(bytes==sizeof(arena) && allocations++==0,"bounded dormant allocation");
    return arena;
}
void sound_play_at(int sfx,int pitch,int vol,int x,int y,int z)
{
    (void)x; (void)y; (void)z;
    check(sfx==SFX_HISS && pitch==256 && vol==255,"TNT spawn audio notification");
    ++audioCalls;
}
void world_set(int x,int y,int z,u8 b)
{ (void)x; (void)y; (void)z; (void)b; unused("world_set callback"); }
int chest_remove(int x,int y,int z,int giveItems)
{ (void)x; (void)y; (void)z; (void)giveItems; unused("chest_remove callback"); return 0; }
void player_hurt(int dmg,int fromX,int fromZ)
{ (void)dmg; (void)fromX; (void)fromZ; unused("player_hurt callback"); }
void body_tick(Body *b,int gravity)
{ (void)b; (void)gravity; unused("body_tick callback"); }
int face_light_level(int light,int dir,int darken)
{ (void)light; (void)dir; (void)darken; unused("lighting callback"); return 0; }
MBox *render_add_box(void) { unused("renderer callback"); return NULL; }
int inv_add(int item,int count)
{ (void)item; (void)count; unused("inventory callback"); return 0; }
void game_message(const char *message)
{ (void)message; unused("game_message callback"); }
int world_surface_y(int x,int z)
{ (void)x; (void)z; unused("world_surface_y callback"); return 0; }
int world_light_at(int x,int y,int z)
{ (void)x; (void)y; (void)z; unused("world_light_at callback"); return 0; }

static void put_word(u32 word)
{
    check(written<BUFFER_WORDS,"bounded output buffer");
    wire[written++]=word;
}
static u32 get_word(void)
{
    check(++calls<=BUFFER_WORDS,"bounded input callback calls");
    if(readPos>=available) { eof=1; return 0; }
    return wire[readPos++];
}
static int load_words(int count)
{
    check(count>=0 && count<=BUFFER_WORDS,"input size bound");
    available=count; readPos=eof=calls=0;
    return mobs_load(get_word);
}
static void save_words(void) { written=0; mobs_save(put_word); }
static int first_record(void) { return 5+g_NC*g_NC; }
static int active_count(void)
{
    int n=0;
    for(int i=0; i<MAX_MOBS; ++i) n+=g_mobs[i].type!=MOB_NONE;
    return n;
}
static int by_id(u32 id)
{
    for(int i=0; i<MAX_MOBS; ++i)
        if(g_mobs[i].type && g_mobIDs[i]==id) return i;
    return -1;
}
static void reset_fixture(int width)
{
    g_W=width; g_NC=width/CS;
    memset(&g_player,0,sizeof(g_player));
    g_player.body.x=g_player.body.z=width/2*FU;
    mobs_clear();
    regionSeed=0xFEDCBA98u;
    memset(regionVisited,1,sizeof(regionVisited));
    rnd_seed(4242);
}
static void fill_fields(Mob *m,int variant)
{
    m->health=123+variant;
    m->body.vx=-12345-variant; m->body.vy=23456+variant;
    m->body.vz=-34567-variant; m->body.fallDist=45678+variant;
    m->body.onGround=variant&1; m->body.inWater=(variant>>1)&1;
    m->body.hitWall=(variant>>2)&1; m->body.headInWater=(variant>>3)&1;
    m->yaw=101+variant; m->targetYaw=901-variant;
    m->aiTimer=-41-variant; m->walking=variant&1;
    m->hurtTimer=11+variant; m->attackTimer=22+variant;
    m->panic=33+variant; m->fuse=44+variant;
    m->burnTimer=55+variant; m->walkPhase=-6677-variant;
}
static void same_mob(const Mob *a,const Mob *b)
{
#define SAME(f) check(a->f==b->f,"restored field " #f)
    SAME(type); SAME(health); SAME(body.x); SAME(body.y); SAME(body.z);
    SAME(body.vx); SAME(body.vy); SAME(body.vz); SAME(body.fallDist);
    SAME(body.onGround); SAME(body.inWater); SAME(body.hitWall); SAME(body.headInWater);
    SAME(yaw); SAME(targetYaw); SAME(aiTimer); SAME(walking);
    SAME(hurtTimer); SAME(attackTimer); SAME(panic); SAME(fuse); SAME(burnTimer); SAME(walkPhase);
    /* Dimensions are derived from type, not serialized. */
    SAME(body.hw); SAME(body.h);
#undef SAME
}
static void expected_record(int offset,const Mob *m,u32 id)
{
    const u32 expected[RECORD_WORDS]={
        id,m->type,(u32)(int)m->health,
        (u32)m->body.x,(u32)m->body.y,(u32)m->body.z,
        (u32)m->body.vx,(u32)m->body.vy,(u32)m->body.vz,(u32)m->body.fallDist,
        (u32)m->body.onGround|((u32)m->body.inWater<<8)|
        ((u32)m->body.hitWall<<16)|((u32)m->body.headInWater<<24),
        (u32)m->yaw,(u32)m->targetYaw,(u32)m->aiTimer,(u32)m->walking,
        (u32)m->hurtTimer,(u32)m->attackTimer,(u32)m->panic,(u32)m->fuse,
        (u32)m->burnTimer,(u32)m->walkPhase
    };
    check(!memcmp(wire+offset,expected,sizeof(expected)),"explicit 21-word wire field order");
}
static void mixed_fixture(int width)
{
    reset_fixture(width);
    for(int type=MOB_PIG; type<=MOB_SHEEP; ++type)
    {
        int slot=mob_spawn(type,8*FU,2*FU,8*FU);
        check(slot>=0,"spawn dormant animal"); fill_fields(&g_mobs[slot],type+10);
    }
    mobs_regions_tick();
    check(active_count()==0 && mobs_dormant_count()==2,"real manager creates dormant owners");
    /* A dead slot leaves a gap in IDs and proves serialization compacts slots. */
    int dead=mob_spawn(MOB_PIG,width/2*FU,FU,width/2*FU);
    check(dead>=0,"spawn discarded fixture slot");
    for(int type=MOB_PIG; type<NUM_MOB_TYPES; ++type)
    {
        int slot=mob_spawn(type,width/2*FU+type,3*FU+type,width/2*FU-type);
        check(slot>=0,"spawn each active type"); fill_fields(&g_mobs[slot],type);
    }
    g_mobs[dead].type=MOB_NONE;
    for(int i=0; i<g_NC*g_NC; ++i) regionVisited[i]=(i%3)!=0;
    save_words(); baselineWords=written;
    memcpy(baseline,wire,written*sizeof(u32));
}
static void pristine(void) { memcpy(wire,baseline,baselineWords*sizeof(u32)); }
static void cleared(void)
{
    check(!active_count() && !mobs_dormant_count() && nextMobID==1,"failed load clears owners and ID counter");
    for(int i=0; i<MAX_MOBS; ++i) check(!g_mobIDs[i],"failed load clears active IDs");
    for(int i=0; i<ARRAY_LEN(regionVisited); ++i) check(!regionVisited[i],"failed load clears region states");
}
static void reject_word(int index,u32 value,const char *reason)
{
    pristine(); wire[index]=value;
    check(!load_words(baselineWords),reason);
    check(!eof,"malformed full stream does not underflow"); cleared();
}
static void roundtrip(void)
{
    const int widths[]={96,160,208,256};
    for(int w=0; w<ARRAY_LEN(widths); ++w)
    {
        Mob expected[7]; u32 ids[7]; int n=0;
        mixed_fixture(widths[w]);
        for(int i=0; i<MAX_MOBS; ++i) if(g_mobs[i].type)
        { expected[n]=g_mobs[i]; ids[n++]=g_mobIDs[i]; }
        for(int i=0; i<2; ++i)
        { expected[n]=dormant[i].mob; ids[n++]=dormant[i].id; }
        check(written==first_record()+7*RECORD_WORDS,"mixed stream length");
        check(wire[0]==0xFEDCBA98u && wire[1]==9 && wire[2]==(u32)(g_NC*g_NC),"seed, next ID and region count");
        for(int i=0; i<g_NC*g_NC; ++i) check(wire[3+i]==(u32)((i%3)!=0),"visited/empty region persistence");
        check(wire[first_record()-2]==5 && wire[first_record()-1]==2,"active/dormant counts");
        for(int i=0; i<7; ++i) expected_record(first_record()+i*RECORD_WORDS,&expected[i],ids[i]);
        for(int pass=0; pass<3; ++pass)
        {
            check(load_words(baselineWords) && !eof && readPos==baselineWords,"complete load consumes all words");
            check(active_count()==5 && mobs_dormant_count()==2,"restored owner counts");
            for(int i=0; i<5; ++i)
            { int slot=by_id(ids[i]); check(slot>=0,"stable active identity"); same_mob(&g_mobs[slot],&expected[i]); }
            for(int i=0; i<2; ++i)
            { check(dormant[i].id==ids[5+i],"stable dormant identity"); same_mob(&dormant[i].mob,&expected[5+i]); }
            save_words();
            check(written==baselineWords && !memcmp(wire,baseline,written*sizeof(u32)),"repeated save/load byte stability");
        }
        int slot=mob_spawn(MOB_PIG,48*FU,FU,48*FU);
        check(slot>=0 && g_mobIDs[slot]==9,"new spawn resumes saved next ID");
        memset(regionVisited,1,sizeof(regionVisited));
        g_player.body.x=g_player.body.z=8*FU;
        mobs_regions_tick();
        for(int i=5; i<7; ++i)
        { slot=by_id(ids[i]); check(slot>=0,"loaded dormant identity reactivates"); same_mob(&g_mobs[slot],&expected[i]); }
    }
}
static void empty_capacity(void)
{
    reset_fixture(256); memset(regionVisited,0,sizeof(regionVisited)); save_words();
    check(written==first_record() && load_words(written) && !eof,"empty populations load");
    check(!active_count() && !mobs_dormant_count() && nextMobID==1,"empty owner metadata");
    for(int i=0; i<MAX_MOBS; ++i)
    {
        int slot=mob_spawn(1+i%5,FU,FU,FU);
        check(slot==i,"fill active capacity"); fill_fields(&g_mobs[slot],i);
    }
    for(int i=0; i<MAX_DORMANT_MOBS; ++i)
    {
        dormant[i].mob=g_mobs[i%2]; dormant[i].mob.type=1+i%2;
        dormant[i].id=nextMobID++;
    }
    dormantCount=MAX_DORMANT_MOBS;
    save_words(); baselineWords=written; memcpy(baseline,wire,written*sizeof(u32));
    check(written==BUFFER_WORDS && load_words(written) && !eof,"maximum bounded populations load");
    check(active_count()==MAX_MOBS && mobs_dormant_count()==MAX_DORMANT_MOBS,"capacity counts preserved");
    save_words(); check(!memcmp(wire,baseline,sizeof(wire)),"maximum population wire stability");
}
static void malformed_headers(void)
{
    mixed_fixture(96);
    reject_word(1,0,"zero next ID");
    reject_word(1,baseline[first_record()],"next ID equals existing ID");
    reject_word(1,8,"next ID equals maximum existing ID");
    reject_word(2,g_NC*g_NC-1,"wrong region dimensions");
    reject_word(2,0xFFFFFFFFu,"unsigned region count");
    reject_word(3,2,"invalid visited state");
    reject_word(3,0xFFFFFFFFu,"unsigned visited state");
    reject_word(first_record()-2,MAX_MOBS+1,"active count over capacity");
    reject_word(first_record()-2,0xFFFFFFFFu,"unsigned active count");
    reject_word(first_record()-1,MAX_DORMANT_MOBS+1,"dormant count over capacity");
    reject_word(first_record()-1,0xFFFFFFFFu,"unsigned dormant count");
}
static void malformed_records(void)
{
    mixed_fixture(96); int first=first_record();
    reject_word(first+ID,0,"zero identity");
    reject_word(first+RECORD_WORDS+ID,baseline[first+ID],"duplicate active identity");
    reject_word(first+5*RECORD_WORDS+ID,baseline[first+ID],"duplicate active/dormant identity");
    reject_word(first+6*RECORD_WORDS+ID,baseline[first+5*RECORD_WORDS+ID],"duplicate dormant identity");
    reject_word(first+TYPE,MOB_NONE,"none type");
    reject_word(first+TYPE,NUM_MOB_TYPES,"type above enum");
    reject_word(first+TYPE,257,"wide type cannot narrow to pig");
    reject_word(first+HEALTH,0,"zero health");
    reject_word(first+HEALTH,32768,"health over s16 range");
    reject_word(first+HEALTH,0xFFFFFFFFu,"negative health encoding");
    for(int type=MOB_ZOMBIE; type<NUM_MOB_TYPES; ++type)
        reject_word(first+5*RECORD_WORDS+TYPE,type,"dormant owner must be animal");
}
static void malformed_ranges(void)
{
    mixed_fixture(96); int first=first_record();
    const int fields[]={X,Z,Y,YAW,TARGET,WALKING,HURT,ATTACK,PANIC,FUSE,BURN};
    for(int owner=0; owner<2; ++owner)
    {
        int start=first+owner*5*RECORD_WORDS;
        for(int i=0; i<ARRAY_LEN(fields); ++i)
            reject_word(start+fields[i],0x80000000u,"negative/INT_MIN validated field");
        reject_word(start+X,g_W*FU,"x exclusive upper bound");
        reject_word(start+Z,g_W*FU,"z exclusive upper bound");
        reject_word(start+Y,(u32)(-8*FU-1),"y below lower bound");
        reject_word(start+Y,(WH+8)*FU,"y exclusive upper bound");
        reject_word(start+YAW,ANG_MASK+1,"yaw over angle mask");
        reject_word(start+TARGET,ANG_MASK+1,"target yaw over angle mask");
        reject_word(start+WALKING,2,"walking nonboolean");
        for(int byte=0; byte<4; ++byte)
            for(int bit=1; bit<8; ++bit)
                reject_word(start+FLAGS,1u<<(byte*8+bit),"invalid packed flag bit");
    }
}
static void valid_boundaries(void)
{
    mixed_fixture(96); int first=first_record();
    for(int edge=0; edge<2; ++edge)
    {
        pristine();
        for(int owner=0; owner<2; ++owner)
        {
            int start=first+owner*5*RECORD_WORDS;
            wire[start+HEALTH]=edge ? 32767 : 1;
            wire[start+X]=wire[start+Z]=edge ? g_W*FU-1 : 0;
            wire[start+Y]=edge ? (WH+8)*FU-1 : (u32)(-8*FU);
            wire[start+YAW]=wire[start+TARGET]=edge ? ANG_MASK : 0;
            wire[start+WALKING]=edge; wire[start+FLAGS]=edge ? 0x01010101u : 0;
            for(int f=HURT; f<=BURN; ++f) wire[start+f]=edge ? 0x7FFFFFFFu : 0;
            /* The codec intentionally has no velocity/fall/AI/phase bounds. */
            wire[start+VX]=wire[start+VZ]=wire[start+AI]=0x80000000u;
            wire[start+VY]=wire[start+FALL]=wire[start+PHASE]=0x7FFFFFFFu;
        }
        u32 expected[BUFFER_WORDS]; memcpy(expected,wire,baselineWords*sizeof(u32));
        check(load_words(baselineWords) && !eof,"inclusive valid range boundaries");
        save_words(); check(written==baselineWords && !memcmp(wire,expected,written*sizeof(u32)),"boundary words persist exactly");
    }
}
static void truncation(void)
{
    mixed_fixture(96); int rawAccepted=0;
    for(int prefix=0; prefix<baselineWords; ++prefix)
    {
        pristine(); int ok=load_words(prefix);
        check(eof,"every truncated valid prefix sets transport EOF");
        check(readPos==prefix,"truncation never reads beyond available words");
        if(ok) ++rawAccepted; else cleared();
        /* A storage adapter must combine the real codec result with its own
           sticky EOF/error status; get(void) cannot communicate EOF itself. */
        check(!(ok && !eof),"transport rejects every truncated prefix");
    }
    check(rawAccepted>0,"document zero-filled suffix acceptance in unchanged codec");
    printf("INFO: %d truncated prefixes, %d accepted by raw zero-filling codec; all signal transport EOF\n",baselineWords,rawAccepted);
    pristine(); check(load_words(baselineWords) && !eof,"valid full stream after truncation sweep");
    pristine(); wire[baselineWords]=0xDEADBEEFu;
    check(load_words(baselineWords+1) && !eof && readPos==baselineWords,"codec leaves following section untouched");
}

int main(int argc,char **argv)
{
    check(argc==2,"one named test required"); caseName=argv[1];
    if(!strcmp(caseName,"roundtrip")) roundtrip();
    else if(!strcmp(caseName,"empty_capacity")) empty_capacity();
    else if(!strcmp(caseName,"malformed_headers")) malformed_headers();
    else if(!strcmp(caseName,"malformed_records")) malformed_records();
    else if(!strcmp(caseName,"malformed_ranges")) malformed_ranges();
    else if(!strcmp(caseName,"valid_boundaries")) valid_boundaries();
    else if(!strcmp(caseName,"truncation")) truncation();
    else check(0,"unknown named test");
    check(allocations==1,"single bounded heap allocation");
    check(audioCalls>0,"real TNT spawn callback exercised");
    printf("PASS: pinned Towns mobs codec %s\n",caseName);
    return 0;
}
