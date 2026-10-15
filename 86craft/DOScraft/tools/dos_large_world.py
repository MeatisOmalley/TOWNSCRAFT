"""Opt-in source groundwork for 1024-block disk-backed worlds.

The main build keeps the established 256-block storage format until the
large-world medium, save format, and guest acceptance are complete.
"""

from build_imported import replace_once, lf_text


def adapt(name, data):
    if name not in ('world.c', 'world.h', 'column_store.inc', 'column_hdd.inc',
                    'mobs.c', 'inventory.h', 'render.c'):
        return data, []
    recipe = []
    text = lf_text(data, recipe)

    def one(before, after):
        nonlocal text
        text = replace_once(text, before, after, recipe)

    if name == 'world.c':
        one('static short columnMapStorage[256],columnCoordsStorage[50];',
            'static short columnMapStorage[4096],columnCoordsStorage[50];')
        one('g_zOff=heap_alloc_low(sizeof(int)*256);',
            'g_zOff=heap_alloc_low(sizeof(int)*g_W);')
        one('static int terrainActive,terrainBlocking;',
            'static int terrainActive,terrainBlocking;\n'
            'static int terrainLarge,terrainV2,terrainV3;\n'
            'static u32 terrainSeed,terrainBaseSum[4096];\n'
            'static int terrainSpawnX,terrainSpawnZ;\n'
            '/* V2 medium (tools/convert_large_v2.py): packed base columns and a\n'
            '   pool of edit slot pairs assigned on a column\'s first edit. */\n'
            'static u32 terrainBaseOff[4096],terrainPoolSector,terrainPoolPairs;\n'
            'static u16 terrainBaseLen[4096],terrainPair[4096];\n'
            'static u8 terrainPairUsed[512];\n'
            '/* V3 medium (tools/convert_large_v3.py): the same packed base and an\n'
            '   edit log [terrainLogFirst,terrainLogEnd) in sectors; terrainV2 is set too. */\n'
            'static u32 terrainLogFirst,terrainLogEnd;\n'
            'static void large_log_reset(void);\n'
            'static int large_log_alloc(int id,u32 len,u32 *off);\n'
            'static u32 large_column_offset(int id,int slot);\n'
            'static int large_column_slot(int id,u32 off);\n'
            'static int large_pool_assign(int id);\n'
            '/* V3 per-block edits (large_world.inc). */\n'
            'static void large_delta_note(int id,u32 cell,u8 b);\n'
            'static void large_delta_apply(int id,u8 *dst);\n'
            'static int large_delta_keep(int id);\n'
            'static int large_column_edited(int id);')
        one('\tif(terrainActive && g_columnMap) ++terrainVersions[(z/CS)*g_NC+x/CS];\n',
            '\tif(terrainActive && g_columnMap) ++terrainVersions[(z/CS)*g_NC+x/CS];\n'
            '\tif(terrainV3 && g_columnMap) large_delta_note((z/CS)*g_NC+x/CS,i%COLUMN_CELLS,b);\n')
        one('\tcolumnCapacity=(g_ramMB<=2 ? 128 : (g_ramMB<=4 ? 384 : 1024))*1024u;\n'
            '\tcolumnArena=heap_alloc_high(columnCapacity);\n'
            '\tcolumnScratch=heap_alloc_low(COLUMN_CELLS);\n'
            '\tcolumnEncodeScratch=heap_alloc_low(COLUMN_CELLS);\n'
            '\tterrain_init();',
            '\tcolumnScratch=heap_alloc_low(COLUMN_CELLS);\n'
            '\tcolumnEncodeScratch=heap_alloc_low(COLUMN_CELLS);\n'
            '\tterrain_init();\n'
            '\tif(terrainLarge) { g_W=1024; g_NC=64; strideZ=g_W*WH; }\n'
            '\tcolumnCapacity=terrainLarge ? 64u*1024u :\n'
            '\t\t(g_ramMB<=2 ? 128 : (g_ramMB<=4 ? 384 : 1024))*1024u;\n'
            '\tcolumnArena=heap_alloc_high(columnCapacity);')
        one('\tif(cacheAllocated) generation_begin();\n\tgenSeed=seed;',
            '\tif(terrainLarge)\n\t{\n'
            '\t\tworld_load_prebaked();\n'
            '\t\tprogress(100);\n\t\treturn;\n\t}\n'
            '\tif(cacheAllocated) generation_begin();\n\tgenSeed=seed;')
        one('#include "column_cache.inc"',
            '#include "column_cache.inc"\n'
            '/* Large-world routines follow the cache definitions they call. */\n'
            '#include "large_world.inc"')
    elif name == 'world.h':
        one('int world_save_columns(void (*put)(u32),u32 (*position)(void));',
            'int world_save_columns(void (*put)(u32),u32 (*position)(void));\n'
            'int world_save_large_columns(void (*put)(u32));\n'
            'int world_load_large_columns(u32 (*get)(void),u32 version);\n'
            'void world_commit_large_columns(void);\n'
            'int world_verify_large_travel(int x,int z);')
    elif name == 'column_store.inc':
        one('static ColumnRecord columnRecords[256];',
            'static ColumnRecord columnRecords[4096];')
        one('int done[256]={0},n;',
            'static u8 done[4096];\n\tint n;\n\tmemset(done,0,sizeof(done));')
        one('static void column_restore_valid(int cx,int cz,u8 *dst)\n{\n',
            'static void column_restore_record(int cx,int cz,u8 *dst);\n'
            '/* The stored record, then the column\'s per-block edits (V3). */\n'
            'static void column_restore_valid(int cx,int cz,u8 *dst)\n{\n'
            '\tcolumn_restore_record(cx,cz,dst);\n'
            '\tlarge_delta_apply(cz*g_NC+cx,dst);\n'
            '}\n'
            'static void column_restore_record(int cx,int cz,u8 *dst)\n{\n')
    elif name == 'column_hdd.inc':
        one('#define TERRAIN_COLUMNS 256', '#define TERRAIN_COLUMNS 4096')
        one('hdd_sector_count()<1+TERRAIN_COLUMNS*TERRAIN_SECTORS*2',
            'hdd_sector_count()<1+(u32)(g_NC*g_NC)*TERRAIN_SECTORS*2')
        one('((u32 *)columnScratch)[5]!=TERRAIN_COLUMNS',
            '((u32 *)columnScratch)[5]!=(u32)(g_NC*g_NC)')
        one('static ColumnRecord terrainPending;',
            'static ColumnRecord terrainPending;\n'
            'static u8 terrainPinSlot[TERRAIN_COLUMNS];')
        one('\tif(r->disk==TERRAIN_DISK && terrainSavedVersions[id]==terrainVersions[id]) return 1;\n',
            '\tif(r->disk==TERRAIN_DISK && terrainSavedVersions[id]==terrainVersions[id]) return 1;\n'
            '\t/* Per-block edits stay in RAM until a save stores them. */\n'
            '\tif(large_delta_keep(id)) { terrainSavedVersions[id]=terrainVersions[id]; return 1; }\n')
        begin = text.index('static int terrain_init(void)\n{')
        end = text.index('\nstatic void terrain_cache_allocate(void)', begin)
        one(text[begin:end], '''static int terrain_init(void)
{
	static const char legacyMagic[]="TSC-TERRAIN-TEMP";
	static const char largeMagic[]="DOSCRAFT-1024-V1";
	static const char compactMagic[]="DOSCRAFT-1024-V2";
	static const char logMagic[]="DOSCRAFT-1024-V3";
	terrainActive=terrainLarge=terrainV2=terrainV3=0;
	if(!hdd_init() || hdd_transfer_sync(0,1,0,columnScratch)!=1) return 0;
	if(!memcmp(columnScratch,largeMagic,16) || !memcmp(columnScratch,compactMagic,16) ||
	   !memcmp(columnScratch,logMagic,16))
	{
		/* Index and appendix readers reuse columnScratch; keep the header. */
		static u32 headerCopy[128];
		u32 *header=headerCopy;
		memcpy(headerCopy,columnScratch,512);
		terrainV3=!memcmp(columnScratch,logMagic,16);
		terrainV2=terrainV3 || !memcmp(columnScratch,compactMagic,16);
		if(header[4]!=(terrainV3 ? 3u : terrainV2 ? 2u : 1u) || header[5]!=1024 || header[6]!=TERRAIN_COLUMNS ||
		   header[7]!=TERRAIN_SECTORS || header[8]!=33 ||
		   hdd_sector_count()<(terrainV2 ? header[35] : 33u+TERRAIN_COLUMNS*TERRAIN_SECTORS*3u))
			fatal("Invalid 1024-world HDD medium");
		terrainSeed=header[9]; terrainSpawnX=header[10]; terrainSpawnZ=header[11];
		u32 indexSum=header[12];
		if(terrainSpawnX<1 || terrainSpawnX>=1023 ||
		   terrainSpawnZ<1 || terrainSpawnZ>=1023)
			fatal("Invalid 1024-world spawn");
		/* Optional appendix headers. */
		if(terrainV2)
		{
			/* Index: per column u32 byte offset, length and sum (12 bytes). */
			u32 baseSector=header[30],indexSector=header[31],sum=2166136261u;
			terrainPoolSector=header[33]; terrainPoolPairs=terrainV3 ? 0 : header[34];
			terrainLogFirst=terrainV3 ? header[33] : 0; terrainLogEnd=terrainV3 ? header[35] : 0;
			if(indexSector!=1 || baseSector<indexSector+192 || terrainPoolSector<baseSector ||
			   (terrainV3 ? header[34]<2048 || header[34]>65536 || header[35]!=header[33]+header[34] :
			                terrainPoolPairs>4096 ||
			                header[35]!=terrainPoolSector+terrainPoolPairs*2u*TERRAIN_SECTORS))
				fatal("Invalid 1024-world HDD medium");
			for(int sector=0; sector<96; sector+=24)
			{
				if(hdd_transfer_sync(indexSector+sector,24,0,columnScratch)!=1)
					fatal("Cannot read 1024-world index");
				for(u32 i=0; i<24*512; ++i) sum=(sum^columnScratch[i])*16777619u;
				for(int e=0; e<1024; ++e)
				{
					const u32 *entry=(const u32 *)columnScratch+e*3;
					int id=sector/24*1024+e;
					if(entry[0]%512 || entry[0]/512<baseSector || !entry[1] ||
					   entry[1]>COLUMN_CELLS || entry[0]+entry[1]>terrainPoolSector*512u)
						fatal("Corrupt 1024-world index");
					terrainBaseOff[id]=entry[0]; terrainBaseLen[id]=entry[1];
					terrainBaseSum[id]=entry[2];
				}
			}
			if(sum!=indexSum) fatal("Corrupt 1024-world index");
		}
		else
		{
			for(int sector=0; sector<32; sector+=24)
			{
				int count=MIN(24,32-sector);
				if(hdd_transfer_sync(1+sector,count,0,columnScratch)!=1)
					fatal("Cannot read 1024-world index");
				memcpy((u8 *)terrainBaseSum+sector*512,columnScratch,count*512);
			}
			if(column_checksum((u8 *)terrainBaseSum,sizeof(terrainBaseSum))!=indexSum)
				fatal("Corrupt 1024-world index");
		}
		terrainLarge=1;
	}
	else
	{
		if(hdd_sector_count()<1+(u32)(g_NC*g_NC)*TERRAIN_SECTORS*2 ||
		   memcmp(columnScratch,legacyMagic,16) || ((u32 *)columnScratch)[4]!=1 ||
		   ((u32 *)columnScratch)[5]!=(u32)(g_NC*g_NC) ||
		   ((u32 *)columnScratch)[6]!=TERRAIN_SECTORS) return 0;
		for(int i=28; i<512; ++i) if(columnScratch[i]) return 0;
	}
	terrainWriteBuffer=heap_alloc_high(COLUMN_CELLS);
	terrainCacheCount=0; terrainReadCache=NULL;
	terrainActive=1;
	terrain_reset();
	return 1;
}''')
        one('\tterrainClock=0;\n}',
            '\tterrainClock=0;\n\tmemset(terrainPinSlot,0,sizeof(terrainPinSlot));\n'
            '\tmemset(terrainPair,0xFF,sizeof(terrainPair));\n'
            '\tmemset(terrainPairUsed,0,sizeof(terrainPairUsed));\n'
            '\tlarge_log_reset();\n}')
        one('u32 first=(1+id*TERRAIN_SECTORS*2)*512u;\n'
            '\tterrainPending.off=r->disk==TERRAIN_DISK && r->off==first ? first+COLUMN_CELLS : first;',
            'u32 first=(terrainLarge ? 33u+id*TERRAIN_SECTORS*3u : 1u+id*TERRAIN_SECTORS*2u)*512u;\n'
            '\tif(terrainV3)\n\t{\n'
            '\t\tif(!large_log_alloc(id,terrainPending.len,&terrainPending.off)) { streamError=2; return 0; }\n'
            '\t}\n\telse if(terrainV2)\n\t{\n'
            '\t\tint active=large_column_slot(id,r->off);\n'
            '\t\tint next=active==1 ? 2 : 1;\n'
            '\t\tif(next==terrainPinSlot[id]) next=active;\n'
            '\t\tif(next==0) next=1;\n'
            '\t\tif(!large_pool_assign(id)) { streamError=2; return 0; }\n'
            '\t\tterrainPending.off=large_column_offset(id,next);\n'
            '\t}\n\telse if(terrainLarge)\n\t{\n'
            '\t\tint active=(r->off-first)/COLUMN_CELLS;\n'
            '\t\tint next=active==1 ? 2 : 1;\n'
            '\t\tif(next==terrainPinSlot[id]) next=active;\n'
            '\t\tif(next==0) next=1;\n'
            '\t\tterrainPending.off=first+next*COLUMN_CELLS;\n'
            '\t}\n\telse\n'
            '\t\tterrainPending.off=r->disk==TERRAIN_DISK && r->off==first ? first+COLUMN_CELLS : first;')
    elif name == 'mobs.c':
        one('static u8 regionVisited[256];', 'static u8 regionVisited[4096];')
    elif name == 'inventory.h':
        one('u8 used,x,y,z;', 'u8 used,y;\n\tu16 x,z;')
    elif name == 'render.c':
        one('   a: x | z<<8 | y<<16 | dir<<22 | kind<<25',
            '   a: x (10 bits) | z<<10 | y<<20 | dir<<26 | kind<<29')
        one('u32 a=(u32)x|((u32)z<<8)|((u32)y<<16)|((u32)dir<<22)|((u32)IK_QUAD<<25);',
            'u32 a=(u32)x|((u32)z<<10)|((u32)y<<20)|((u32)dir<<26)|((u32)IK_QUAD<<29);')
        one('a+((u32)i<<8)', 'a+((u32)i<<10)')
        one('(u32)x|((u32)z<<8)|((u32)y<<16)|((u32)IK_MODEL<<25)',
            '(u32)x|((u32)z<<10)|((u32)y<<20)|((u32)IK_MODEL<<29)')
        one('if(((it->a>>25)&3)!=IK_QUAD)',
            'if(((it->a>>29)&3)!=IK_QUAD)')
        one('draw_quad(it->a&255,(it->a>>16)&63,(it->a>>8)&255,(it->a>>22)&7,',
            'draw_quad(it->a&1023,(it->a>>20)&63,(it->a>>10)&1023,(it->a>>26)&7,')
        one('kind=(it->a>>25)&3;', 'kind=(it->a>>29)&3;')
        one('int x=it->a&255,z=(it->a>>8)&255,y=(it->a>>16)&63;',
            'int x=it->a&1023,z=(it->a>>10)&1023,y=(it->a>>20)&63;')
        one('draw_quad(x,y,z,(it->a>>22)&7,',
            'draw_quad(x,y,z,(it->a>>26)&7,')
    return text.encode('utf-8'), recipe


