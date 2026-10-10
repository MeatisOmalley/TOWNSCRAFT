/* Native driver: commands through public mixer API, output bytes for the
 * separate Python arithmetic oracle. Synthetic assets exercise boundary cases;
 * MIXER_REAL_ASSETS instead links the committed asset implementation. */
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "sound_assets.h"
#include "sound_mixer.h"

#ifndef MIXER_REAL_ASSETS
static unsigned char fixture_wave[65536];
static DosSoundSample fixture_samples[12]={
    {0,5,256,255},{16,7,128,170},{32,3,65535,255},
    {48,9,1,120},{64,1,256,255},{65,1,256,255},
    {0,65536,8192,255},{0,0,256,255},{UINT_MAX,2,256,255},
    {1,UINT_MAX,256,255},{0,5,0,255},{0,5,256,0}
};
static int ready;
int dos_sound_assets_build(void)
{
    unsigned int i;
    const unsigned char first[5]={255,0,200,80,129};
    for(i=0;i<sizeof(fixture_wave);++i)
        fixture_wave[i]=(unsigned char)(((i*73u+(i/7u)*19u)^(i>>3))&255u);
    memcpy(fixture_wave,first,sizeof(first));
    fixture_wave[64]=255; fixture_wave[65]=0;
    ready=1;
    return 0;
}
const unsigned char *dos_sound_wave(unsigned int *bytes)
{
    if(bytes) *bytes=ready ? sizeof(fixture_wave) : 0;
    return ready ? fixture_wave : NULL;
}
const DosSoundSample *dos_sound_sample(int id)
{
    return ready && id>=0 && id<12 ? &fixture_samples[id] : NULL;
}
#endif

static unsigned char output[DOS_MIXER_MAX_RENDER_FRAMES+2u];
static void check(int ok,const char *message)
{
    if(!ok) { fprintf(stderr,"FAIL mixer driver: %s\n",message); exit(1); }
}

int main(int argc,char **argv)
{
    FILE *commands,*result;
    char operation[16];
    check(argc==3,"command and output paths required");
    commands=fopen(argv[1],"r"); result=fopen(argv[2],"wb");
    check(commands && result,"open test files");
    while(fscanf(commands,"%15s",operation)==1) {
        int id,pitch,vol,pan,expected;
        unsigned int value;
        if(!strcmp(operation,"build")) check(!dos_sound_assets_build(),"build assets");
        else if(!strcmp(operation,"init")) {
            check(fscanf(commands,"%u%d",&value,&expected)==2,"init args");
            check(dos_mixer_init(value)==expected,"init status");
        }
        else if(!strcmp(operation,"play")) {
            check(fscanf(commands,"%d%d%d%d",&id,&pitch,&vol,&pan)==4,"play args");
            dos_mixer_play(id,pitch,vol,pan);
        }
        else if(!strcmp(operation,"loop")) {
            check(fscanf(commands,"%d%d",&id,&vol)==2,"loop args");
            dos_mixer_loop(id,vol);
        }
        else if(!strcmp(operation,"stop")) dos_mixer_stop();
        else if(!strcmp(operation,"render")) {
            check(fscanf(commands,"%u",&value)==1,"render args");
            check(value<=DOS_MIXER_MAX_RENDER_FRAMES,"driver buffer bound");
            memset(output,0x5a,sizeof(output));
            check(!dos_mixer_render(output+1,value),"valid render status");
            check(output[0]==0x5a,"render prefix canary");
            for(unsigned int i=value+1u;i<sizeof(output);++i)
                check(output[i]==0x5a,"render suffix canaries");
            check(fwrite(output+1,1,value,result)==value,"complete PCM write");
        }
        else if(!strcmp(operation,"null")) {
            check(fscanf(commands,"%u%d",&value,&expected)==2,"null args");
            check(dos_mixer_render(NULL,value)==expected,"NULL render status");
        }
        else if(!strcmp(operation,"reject")) {
            check(fscanf(commands,"%u",&value)==1,"reject args");
            memset(output,0x5a,sizeof(output));
            check(dos_mixer_render(output+1,value)==-1,"oversize render status");
            for(unsigned int i=0;i<sizeof(output);++i)
                check(output[i]==0x5a,"rejected render writes nothing");
        }
        else check(0,"unknown command");
    }
    check(!ferror(commands),"command read");
    check(!fclose(commands),"command close");
    check(!fclose(result),"output close");
    puts("PASS: foreground sound mixer driver");
    return 0;
}
