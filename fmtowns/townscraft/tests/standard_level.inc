/* Benchmark level v1. Coordinates, not the historical generator, define it.
   Deliberately includes opaque/transparent/model faces, a cave, shafts,
   water, trees, chunk seams and a clear walking lane at z=48, y=24. */
u32 stdTerrainHash;
static u8 standard_block(int x,int y,int z)
{
	u8 b=y==0 ? B_BEDROCK : y<23 ? B_STONE : y==23 ? B_GRASS : B_AIR;
	if(x>=24 && x<=64 && z>=42 && z<=54 && y>=10 && y<=20) b=B_AIR;
	if(((x>=38 && x<=40 && z>=51 && z<=53) ||
	    (x>=57 && x<=59 && z>=44 && z<=46)) && y>=10 && y<24) b=B_AIR;
	if(x>=52 && x<=60 && z>=51 && z<=56 && y>=22 && y<24) b=B_WATER;
	if(x>=36 && x<=64 && x%8==4 && (z==40 || z==57) && y>=24 && y<31) b=B_LOG;
	if(x>=34 && x<=66 && x%8>=2 && x%8<=6 &&
	   ((z>=38 && z<=42) || (z>=55 && z<=59)) && y>=29 && y<=32) b=B_LEAVES;
	if(x>=42 && x<=50 && z==55 && y>=24 && y<=28) b=B_GLASS;
	if(x>=32 && x<=64 && x%4==0 && z==45 && y==24) b=B_FLOWER;
	if(x>=28 && x<=64 && x%8==0 && z==43 && y==11) b=B_TORCH;
	return b;
}
void world_generate(u32 seed)
{
	trackLightDirty=0;
	STD_GENERATION_BEGIN;
	stdTerrainHash=2166136261u;
	for(int z=0;z<g_W;++z) for(int x=0;x<g_W;++x)
	{
		for(int y=0;y<WH;++y)
		{
			u8 b=standard_block(x,y,z);
			g_blocks[widx(x,y,z)]=b;
			stdTerrainHash=(stdTerrainHash^b)*16777619u;
		}
		update_height(x,z);
	}
	g_spawnX=32; g_spawnY=24; g_spawnZ=48;
	STD_GENERATION_END;
	trackLightDirty=1;
}
