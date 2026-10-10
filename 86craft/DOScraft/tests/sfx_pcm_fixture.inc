/* Deterministic diagnostic commands only, not new game timing or UI. */
#define SFX_FIXTURE_BLOCKS 48u
#define SFX_FIXTURE_FRAMES 512u
static void sfx_fixture_command(unsigned int block)
{
    if(block==0) dos_mixer_loop(SFX_RAIN,128);
    if(block<10) dos_mixer_play(block,128+(block%4)*64,220,(int)block*3-15);
    if(block>=10 && block<20) dos_mixer_play((block-10)%10,256,255,0);
    if(block==20) dos_mixer_loop(SFX_RAIN,220); /* retain loop phase */
    if(block==24) dos_mixer_loop(SFX_RAIN,0);
    if(block==28) for(int id=0;id<10;++id) dos_mixer_play(id,256,200,id-5);
    if(block==40) dos_mixer_stop();
}
