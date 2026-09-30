/* Sound.

   Effects use the RF5c68 PCM chip: the samples are synthesized at start
   into its 64 KB wave RAM, and each effect plays on the next of its 8
   channels with a pitch, volume and pan.

   Music uses the YM2612 FM chip: an electric piano patch on all 6
   channels, 3 for each hand, and a small sequencer driven by the timer
   interrupt.  The piece is an original composition, written below as note
   strings. */
#include "sound.h"
#include "hw.h"
#include "fmath.h"
#include "sys.h"

int g_musicOn=1,g_sfxOn=1;

/* ---------------- PCM (RF5c68) ----------------

   I/O 4F0h-4F6h: channel registers (ENV, PAN, FD low/high, loop start
   low/high, start page) of the channel selected in 4F7h.  4F7h: bit 7 =
   chip on, bit 6 = select channel (bits 0-2), else select the wave RAM
   bank (bits 0-3) shown at C2200000h (4 KB).  4F8h: channel off bits.
   Samples are sign and magnitude bytes; FFh ends a sample and makes the
   channel jump to its loop start.  Every effect loops to a silent word at
   address 0, so a channel goes quiet when its sample ends. */
#define PCM_WINDOW ((volatile u8 *)0xC2200000)
#define PCM_RATE 20833            /* Hz at FD=0800h */

typedef struct
{
	u16 start;    /* Page (256 bytes) */
	u16 pitch;    /* FD at pitch 256 */
	u8 vol;       /* Base volume */
} SfxDef;
static SfxDef sfx[NUM_SFX];
static u8 pcmOff=0xFF;            /* Shadow of 4F8h */
static int nextCh;
static int lisX,lisY,lisZ,lisYaw;

static u32 synthSeed=12345;
static int noise(void)
{
	synthSeed=synthSeed*1103515245u+12345u;
	return (int)((synthSeed>>16)&0xFF)-128;
}

/* Samples are written straight into wave RAM, one 4 KB bank at a time */
static int synthTop,synthBank=-1;

static void put_byte(u8 b)
{
	int bank=synthTop>>12;
	if(bank!=synthBank)
	{
		synthBank=bank;
		outb(0x4F7,bank);             /* Chip off, select the bank */
	}
	PCM_WINDOW[synthTop&4095]=b;
	++synthTop;
}

static void put_sample(int v)
{
	v=CLAMP(v,-126,126);
	put_byte((v>=0) ? (u8)v : (u8)(0x80|(-v)));
}

static int begin_sample(int id,int pitch,int vol)
{
	synthTop=(synthTop+255)&~255;
	sfx[id].start=synthTop>>8;
	sfx[id].pitch=pitch;
	sfx[id].vol=vol;
	return synthTop;
}

static void end_sample(void)
{
	put_byte(0xFF);
}

/* Filtered noise with an envelope: lp = 0..7 (more = darker), grain adds
   crunchy amplitude jumps */
static void gen_noise(int id,int len,int lp,int attack,int grain,int pitch,int vol)
{
	int i,y=0,g=256;
	begin_sample(id,pitch,vol);
	for(i=0; i<len; ++i)
	{
		int env=(len-i)*256/len;          /* Linear decay */
		env=env*env>>8;                   /* ... squared: quick fall */
		if(i<attack)
		{
			env=env*i/attack;
		}
		if(grain && 0==i%grain)
		{
			g=96+((noise()+128)*160>>8);
		}
		y+=(noise()-y)>>lp;
		put_sample((y*env>>8)*g>>(lp>2 ? 6 : 7));
	}
	end_sample();
}

/* Decaying tone: frequency from f0 to f1 (Hz), plus a noise click */
static void gen_tone(int id,int len,int f0,int f1,int square,int click,int pitch,int vol)
{
	int i,phase=0,y=0;
	begin_sample(id,pitch,vol);
	for(i=0; i<len; ++i)
	{
		int f=f0+(f1-f0)*i/len;
		int env=(len-i)*256/len,v;
		phase+=f*65536/PCM_RATE;
		if(square)
		{
			v=(phase&0x8000) ? 90 : -90;
		}
		else
		{
			v=fsin((phase>>6)&ANG_MASK)*110>>14;
		}
		if(i<click)
		{
			v+=noise()*(click-i)/click;
		}
		y+=(v-y)>>1;                      /* Soften */
		put_sample(y*env>>8);
	}
	end_sample();
}

