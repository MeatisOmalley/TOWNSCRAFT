"""Independent Very Low experiments; applied after the established overlays."""
from build_imported import replace_once


def adapt(name, data, *, stream_radius=False, native_flat=False):
    if not ((stream_radius and name=='column_cache.inc') or (native_flat and name=='raster.c')):
        return data, []
    recipe=[]
    text=data.decode().replace('\r\n','\n')
    if b'\r\n' in data:
        recipe.append(dict(operation='replace',before='\r\n',after='\n',count=data.count(b'\r\n')))
    def one(before,after):
        nonlocal text
        text=replace_once(text,before,after,recipe)
    if name=='column_cache.inc':
        one('static int cacheBoot;', 'extern int dos_potato_mode;\nstatic int cacheBoot;')
        one('if(!world_column_ready(cx,cz) && d<bd) { bx=cx; bz=cz; bd=d; }',
            '/* Retain light reach, the 27-block spawn radius and body collision neighbors. */\n'
            '\t\t\t\tint reach=MAX(g_viewDist+CS,28);\n'
            '\t\t\t\tif(!cacheBoot && budget<=100 && dos_potato_mode && d>reach*reach &&\n'
            '\t\t\t\t   (!columnPin || !columnPin(cx,cz))) continue;\n'
            '\t\t\t\tif(!world_column_ready(cx,cz) && d<bd) { bx=cx; bz=cz; bd=d; }')
    else:
        one('else if(rplanar>1)\n', '''else if(rplanar==3)
			{
				/* Native 80x50 flat spans, stored in the existing planar target. */
				for(; s<end; ++s,row+=stride)
				{
					int xl=MAX(0,(left->x16+0x7FFF)>>16),xr=MIN(rw,(right->x16+0x7FFF)>>16);
					if(xr>xl) {
						BENCH_HOOK(if(!g_fastFlat) g_benchCnt[S_FLATPX]+=(xr-xl)*2);
						flat_fill(row+xl,xr-xl,flatWord);
						if(!(dos_very_low_rows[s*2]&1)) flat_fill(row+PLANAR_C+xl,xr-xl,flatWord);
						if(!(dos_very_low_rows[s*2+1]&2)) {
							flat_fill(row+PLANAR_ROW+xl,xr-xl,flatWord);
							if(!(dos_very_low_rows[s*2+1]&1)) flat_fill(row+PLANAR_ROW+PLANAR_C+xl,xr-xl,flatWord);
						}
					}
					left->x16+=left->dx; right->x16+=right->dx;
				}
			}
			else if(rplanar>1)
''')
        one('void raster_flat_poly(const RVert *v,int n,u8 color)\n{\n\tpoly_scan(v,n,NULL,0,color);\n}', '''void raster_flat_poly(const RVert *v,int n,u8 color)
{
	if(rplanar==2 && dos_potato_mode && n<=12) {
		RVert small[12];
		/* Match the original even logical sample centers, with finer edge rounding. */
		for(int i=0;i<n;++i) { small[i].x=(v[i].x+8)>>1; small[i].y=(v[i].y+8)>>1; }
		rw/=2; rh/=2; rstride*=2; rplanar=3;
		poly_scan(small,n,NULL,0,color);
		rw*=2; rh*=2; rstride/=2; rplanar=2;
		return;
	}
	poly_scan(v,n,NULL,0,color);
}''')
    return text.encode(),recipe
