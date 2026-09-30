#ifndef RENDER_H
#define RENDER_H
#include "common.h"

/* Positions are in 1/256 block units ("units"). */
typedef struct
{
	int x,y,z;
	int yaw,pitch;      /* 0..1023 */
} Camera;

/* Entity model box.  Local coordinates in 1/16 block relative to the
   entity origin (feet center).  The box is rotated by pitch about its pivot
   (local X axis), then by the entity yaw. */
typedef struct
{
	int ox,oy,oz;       /* Entity origin, units */
	int yaw,pitch;
	s8 px,py,pz;        /* Pivot */
	s8 x0,y0,z0,x1,y1,z1;
	u8 tex[6];          /* -X,+X,-Y,+Y,-Z,+Z */
	u8 light;           /* 0..15 */
	u8 flash;           /* Draw red (hurt) */
	u16 hw,h;           /* Entity bounding box (units), for draw ordering */
} MBox;

typedef struct
{
	int skyDarken;      /* 0 (noon) .. 11 (midnight) subtracted from sky light */
	int sunAngle;       /* 0..1023, 0 = sunrise (east), 256 = noon */
	u8 skyColor,fogColor;
	int targetValid,tx,ty,tz;
	int underwater;
	int starBrightness; /* 0..15 */
} RenderEnv;

extern Camera g_cam;
extern int g_viewDist;      /* Blocks */
extern int g_renderScale;   /* 1 = 320x240, 2 = 160x120 doubled */
extern u32 g_statFaces,g_statItems;
extern u32 g_prof[8];
extern int g_flatLOD;
extern int g_flatDist;

void render_init(void);
u32 render_mem_needed(int worldWidth);
void render_frame(u8 *fb,const RenderEnv *env);
MBox *render_add_box(void);
void render_clear_boxes(void);
int face_light_level(int lightByte,int dir,int skyDarken);

#endif
