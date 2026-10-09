/* Fault injection for the real adapter body, not guest/controller evidence. */
#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>
#include <string.h>
#undef NDEBUG
#include <assert.h>
static int test_open(const char *, int, ...);
static int test_close(int);
static int test_fstat(int, struct stat *);
static off_t test_lseek(int, off_t, int);
static ssize_t test_read(int, void *, size_t);
static ssize_t test_write(int, const void *, size_t);
static int test_atexit(void (*)(void));
#define open test_open
#define close test_close
#define fstat test_fstat
#define lseek test_lseek
#define read test_read
#define write test_write
#define atexit test_atexit
#include "hdd.c"
#undef open
#undef close
#undef fstat
#undef lseek
#undef read
#undef write
#undef atexit

static unsigned char disk[65536], buffer[12288], other[12288];
static int failure, opens, closes, reads, writes, flushes, registrations;
static int fd_live, short_limit;
static off_t position;
static void (*cleanup)(void);
static char trace[16384];
static int trace_count;
static void traced(char operation) { assert(trace_count<(int)sizeof(trace)); trace[trace_count++]=operation; }
static int test_open(const char *path, int flags, ...) {
    assert(!strcmp(path,DOS_TERRAIN_PATH));
    assert(flags==(O_RDWR|O_BINARY)); ++opens;
    if(failure==1) return -1;
    assert(!fd_live); fd_live=1; position=0; return 42;
}
static int test_close(int fd) {
    assert(fd==42 && fd_live); ++closes; fd_live=0; return failure==15?-1:0;
}
static int test_fstat(int fd, struct stat *info) {
    assert(fd==42 && fd_live); memset(info,0,sizeof(*info));
    info->st_mode=failure==5?S_IFDIR:S_IFREG;
    info->st_size=failure==3?0:failure==4?65535:65536;
    return failure==2?-1:0;
}
static off_t test_lseek(int fd, off_t offset, int whence) {
    traced('S');
    assert(fd==42 && fd_live && whence==SEEK_SET && offset>=0 && offset<65536);
    if(failure==7) return -1;
    if(failure==8) return offset+1;
    position=offset; return offset;
}
static ssize_t test_read(int fd, void *dst, size_t size) {
    traced('R');
    assert(fd==42 && fd_live && size && size<=2048 && position+(off_t)size<=65536);
    ++reads;
    if(failure==9) return -1;
    if(failure==10) return 0;
    if(failure==11) return size+1;
    if(short_limit && size>(size_t)short_limit) size=short_limit;
    memcpy(dst,disk+position,size); position+=size; return size;
}
static ssize_t test_write(int fd, const void *src, size_t size) {
    traced('W');
    assert(fd==42 && fd_live && size && size<=2048 && position+(off_t)size<=65536);
    ++writes;
    if(failure==12) return -1;
    if(failure==13) return 0;
    if(short_limit && size>(size_t)short_limit) size=short_limit;
    memcpy(disk+position,src,size); position+=size; return size;
}
int __dpmi_int(int vector, __dpmi_regs *regs) {
    assert(vector==0x21 && regs->x.ax==0x6800 && regs->x.bx==42 && fd_live);
    traced('F'); ++flushes;
    regs->x.flags=(failure==14 || failure==16 || failure==17)?1:0;
    regs->x.ax=failure==16?1:failure==17?6:failure==14?5:0;
    return failure==18?-1:0;
}
static int test_atexit(void (*fn)(void)) {
    assert(fn==dos_hdd_shutdown); ++registrations;
    if(failure==6) return -1;
    cleanup=fn; return 0;
}

