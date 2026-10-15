"""Painter sort: build the three radix histograms in one pass.

sort_items() is a stable three-pass, 7-bit LSD radix sort. It filled the
order array with the identity permutation and then recounted keys through
that indirection on every pass. All three digit histograms are now counted
in one pass over itemKey, and the first scatter reads indices directly.
Bucket offsets and the stable scatter are unchanged, so the permutation is
identical.
"""
from build_imported import replace_once

BEFORE = '''static void sort_items(void)
{
	static u16 cnt[128];
	int pass,i;
	u16 *src=order,*dst=order2,*t;
	for(i=0; i<nItems; ++i)
	{
		src[i]=i;
	}
	for(pass=0; pass<3; ++pass)
	{
		int shift=pass*7;
		u32 sum=0;
		memset(cnt,0,sizeof(cnt));
		for(i=0; i<nItems; ++i)
		{
			++cnt[(itemKey[src[i]]>>shift)&127];
		}
		for(i=0; i<128; ++i)
		{
			u32 c=cnt[i];
			cnt[i]=sum;
			sum+=c;
		}
		for(i=0; i<nItems; ++i)
		{
			u16 k=src[i];
			dst[cnt[(itemKey[k]>>shift)&127]++]=k;
		}
		t=src; src=dst; dst=t;
	}
'''

AFTER = '''static void sort_items(void)
{
	/* Histograms do not depend on the order: count all digits at once. */
	static u16 cnt[3][128];
	int pass,i;
	u16 *src=order,*dst=order2,*t;
	memset(cnt,0,sizeof(cnt));
	for(i=0; i<nItems; ++i)
	{
		u32 key=itemKey[i];
		++cnt[0][key&127];
		++cnt[1][(key>>7)&127];
		++cnt[2][(key>>14)&127];
	}
	for(pass=0; pass<3; ++pass)
	{
		int shift=pass*7;
		u16 *c=cnt[pass];
		u32 sum=0;
		for(i=0; i<128; ++i)
		{
			u32 n=c[i];
			c[i]=sum;
			sum+=n;
		}
		if(0==pass)
		{
			/* The first pass reads the identity order directly. */
			for(i=0; i<nItems; ++i)
			{
				dst[c[itemKey[i]&127]++]=i;
			}
		}
		else
		{
			for(i=0; i<nItems; ++i)
			{
				u16 k=src[i];
				dst[c[(itemKey[k]>>shift)&127]++]=k;
			}
		}
		t=src; src=dst; dst=t;
	}
'''


def adapt(name, data):
    if name != 'render.c':
        return data, []
    text = data.decode('utf-8')
    recipe = []
    if '\r\n' in text:
        recipe.append(dict(operation='replace', before='\r\n', after='\n', count=text.count('\r\n')))
        text = text.replace('\r\n', '\n')
    text = replace_once(text, BEFORE, AFTER, recipe)
    return text.encode('utf-8'), recipe