static void pcm_init(void)
{
	int i;
	outb(0x4F7,0);                    /* Chip off */
	outb(0x4F8,0xFF);                 /* All channels off */
	/* Page 0: the silence loop */
	put_byte(0);
	put_byte(0);
	put_byte(0xFF);
	gen_noise(SFX_CRUNCH,2600,2,60,180,256,200);
	gen_noise(SFX_STONE,2200,1,20,90,256,170);
	gen_tone(SFX_WOOD,2600,520,380,0,120,256,230);
	gen_noise(SFX_STEP,1000,3,80,0,256,120);
	gen_tone(SFX_PLACE,1800,150,90,0,300,256,230);
	gen_tone(SFX_HURT,4200,640,260,1,0,256,190);
	gen_noise(SFX_EXPLODE,14000,4,40,500,128,255);
	gen_noise(SFX_HISS,9000,0,4000,0,256,110);
	gen_tone(SFX_CLICK,300,1800,1600,1,60,256,140);
	for(i=0; i<8; ++i)
	{
		outb(0x4F7,0x40|i);
		outb(0x4F0,0);
		outb(0x4F1,0);
		outb(0x4F4,0);                /* Loop start: the silence */
		outb(0x4F5,0);
	}
	outb(0x4F7,0xC0);                 /* Chip on */
}

void sound_play(int id,int pitch,int vol,int pan)
{
	const SfxDef *d;
	int ch,fd,l,r;
	if(!g_sfxOn || id<0 || id>=NUM_SFX)
	{
		return;
	}
	d=&sfx[id];
	vol=vol*d->vol>>8;
	if(vol<=0)
	{
		return;
	}
	fd=d->pitch*8*pitch>>8;           /* 0800h * pitch/256 * sample pitch/256 */
	fd=CLAMP(fd,64,0xFFFF);
	pan=CLAMP(pan,-15,15);
	l=15-MAX(0,pan);
	r=15-MAX(0,-pan);
	ch=nextCh;
	nextCh=(nextCh+1)&7;
	outb(0x4F8,pcmOff|(1<<ch));       /* Stop the channel */
	outb(0x4F7,0xC0|ch);
	outb(0x4F0,vol);
	outb(0x4F1,(r<<4)|l);
	outb(0x4F2,fd&0xFF);
	outb(0x4F3,fd>>8);
	outb(0x4F6,d->start);
	pcmOff&=~(1<<ch);
	outb(0x4F8,pcmOff);               /* Start */
}

void sound_set_listener(int x,int y,int z,int yaw)
{
	lisX=x; lisY=y; lisZ=z; lisYaw=yaw;
}

void sound_play_at(int id,int pitch,int vol,int x,int y,int z)
{
	int dx=(x-lisX)>>8,dy=(y-lisY)>>8,dz=(z-lisZ)>>8;   /* 1/16 block */
	int d=isqrt((u32)(dx*dx+dy*dy+dz*dz)),pan=0;
	vol=vol*(256-d)>>8;               /* Silent at 16 blocks */
	if(vol<=0)
	{
		return;
	}
	if(d>8)
	{
		/* Projection on the listener's right axis (as in the renderer) */
		int side=(dx*fcos(lisYaw)-dz*fsin(lisYaw))>>14;
		pan=side*12/d;
	}
	sound_play(id,pitch,vol,pan);
}

/* ---------------- FM (YM2612) ----------------

   I/O 4D8h/4DAh: address/data for channels 1-3 and the common
   registers, 4DCh/4DEh for channels 4-6. */

static void fm_write(int part,int reg,int val)
{
	int port=part ? 0x4DC : 0x4D8,guard=200;
	while((inb(0x4D8)&0x80) && --guard);   /* Busy */
	outb(port,reg);
	outb(port+2,val);
}

/* Electric piano: algorithm 4 (two modulator-carrier pairs).  The first
   pair gives the warm body, the second (modulator at 7x, carrier at 2x,
   quicker decay) the bell-like attack.  Operator order in the register
   map: S1, S3, S2, S4.  Envelope rates: a decay rate d takes about
   14 s * 2^((18-2d)/4) for 96 dB, a release rate r the same at 4r+2. */
static const u8 epPatch[7][4]=
{
	{0x01,0x07,0x01,0x02},   /* 30h DT/MUL */
	{30,  40,  0,   14},     /* 40h TL (carriers get the note volume added) */
	{0x1F,0x1F,0x5F,0x5F},   /* 50h KS/AR */
	{10,  14,  9,   12},     /* 60h AM/D1R: body ~3 s to -30 dB, bell ~1 s */
	{5,   8,   5,   8},      /* 70h D2R */
	{0x88,0xF8,0xA8,0xC8},   /* 80h SL/RR: release ~0.6 s after key off */
	{0,   0,   0,   0},      /* 90h SSG-EG */
};

