"""Fail-closed recipes and exact reversal of the renderer's stable merge."""
from pathlib import Path
import sys
import unittest
ROOT=Path(__file__).resolve().parents[1]
sys.path.insert(0,str(ROOT/'tools'))
import build_imported as imported
import dos_sources


class FrontendTests(unittest.TestCase):
    def test_recipes_replay_and_vendor_unchanged(self):
        for name in imported.SELECTED:
            data=(imported.VENDOR/'src'/name).read_bytes()
            self.assertEqual(dos_sources.adapt(name,data),(data,[]))
            result,recipe=dos_sources.adapt(name,data,front_to_back=True)
            replay=data
            for step in recipe:
                old,new=step['before'].encode(),step['after'].encode()
                self.assertEqual(replay.count(old),step['count'])
                replay=replay.replace(old,new)
            self.assertEqual(replay,result)
            if name not in ('render.c','raster.c'): self.assertFalse(recipe)
        for name in ('render.c','raster.c'):
            with self.assertRaises(ValueError):
                dos_sources.adapt(name,b'unexpected upstream change',front_to_back=True)
        with self.assertRaises(ValueError):
            dos_sources.adapt('render.c',b'',packed_raster=True,front_to_back=True)

    def test_tie_and_internal_primitive_order(self):
        # Exhaustive stable equal-key merges, including missing/empty lists.
        from itertools import combinations_with_replacement
        lists=[list(x) for n in range(5) for x in combinations_with_replacement(range(4),n)]
        for terrain in lists:
            for entities in lists:
                old=[]; k=len(terrain)-1; e=len(entities)-1
                while k>=0 or e>=0:
                    if e>=0 and (k<0 or entities[e]>=terrain[k]):
                        old.extend(('e',e,p) for p in range(3)); e-=1
                    else:
                        old.extend(('t',k,p) for p in range(4)); k-=1
                new=[]; k=e=0
                while k<len(terrain) or e<len(entities):
                    if e<len(entities) and (k>=len(terrain) or entities[e]<terrain[k]):
                        new.extend(('e',e,p) for p in reversed(range(3))); e+=1
                    else:
                        new.extend(('t',k,p) for p in reversed(range(4))); k+=1
                self.assertEqual(new,old[::-1])

    def test_real_source_fallback_and_overlay_boundaries(self):
        text,_=dos_sources.adapt('render.c',(imported.VENDOR/'src/render.c').read_bytes(),front_to_back=True)
        text=text.decode()
        self.assertIn('dos_f2b_capture_replay(&f2bCapture,1,f2b_emit,NULL)!=1',text)
        self.assertIn('draw_sky(env); draw_clouds(env);\n\t\t\treturn 0;',text)
        self.assertIn('if(!frontSuccess',text)
        self.assertIn('if(occQuery && (it->a&ITEM_OCCLUDED))',text)  # original fallback intact
        self.assertLess(text.index('dos_f2b_end();\n\treturn 1;'),text.index('void render_frame'))
        self.assertIn('dos_f2b_verify_failures',text)


if __name__=='__main__': unittest.main()
