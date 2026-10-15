"""Read-only sampled maps for reviewing the offline terrain design."""
import argparse
import json
from pathlib import Path
from PIL import Image,ImageDraw
from bake_large_world import height,noise
from terrain_biomes import sample,NAMES,COLORS

def preview(seed,output):
    output=Path(output);output.mkdir(parents=True,exist_ok=True)
    side=256; hs=[];bs=[];old=[];counts=dict.fromkeys(NAMES,0)
    for z in range(side):
        for x in range(side):
            h,b=sample(x*4,z*4,seed,noise);hs.append(h);bs.append(b);old.append(height(x*4,z*4,seed))
            if h>21: counts[NAMES[b]]+=1
    def terrain(heights,biomes):
        pixels=[]
        for z in range(side):
            for x in range(side):
                i=z*side+x;h=heights[i]
                if h<20: base=(44,82,127)
                elif h<=21: base=(200,184,133)
                else: base=COLORS[biomes[i]]
                slope=heights[z*side+max(0,x-1)]-heights[max(0,z-1)*side+x]
                brightness=max(.6,min(1.3,1+slope*.07+(h-22)*.006))
                pixels.append(tuple(min(255,int(c*brightness)) for c in base))
        image=Image.new('RGB',(side,side));image.putdata(pixels)
        return image.resize((512,512),Image.Resampling.NEAREST)
    sheet=Image.new('RGB',(1060,572),(26,29,33));d=ImageDraw.Draw(sheet)
    d.text((18,12),'Existing seed 4242',(230,230,230));d.text((542,12),'Biome terrain candidate - seed 4242',(230,230,230))
    sheet.paste(terrain(old,[0]*len(old)),(18,34));sheet.paste(terrain(hs,bs),(542,34))
    d.text((18,551),'Sampled ground heights; tree canopies and caves are not shown.',(190,195,200))
    sheet.save(output/'terrain-comparison.png')
    image=Image.new('RGB',(side,side));image.putdata([COLORS[b] if h>21 else (44,82,127) for h,b in zip(hs,bs)])
    image.resize((512,512),Image.Resampling.NEAREST).save(output/'biomes.png')
    report={'seed':seed,'sample_step':4,'land_samples':counts,'height_min':min(hs),'height_max':max(hs)}
    (output/'sampled-geography.json').write_text(json.dumps(report,indent=2)+'\n')
    print(json.dumps(report))

if __name__=='__main__':
    p=argparse.ArgumentParser(description=__doc__);p.add_argument('--seed',type=int,default=4242)
    p.add_argument('--output',type=Path,required=True);a=p.parse_args();preview(a.seed,a.output)