int main(void) {
    int f,i,before,status;
    assert(!hdd_sector_count() && hdd_init()==1); /* normal file exists */
    dos_hdd_shutdown(); cleanup_registered=0;
    for(f=1;f<=6;++f) {
        failure=f; before=writes;
        assert(!hdd_init() && !hdd_sector_count() && !fd_live && writes==before);
        assert(hdd_error()==(f==1?HDD_ERROR_ABSENT:f==6?HDD_ERROR_MEMORY:HDD_ERROR_CAPACITY));
    }
    failure=0;
    assert(hdd_transfer(0,1,0,buffer)==-1 && hdd_error()==HDD_ERROR_ABSENT);
    assert(hdd_init() && hdd_sector_count()==128 && cleanup);
    before=opens; assert(hdd_init() && opens==before);
    for(i=0;i<65536;++i) disk[i]=(unsigned char)(i*37+(i>>8));
    assert(hdd_transfer(0,0,0,buffer)==-1 && hdd_error()==HDD_ERROR_ARGUMENT);
    assert(hdd_transfer(0,25,0,buffer)==-1 && hdd_error()==HDD_ERROR_ARGUMENT);
    assert(hdd_transfer(0,1,2,buffer)==-1 && hdd_error()==HDD_ERROR_ARGUMENT);
    assert(hdd_transfer(0,1,0,0)==-1 && hdd_error()==HDD_ERROR_ARGUMENT);
    assert(hdd_transfer(127,2,0,buffer)==-1 && hdd_error()==HDD_ERROR_CAPACITY);
    assert(hdd_transfer(0xFFFFFFFFu,24,0,buffer)==-1 && hdd_error()==HDD_ERROR_CAPACITY);
    assert(hdd_transfer(128,1,0,buffer)==-1 && hdd_error()==HDD_ERROR_CAPACITY);
    before=reads;
    assert(!hdd_transfer(1,24,0,buffer) && reads==before+1 && request.copied==2048);
    assert(hdd_transfer(2,24,0,buffer)==-1 && hdd_error()==HDD_ERROR_BUSY);
    assert(hdd_transfer(1,23,0,buffer)==-1 && hdd_error()==HDD_ERROR_BUSY);
    assert(hdd_transfer(1,24,1,buffer)==-1 && hdd_error()==HDD_ERROR_BUSY);
    assert(hdd_transfer(1,24,0,other)==-1 && hdd_error()==HDD_ERROR_BUSY);
    assert(request.active && request.copied==2048 && reads==before+1);
    assert(hdd_init());
    assert(hdd_transfer_sync(1,24,0,buffer)==1 && !memcmp(buffer,disk+512,12288));
    assert(!hdd_error() && !request.active && reads==before+6);
    disk[512]^=255;
    assert(hdd_transfer_sync(1,24,0,buffer)==1 && !memcmp(buffer,disk+512,12288));
    for(i=0;i<12288;++i) buffer[i]=(unsigned char)(i*11+(i>>7));
    before=flushes; trace_count=0;
    assert(hdd_transfer_sync(80,24,1,buffer)==1 && flushes==before+1);
    assert(trace_count==13 && !memcmp(trace,"SWSWSWSWSWSWF",13));
    assert(!memcmp(buffer,disk+80*512,12288));
    assert(hdd_transfer_sync(127,1,0,other)==1 && !memcmp(other,disk+127*512,512));
    short_limit=17;
    assert(hdd_transfer_sync(30,24,1,buffer)==1 && !memcmp(buffer,disk+30*512,12288));
    assert(hdd_transfer_sync(30,24,0,other)==1 && !memcmp(buffer,other,12288));
    short_limit=0;
    for(f=7;f<=11;++f) {
        failure=f;
        assert(hdd_transfer(0,1,0,buffer)==-1 && hdd_error()==HDD_ERROR_STATUS && !request.active);
    }
    for(f=12;f<=14;++f) {
        failure=f;
        assert(hdd_transfer(0,1,1,buffer)==-1 && hdd_error()==HDD_ERROR_STATUS && !request.active);
    }
    failure=0;
    for(f=12;f<=18;++f) {
        if(f==15) continue; /* Close-error case, tested below. */
        memcpy(other,disk+80*512,12288);
        trace_count=0;
        assert(!hdd_transfer(30,24,1,buffer));
        failure=f;
        if(f==12 || f==13) assert(hdd_transfer(30,24,1,buffer)==-1);
        else {
            assert(hdd_transfer_sync(30,24,1,buffer)==-1);
            assert(trace_count==13 && !memcmp(trace,"SWSWSWSWSWSWF",13));
        }
        assert(!request.active && hdd_error()==HDD_ERROR_STATUS);
        assert(!memcmp(other,disk+80*512,12288)); /* Published alternate untouched. */
        failure=0;
        assert(hdd_transfer_sync(30,24,1,buffer)==1 && !hdd_error());
    }
    assert(!hdd_transfer(0,24,0,buffer));
    failure=9;
    assert(hdd_transfer(0,24,0,buffer)==-1 && !request.active);
    failure=0;
    assert(hdd_transfer_sync(0,24,0,buffer)==1 && !memcmp(buffer,disk,12288));
    assert(!hdd_transfer(0,24,1,buffer));
    before=flushes;
    hdd_cancel(); assert(!request.active && hdd_error()==HDD_ERROR_CANCELLED && flushes==before);
    hdd_cancel(); assert(hdd_error()==HDD_ERROR_CANCELLED);
    assert(hdd_transfer_sync(0,24,1,buffer)==1 && !hdd_error());
    before=closes; cleanup(); assert(closes==before+1 && !fd_live && !hdd_sector_count());
    cleanup(); assert(closes==before+1);
    before=registrations; assert(hdd_init() && registrations==before);
    assert(!hdd_transfer(0,24,0,buffer));
    failure=15; dos_hdd_shutdown();
    assert(!fd_live && !request.active && !hdd_sector_count() && hdd_error()==HDD_ERROR_STATUS);
    failure=0; assert(hdd_init());
    status=hdd_transfer_sync(0,24,0,buffer); assert(status==1);
    dos_hdd_shutdown();
    puts("PASS: DOS terrain transport / bounds / request identity / short I/O / faults / cleanup");
    return 0;
}
