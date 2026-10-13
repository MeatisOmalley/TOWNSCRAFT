from pathlib import Path
import sys
import unittest
sys.path.insert(0,str(Path(__file__).resolve().parents[1]/'tools'))
from bake_large_world import noise
from terrain_biomes import sample,surface,vegetation,PLAINS,FOREST,DESERT,HIGHLANDS

class BiomeTests(unittest.TestCase):
    def test_seed_4242_has_all_four_land_regions_and_bounded_heights(self):
        regions=set()
        for z in range(0,1024,16):
            for x in range(0,1024,16):
                h,b=sample(x,z,4242,noise)
                self.assertTrue(4<=h<=38)
                if h>21: regions.add(b)
        self.assertEqual(regions,{PLAINS,FOREST,DESERT,HIGHLANDS})

    def test_field_is_repeatable_and_neighbors_have_bounded_steps(self):
        for z in range(20,1000,39):
            for x in range(20,1000,37):
                a=sample(x,z,4242,noise)
                self.assertEqual(a,sample(x,z,4242,noise))
                for dx,dz in ((1,0),(0,1)):
                    h,b=sample(x+dx,z+dz,4242,noise)
                    self.assertLessEqual(abs(h-a[0]),8)

    def test_climate_changes_surface_region_without_changing_topography(self):
        heights=[];regions=[]
        for heat,moisture in ((130,130),(180,100),(130,180)):
            def climate(x,z,shift,seed):
                return heat if seed==4242+40 else moisture if seed==4242+41 else 150
            h,b=sample(512,512,4242,climate);heights.append(h);regions.append(b)
        self.assertEqual(regions,[PLAINS,DESERT,FOREST])
        self.assertEqual(len(set(heights)),1)

    def test_existing_materials_and_bounded_vegetation(self):
        self.assertEqual(surface(24,DESERT,0,128),8)
        self.assertEqual(surface(30,HIGHLANDS,4,128),1)
        self.assertEqual(surface(30,HIGHLANDS,4,230),22)
        self.assertEqual(surface(20,FOREST,0,128),8)
        self.assertEqual(surface(24,PLAINS,0,128),2)
        self.assertEqual(vegetation(DESERT),(0,0,0))
        for b in range(4): self.assertLessEqual(vegetation(b)[0],45)

if __name__=='__main__':unittest.main()
