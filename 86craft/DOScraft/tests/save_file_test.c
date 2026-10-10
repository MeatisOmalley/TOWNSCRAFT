/* Native fault injection into the actual DOS save transport and pinned codec. */
#include <stdio.h>
#include <stdlib.h>
#include <errno.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>
#include <string.h>
#undef NDEBUG
#include <assert.h>
static int test_open(const char *,int,...);
static int test_close(int);
static int test_fstat(int,struct stat *);
static off_t test_lseek(int,off_t,int);
static ssize_t test_read(int,void *,size_t);
static ssize_t test_write(int,const void *,size_t);
static int test_atexit(void (*)(void));
#define open test_open
#define close test_close
#define fstat test_fstat
#define lseek test_lseek
#define read test_read
#define write test_write
#define atexit test_atexit
#include "save.c"
#undef open
#undef close
#undef fstat
#undef lseek
#undef read
#undef write
#undef atexit
#include "save_state_fixture.inc"
volatile u32 g_ticks;
static u8 disk[NUM_TRACKS*TRACK_BYTES], protected_bank[NUM_TRACKS/2*TRACK_BYTES];
static u8 permanent[65536], dst[COLUMN_CELLS], other[COLUMN_CELLS];
static u32 permanent_top;
static int failure, live, read_only, reads, writes, commits, short_limit;
static int commit_error, fail_commit_number, commit_transport_failure;
static int operation_bytes;
static off_t filepos;
static void (*cleanup)(void);
static int write_tracks[1024], write_count;
void *heap_alloc_low(u32 n) {
    assert(n<=sizeof(permanent)-permanent_top);
    void *p=permanent+permanent_top; permanent_top+=n; return p;
}
static int test_open(const char *path,int flags,...) {
    assert(!strcmp(path,DOS_SAVE_PATH));
    assert(flags==(O_RDWR|O_BINARY) || flags==(O_RDONLY|O_BINARY));
    if(failure==1) { errno=ENOENT; return -1; }
    if(read_only && (flags&O_ACCMODE)==O_RDWR) { errno=EACCES; return -1; }
    assert(!live); live=1; filepos=0; return 42;
}
static int test_close(int fd) { assert(fd==42 && live); live=0; return failure==12?-1:0; }
static int test_fstat(int fd,struct stat *s) {
    assert(fd==42 && live); memset(s,0,sizeof(*s));
    s->st_mode=failure==3?S_IFDIR:S_IFREG;
    s->st_size=failure==4?sizeof(disk)-1:sizeof(disk);
    return failure==2?-1:0;
}
static int test_atexit(void (*fn)(void)) {
    assert(fn==dos_save_shutdown); if(failure==5) return -1; cleanup=fn; return 0;
}
static off_t test_lseek(int fd,off_t offset,int whence) {
    assert(fd==42 && live && whence==SEEK_SET && offset>=0 && offset<(off_t)sizeof(disk));
    if(failure==6) return -1; if(failure==7) return offset+1;
    filepos=offset; operation_bytes=0; return offset;
}
static ssize_t test_read(int fd,void *p,size_t n) {
    assert(fd==42 && live && n && n<=TRACK_BYTES && filepos+(off_t)n<=(off_t)sizeof(disk));
    ++reads; if(failure==8) return -1; if(failure==9) return 0;
    if(failure==13) return n+1;
    if(operation_bytes && failure==15) return 0;
    if(operation_bytes && failure==16) return -1;
    if(short_limit && n>(size_t)short_limit) n=short_limit;
    memcpy(p,disk+filepos,n); filepos+=n; operation_bytes+=n; return n;
}
static ssize_t test_write(int fd,const void *p,size_t n) {
    assert(fd==42 && live && n && n<=TRACK_BYTES && filepos+(off_t)n<=(off_t)sizeof(disk));
    ++writes; if(failure==10 || read_only) return -1; if(failure==11) return 0;
    if(failure==14) return n+1;
    if(operation_bytes && failure==17) return 0;
    if(operation_bytes && failure==18) return -1;
    if(short_limit && n>(size_t)short_limit) n=short_limit;
    assert(write_count<1024); write_tracks[write_count++]=filepos/TRACK_BYTES;
    memcpy(disk+filepos,p,n); filepos+=n; operation_bytes+=n; return n;
}
int __dpmi_int(int vector,__dpmi_regs *r) {
    assert(vector==0x21 && r->x.ax==0x6800 && r->x.bx==42 && live);
    ++commits;
    r->x.flags=commit_error && (!fail_commit_number || fail_commit_number==commits)?1:0;
    r->x.ax=commit_error;
    return commit_transport_failure?-1:0;
}
static u32 word_at(u32 offset) { u32 v; memcpy(&v,disk+offset,4); return v; }
static void reset_transport(void) {
    page_cancel(); failure=read_only=short_limit=commit_error=fail_commit_number=commit_transport_failure=0;
    reads=writes=commits=write_count=0; fixture_busy=0; fixture_source=-1;
    close_error=0;
}
static void reset_disk(void) { reset_transport(); memset(disk,0,sizeof(disk)); }
int main(int argc, char **argv) {
    int before,status;
    for(int f=1;f<=5;++f) {
        failure=f; cleanup_registered=0;
        assert(!fdc_start() && !live && !writes);
    }
    reset_disk(); assert(fdc_start() && cleanup);
    assert(!fdc_track(-1,0) && !fdc_track(NUM_TRACKS,1));
    assert(save_column_read(0,0,dst)==-1 && save_column_read(0,1,NULL)==-1);
    assert(save_column_read(0,COLUMN_CELLS+1,dst)==-1);
    assert(save_column_read(sizeof(disk),1,dst)==-1);
    assert(save_column_read(0xffffffffu,1,dst)==-1);
    for(u32 i=0;i<sizeof(disk);++i) disk[i]=(u8)(i*17+(i>>8));
    page_cancel();
    status=save_column_read(TRACK_BYTES-5,COLUMN_CELLS,dst);
    assert(status==0 && pageDone==5 && reads==1);
    before=reads;
    assert(save_column_read(TRACK_BYTES,COLUMN_CELLS,other)==-1 && reads==before);
    assert(save_column_read(TRACK_BYTES-5,COLUMN_CELLS,dst)==0);
    assert(save_column_read(TRACK_BYTES-5,COLUMN_CELLS,dst)==1);
    assert(!memcmp(dst,disk+TRACK_BYTES-5,COLUMN_CELLS));
    before=reads;
    memset(dst,0x5a,sizeof(dst));
    do { status=save_column_read(TRACK_BYTES-5,COLUMN_CELLS,dst); } while(!status);
    assert(status==1 && reads==before+3 && !memcmp(dst,disk+TRACK_BYTES-5,COLUMN_CELLS));
    g_ticks+=201; save_stream_tick(); assert(!live && !pageMotor);
    /* Cache survives idle close, matching the original adjacent-record reuse. */
    before=reads; assert(save_column_read(TRACK_BYTES*2,13,other)==1 && reads==before);
    page_cancel(); memset(dst,0,sizeof(dst));
    assert(save_column_read(TRACK_BYTES-5,COLUMN_CELLS,dst)==0);
    page_cancel(); assert(!pageState && !pageDest && !live);
    assert(save_column_read(7,19,other)==1 && !memcmp(other,disk+7,19));
    for(int f=6;f<=14;++f) {
        reset_transport(); assert(fdc_start()); failure=f;
        if(f==12) assert(!dos_save_finish());
        else if(f==10 || f==11 || f==14) assert(!fdc_track(0,1));
        else assert(!fdc_track(0,0));
    }
    reset_transport(); assert(fdc_start()); short_limit=17;
    assert(fdc_track(NUM_TRACKS-1,0)); memset(trackBuf,0x42,TRACK_BYTES);
    assert(fdc_track(NUM_TRACKS-1,1) && commits==1);
    assert(!memcmp(disk+sizeof(disk)-TRACK_BYTES,trackBuf,TRACK_BYTES));
    for(int f=15;f<=18;++f) {
        reset_transport(); assert(fdc_start()); short_limit=17; failure=f;
        assert(!fdc_track(0,f>=17) && operation_bytes==17);
    }
    /* Failed carry/transport commits never complete a track. */
    for(int err=1;err<=6;++err) {
        reset_transport(); assert(fdc_start()); commit_error=err;
        assert(!fdc_track(0,1));
    }
    reset_transport(); assert(fdc_start()); commit_transport_failure=1; assert(!fdc_track(0,1));
    /* Legacy v3 roundtrip, including width-256 cached version selection. */
    for(int width=32;width<=256;width*=8) {
        reset_disk(); fixture_seed(width,width==256); fixture_legacy_saves=0;
        assert(save_world()==SAVE_OK && !live && word_at(0)==SAVE_MAGIC && word_at(4)==3);
        assert(fixture_legacy_saves==(width==256));
        fixture_clear_state(); assert(load_world()==SAVE_OK && fixture_restored(width,1) && save_loaded_mobs());
        assert(!live);
        before=writes; fixture_busy=1; assert(save_world()==SAVE_DISK_ERROR && load_world()==SAVE_DISK_ERROR && writes==before);
        fixture_busy=0; g_W=16; assert(load_world()==SAVE_WRONG_SIZE); g_W=width;
        disk[8]^=1; assert(load_world()==SAVE_BAD_DATA && !save_loaded_mobs()); disk[8]^=1;
        /* Existing v3 is not transactional; do not silently promise rollback. */
    }
    reset_disk(); fixture_seed(32,1); fixture_commits=0;
    assert(save_world()==SAVE_OK && fixture_commits==1 && word_at(4)==4 && word_at(16)==1);
    assert(write_count==commits && write_tracks[0]==0 && write_tracks[write_count-1]==0);
    assert(word_at(BANK_TRACKS*TRACK_BYTES)==0);
    memcpy(protected_bank,disk,sizeof(protected_bank));
    fixture_clear_state(); assert(load_world()==SAVE_OK && fixture_restored(32,0));
    fixture_seed(32,1); writes=commits=write_count=0;
    assert(save_world()==SAVE_OK && fixture_commits==2);
    assert(word_at(BANK_TRACKS*TRACK_BYTES+16)==2);
    assert(!memcmp(protected_bank,disk,sizeof(protected_bank)));
    assert(write_tracks[0]==BANK_TRACKS && write_tracks[write_count-1]==BANK_TRACKS);
    /* Protect bank 0 even though bank 1 is newer; source reads must preserve
     * the output track containing the new global state/header. */
    fixture_source=0;
    assert(save_world()==SAVE_OK && word_at(BANK_TRACKS*TRACK_BYTES+16)==3);
    assert(!memcmp(protected_bank,disk,sizeof(protected_bank)));
    fixture_clear_state(); assert(load_world()==SAVE_OK && fixture_restored(32,0));
    fixture_source=-1;
    /* Failed first/payload/final-header commit never publishes RAM records. */
    for(int failed=1;failed<=4;++failed) {
        reset_disk(); fixture_seed(32,1); assert(save_world()==SAVE_OK);
        memcpy(protected_bank,disk,sizeof(protected_bank)); fixture_commits=0; commits=0;
        g_time=9876; /* Distinguish a failed new snapshot from the old source. */
        commit_error=5; fail_commit_number=failed;
        assert(save_world()==SAVE_DISK_ERROR && !fixture_commits && !live);
        assert(!memcmp(protected_bank,disk,sizeof(protected_bank)));
        commit_error=0; assert(load_world()==SAVE_OK);
        if(failed<4) assert(g_time==13579);
        else assert(g_time==13579 || g_time==9876); /* Failed commit has no rollback promise. */
        g_time=13579; assert(fixture_restored(32,0));
    }
    reset_disk(); fixture_seed(32,1); assert(save_world()==SAVE_OK);
    fixture_commits=0; failure=12;
    assert(save_world()==SAVE_DISK_ERROR && !fixture_commits && !live);
    failure=0; read_only=1; assert(load_world()==SAVE_OK);
    assert(save_world()==SAVE_DISK_ERROR && !live);
    reset_disk(); fixture_seed(32,0); assert(save_world()==SAVE_OK);
    assert(save_column_read(0,16,dst)==1); failure=12;
    g_ticks+=201; save_stream_tick(); assert(!live && close_error);
    failure=0; before=writes;
    assert(save_world()==SAVE_DISK_ERROR && writes==before && !close_error);
    assert(save_world()==SAVE_OK);
    assert(save_column_read(0,16,dst)==1); failure=12; before=writes;
    assert(save_world()==SAVE_DISK_ERROR && writes==before && !close_error);
    failure=0; assert(load_world()==SAVE_OK);
    assert(save_column_read(0,16,dst)==1); failure=12; g_ticks+=201; save_stream_tick();
    failure=0; before=reads; assert(load_world()==SAVE_DISK_ERROR && reads==before && !close_error);
    assert(load_world()==SAVE_OK);
    assert(save_column_read(0,16,dst)==1); failure=12; before=reads;
    assert(load_world()==SAVE_DISK_ERROR && reads==before && !close_error);
    /* Final-load close error is also propagated and clears loadedMobs. */
    assert(load_world()==SAVE_DISK_ERROR && !live && !save_loaded_mobs());
    failure=0; assert(load_world()==SAVE_OK);
    /* Historical v1/v2 inputs retain their original chest/mob omissions. */
    for(int version=1;version<=2;++version) {
        u32 sum;
        reset_disk(); fixture_seed(32,0);
        assert(fdc_start()); streamBase=0; streamLimit=NUM_TRACKS; stream_begin(1);
        put32(SAVE_MAGIC); put32(version); put32(0); streamSum=0;
        put_state(); put_blocks(); if(version>=2) put_chests();
        sum=streamSum; stream_end_write(); assert(streamOk);
        assert(fdc_track(0,0)); memcpy(trackBuf+8,&sum,4); assert(fdc_track(0,1)); fdc_stop();
        fixture_clear_state(); assert(load_world()==SAVE_OK && !save_loaded_mobs());
        for(int i=0;i<8;++i) assert(!fixture_mobs[i]);
        if(version==1) {
            for(int i=0;i<MAX_CHESTS;++i) assert(!g_chests[i].used);
        } else assert(g_chests[0].used && g_chests[1].used);
        /* State and block RLE must still be restored. */
        assert(g_player.pitch==-21 && g_time==13579 && g_inv[0].count==63);
        for(u32 i=0;i<32*32*WH;++i) assert(fixture_blocks[i]==((i%1024<100)?B_STONE:B_AIR));
    }
    reset_disk(); fixture_seed(32,0); assert(load_world()==SAVE_NOT_A_SAVE);
    failure=1; assert(load_world()==SAVE_NO_DISK && save_world()==SAVE_NO_DISK);
    /* Remaining inherited version-precedence issue; next separate bugfix. */
    reset_disk(); fixture_seed(32,1); assert(save_world()==SAVE_OK && save_world()==SAVE_OK);
    fixture_seed(256,1); assert(save_world()==SAVE_OK && word_at(4)==3);
    assert(load_world()==SAVE_WRONG_SIZE); /* Old v4 bank 1 wins over new v3. */
    reset_transport(); assert(save_column_read(0,16,dst)==1);
    memcpy(other,dst,16); memset(dst,0x5a,16); /* Represents scratch reuse by terrain. */
    before=reads;
    assert(save_column_read(0,16,dst)==1 && !memcmp(dst,other,16) && reads==before);
    reset_transport(); cleanup(); cleanup(); assert(!live);
    assert(!strcmp(save_error_text(SAVE_NO_DISK),"No disk in drive A"));
    if(argc==2) {
        FILE *output;
        reset_disk(); fixture_seed(256,1); assert(save_world()==SAVE_OK);
        fixture_clear_state(); assert(load_world()==SAVE_OK && fixture_restored(256,1));
        fixture_seed(32,1); assert(save_world()==SAVE_OK);
        fixture_clear_state(); assert(load_world()==SAVE_OK && fixture_restored(32,0));
        fixture_seed(32,1); fixture_source=0; assert(save_world()==SAVE_OK);
        fixture_clear_state(); assert(load_world()==SAVE_OK && fixture_restored(32,0));
        output=fopen(argv[1],"wb"); assert(output);
        assert(fwrite(disk,1,sizeof(disk),output)==sizeof(disk)); assert(!fclose(output));
    }
    puts("PASS: DOS save transport and pinned codec fixtures (not world/mob integration)");
    return 0;
}