static void fm_set_patch(int ch)
{
	int part=ch/3,c=ch%3,r,s;
	for(r=0; r<7; ++r)
	{
		for(s=0; s<4; ++s)
		{
			fm_write(part,0x30+r*16+s*4+c,epPatch[r][s]);
		}
	}
	fm_write(part,0xB0+c,(2<<3)|4);   /* Feedback 2, algorithm 4 */
	fm_write(part,0xB4+c,0xC0);       /* Both speakers */
}

static void fm_key(int ch,int on)
{
	fm_write(0,0x28,(on ? 0xF0 : 0)|(ch%3)|((ch/3)<<2));
}

/* F-numbers of C4..B4 at block 4 (A4 = 1038 for 440 Hz on the TOWNS) */
static const u16 fnumTab[12]={617,654,693,734,778,824,873,925,980,1038,1100,1165};

static void fm_note(int ch,int midi,int vol)
{
	int part=ch/3,c=ch%3,oct=midi/12-1,f=fnumTab[midi%12];
	int tl=(127-vol)>>2;
	oct=CLAMP(oct,0,7);
	fm_key(ch,0);
	fm_write(part,0x48+c,epPatch[1][2]+tl);     /* Carrier S2 */
	fm_write(part,0x4C+c,epPatch[1][3]+tl);     /* Carrier S4 */
	fm_write(part,0xA4+c,(oct<<3)|(f>>8));
	fm_write(part,0xA0+c,f&0xFF);
	fm_key(ch,1);
}

/* ---------------- Music ----------------

   Two voices, one token per eighth note: a note (C D E F G A B, optional
   # or b, octave), "-" (the note rings on) or "." (nothing new).  Bars
   are separated by "|" for readability.  Each hand rotates over 3 FM
   channels, so a note keeps ringing until 3 later notes, like a piano
   with the sustain pedal. */

#define EIGHTH 36   /* ticks: about 83 quarter notes per minute */

static const char *const scoreLH[]=
{
	/* A */
	"F2 C3 A3 C4 E4 C4 A3 C3|F2 C3 A3 C4 E4 C4 A3 C3|A2 E3 G3 C4 E4 C4 G3 E3|A2 E3 G3 C4 E4 C4 G3 E3|"
	"D2 A2 F3 A3 C4 A3 F3 A2|D2 A2 F3 A3 C4 A3 F3 A2|Bb1 F2 D3 F3 A3 F3 D3 F2|C2 G2 F3 G3 E3 G3 C4 G2|",
	/* A again */
	"F2 C3 A3 C4 E4 C4 A3 C3|F2 C3 A3 C4 E4 C4 A3 C3|A2 E3 G3 C4 E4 C4 G3 E3|A2 E3 G3 C4 E4 C4 G3 E3|"
	"D2 A2 F3 A3 C4 A3 F3 A2|D2 A2 F3 A3 C4 A3 F3 A2|Bb1 F2 D3 F3 A3 F3 D3 F2|C2 G2 F3 G3 E3 G3 C4 G2|",
	/* B */
	"G2 D3 F3 Bb3 D4 Bb3 F3 D3|A2 E3 G3 C4 E4 C4 G3 E3|Bb1 F2 D3 F3 A3 F3 D3 F2|C2 G2 E3 G3 C4 G3 E3 G2|"
	"G2 D3 F3 Bb3 D4 Bb3 F3 D3|A2 E3 G3 C4 E4 C4 G3 E3|Bb1 F2 D3 F3 A3 F3 D3 F2|C2 G2 F3 G3 E3 G3 C4 G2|",
	/* A and ending */
	"F2 C3 A3 C4 E4 C4 A3 C3|F2 C3 A3 C4 E4 C4 A3 C3|A2 E3 G3 C4 E4 C4 G3 E3|A2 E3 G3 C4 E4 C4 G3 E3|"
	"D2 A2 F3 A3 C4 A3 F3 A2|D2 A2 F3 A3 C4 A3 F3 A2|Bb1 F2 D3 F3 A3 F3 D3 F2|C2 G2 F3 G3 E3 G3 C4 G2|"
	"F2 C3 A3 E4 . . . .|. . . . . . . .|",
};
static const char *const scoreRH[]=
{
	". . . . . . . .|. . . . C5 - A4 -|E5 - - - . . D5 C5|E5 - - - . . . .|"
	". . A4 - D5 - C5 -|A4 - - - F4 - G4 -|A4 - - - . . F5 -|E5 - - - . . . .|",
	". . . . C5 - F5 -|E5 - - - C5 - A4 -|E5 - - - G5 - E5 -|C5 - - - . . . .|"
	". . F5 - E5 - D5 -|C5 - - - A4 - C5 -|D5 - - - C5 - A4 -|G4 - - - . . . .|",
	". . D5 - F5 - G5 -|A5 - - - G5 - E5 -|F5 - - - D5 - . .|E5 - - - . . . .|"
	". . Bb4 - D5 - F5 -|E5 - - - C5 - . .|D5 - - - F5 - A4 -|G4 - - - . . . .|",
	". . . . . . . .|. . . . C5 - A4 -|E5 - - - . . D5 C5|E5 - - - . . . .|"
	". . A4 - D5 - C5 -|A4 - - - F4 - G4 -|A4 - - - . . F5 -|E5 - - - . . . .|"
	"F5 - - - - - - -|. . . . . . . .|",
};