def adapt_save_codec(text, recipe):
    """Store only the 1024-world HDD directory in the checked save bank."""
    def one(before, after):
        nonlocal text
        text = replace_once(text, before, after, recipe)

    # Version 6: version 5's layout with 6-bit block IDs and items from 64.
    # Version 7 (written) adds per-block edits; 6 still loads. Older large
    # saves hold the previous item IDs and are never loaded.
    one('if(!streamOk || get32()!=SAVE_MAGIC || get32()!=4) continue;',
        'if(!streamOk || get32()!=SAVE_MAGIC || get32()-6u>1u) continue; /* 6, or 7 with edits */')
    one('(version=get32())<1 || version>SAVE_VERSION',
        '((version=get32())!=SAVE_VERSION && version!=6)')
    one('static void put_chests(void)', 'static void put_chests(int version)')
    one('put8(c->x); put8(c->y); put8(c->z);',
        'if(version>=5) { put32(c->x); put8(c->y); put32(c->z); }\n'
        '\t\t\telse { put8(c->x); put8(c->y); put8(c->z); }')
    one('int ok,version=world_cache_active() && g_W<256 ? SAVE_VERSION : 3;',
        'int ok,version=g_W==1024 ? SAVE_VERSION : (world_cache_active() && g_W<256 ? 4 : 3);')
    one('if(!world_backing_idle()) return SAVE_DISK_ERROR;\n\tworld_set_backing_reader(save_column_read);',
        'if(!world_backing_idle()) return SAVE_DISK_ERROR;\n'
        '\tif(version>=5 && !world_store_flush()) return SAVE_DISK_ERROR;\n'
        '\tworld_set_backing_reader(save_column_read);')
    one('if(version>=4)\n\t{\n\t\tif(!writeBackup)',
        'if(version>=5)\n\t{\n'
        '\t\tif(!world_save_large_columns(put32)) streamOk=0;\n'
        '\t}\n\telse if(version>=4)\n\t{\n\t\tif(!writeBackup)')
    one('\tput_chests();', '\tput_chests(version);')
    one('if(ok && version>=4) world_commit_columns();',
        'if(ok && version>=5) world_commit_large_columns();\n'
        '\telse if(ok && version>=4) world_commit_columns();')
    one('if(version>=4)\n\t{\n\t\tworld_load_position(g_player.body.x/FU,g_player.body.z/FU);\n'
        '\t\tif(!world_load_columns(get32,stream_position)) streamOk=0;\n\t}',
        'if(version>=4)\n\t{\n'
        '\t\tworld_load_position(g_player.body.x/FU,g_player.body.z/FU);\n'
        '\t\tif(version>=5)\n\t\t{ if(!world_load_large_columns(get32,version)) streamOk=0; }\n'
        '\t\telse if(!world_load_columns(get32,stream_position)) streamOk=0;\n\t}')
    one('ch->x=get8(); ch->y=get8(); ch->z=get8();',
        'if(version>=5) { ch->x=get32(); ch->y=get8(); ch->z=get32(); }\n'
        '\t\t\telse { ch->x=get8(); ch->y=get8(); ch->z=get8(); }')
    return text
