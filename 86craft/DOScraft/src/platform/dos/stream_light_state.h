#define STREAM_LIGHT_MAGIC 0x314c4354u
#define STREAM_LIGHT_BASE 294977u
#define STREAM_LIGHT_SECTORS 13u
#define STREAM_LIGHT_BYTES 6400u
#define STREAM_LIGHT_CACHE 32
static int streamLightBaked,streamLightRead=-1,streamLightPrepared=-1,streamLightKind,streamLightSlot;
static u32 streamLightSum[4096],streamLightVersion[4096];
static u8 streamLightDirty[4096];
static u8 *streamLightScratch,*streamLightCache;
static short streamLightTags[STREAM_LIGHT_CACHE];
static u32 streamLightRevision[STREAM_LIGHT_CACHE],streamLightAge[STREAM_LIGHT_CACHE],streamLightClock;
u32 g_streamLightStats[4]; /* baked restores, RAM restores, recomputations, invalidations */
static void stream_light_header(const u32 *header);
static void stream_light_allocate(void);
static void stream_light_reset(void);
static int stream_light_quiesce(void);
static int stream_light_prepare(int cx,int cz);
static int stream_light_restore(int cx,int cz,int slot);
static int stream_light_clean_seams(int cx,int cz);
static void stream_light_invalidate(int x,int z);