#define MAX_EVENTS 640
static u16 evTick[MAX_EVENTS];
static u8 evNote[MAX_EVENTS],evVoice[MAX_EVENTS];
static int nEvents,songLen;

static volatile int musicPlaying;
static volatile int musicWait=-1;      /* Ticks until the next play; -1 never */
static int musicGap=6000;
static int musicTick,musicEv,rr[2];

/* Parse one voice into events (merged into tick order below) */
static void parse_voice(const char *const *parts,int nParts,int voice)
{
	int p,t=0;
	for(p=0; p<nParts; ++p)
	{
		const char *s=parts[p];
		while(*s)
		{
			if(' '==*s || '|'==*s)
			{
				++s;
				continue;
			}
			if('-'==*s || '.'==*s)
			{
				++s;
				t+=EIGHTH;
				continue;
			}
			{
				static const u8 semi[7]={9,11,0,2,4,5,7};   /* A..G */
				int n=semi[*s-'A'];
				++s;
				if('#'==*s) { ++n; ++s; }
				else if('b'==*s) { --n; ++s; }
				n+=(*s-'0'+1)*12;
				++s;
				if(nEvents<MAX_EVENTS)
				{
					evTick[nEvents]=t;
					evNote[nEvents]=n;
					evVoice[nEvents]=voice;
					++nEvents;
				}
				t+=EIGHTH;
			}
		}
	}
	songLen=MAX(songLen,t);
}

static void music_init(void)
{
	int i,j;
	parse_voice(scoreLH,ARRAY_LEN(scoreLH),0);
	parse_voice(scoreRH,ARRAY_LEN(scoreRH),1);
	/* Stable insertion sort by tick */
	for(i=1; i<nEvents; ++i)
	{
		u16 t=evTick[i];
		u8 n=evNote[i],v=evVoice[i];
		for(j=i; j>0 && evTick[j-1]>t; --j)
		{
			evTick[j]=evTick[j-1];
			evNote[j]=evNote[j-1];
			evVoice[j]=evVoice[j-1];
		}
		evTick[j]=t;
		evNote[j]=n;
		evVoice[j]=v;
	}
	fm_write(0,0x22,0);               /* LFO off */
	fm_write(0,0x27,0);               /* Timers off, normal channel 3 */
	fm_write(0,0x2B,0);               /* DAC off */
	for(i=0; i<6; ++i)
	{
		fm_key(i,0);
		fm_set_patch(i);
	}
}

static void music_all_off(void)
{
	int i;
	for(i=0; i<6; ++i)
	{
		fm_key(i,0);
	}
}

void music_schedule(int ticks)
{
	cli();
	if(!musicPlaying)
	{
		musicWait=ticks;
	}
	sti();
}

void music_set_gap(int minGap)
{
	musicGap=minGap;
}

void music_stop(void)
{
	cli();
	if(musicPlaying)
	{
		music_all_off();
	}
	musicPlaying=0;
	musicWait=-1;
	sti();
}

void sound_tick(void)
{
	if(!musicPlaying)
	{
		if(musicWait>0)
		{
			--musicWait;
		}
		else if(0==musicWait && g_musicOn)
		{
			musicWait=-1;
			musicPlaying=1;
			musicTick=0;
			musicEv=0;
		}
		return;
	}
	if(!g_musicOn)
	{
		music_all_off();
		musicPlaying=0;
		musicWait=musicGap;
		return;
	}
	while(musicEv<nEvents && evTick[musicEv]<=musicTick)
	{
		int v=evVoice[musicEv];
		int ch=v*3+rr[v];
		rr[v]=(rr[v]+1)%3;
		fm_note(ch,evNote[musicEv],v ? 112 : 84);   /* Melody over the accompaniment */
		++musicEv;
	}
	if(++musicTick>songLen+400)       /* Let the last chord ring out */
	{
		music_all_off();
		musicPlaying=0;
		musicWait=musicGap+(int)(((u32)g_ticks*2654435761u>>16)%(u32)(musicGap+1));
	}
}

void sound_init(void)
{
	outb(0x4D5,3);                    /* FM and PCM unmuted */
	outb(0x4EC,inb(0x4EC)|0x40);      /* Audio output on */
	pcm_init();
	music_init();
}
