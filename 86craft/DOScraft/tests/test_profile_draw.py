from pathlib import Path
import sys
import unittest
ROOT=Path(__file__).resolve().parents[1]
sys.path.insert(0,str(ROOT/'tools'))
from profile_draw import analyze,symbolize,symbols_from_nm


class ProfileTests(unittest.TestCase):
    def test_code_symbols_only_and_boundary_attribution(self):
        symbols=symbols_from_nm('00001000 T _first\n00002000 t _second\n00001500 D _data\n U _external\n')
        self.assertEqual(symbols,[(4096,'_first'),(8192,'_second')])
        rows=symbolize([4096,8191,8192,8193,1000],symbols)
        self.assertEqual({r['symbol']:r['samples'] for r in rows},
            {'_first':2,'_second':2,'<before-text>':1})
        self.assertEqual(sum(r['sample_percent'] for r in rows),100)
        self.assertEqual(symbolize([],symbols),[])
        with self.assertRaises(ValueError): symbols_from_nm('00001200 D _not_code')
        for bad in (0,-1,2**32):
            with self.assertRaises(ValueError): symbolize([bad],symbols)

    def test_phase_ranges_and_diagnostic_guard(self):
        report={'results':[{'header':{'result':'PASS','draw_profiling':'TIMINGS_DIAGNOSTIC_ONLY',
            'draw_eip':'1000,2000,2001'},'scenes':[
                {'name':'walk','draw_sample_begin':'0','draw_sample_end':'1'},
                {'name':'house','draw_sample_begin':'1','draw_sample_end':'3'}]}]}
        symbols=[(4096,'a'),(8192,'b')]
        rows=analyze(report,symbols)
        self.assertEqual([r['sample_count'] for r in rows[0]['scenes']],[1,2])
        report['results'][0]['scenes'][1]['draw_sample_begin']='2'
        with self.assertRaises(ValueError): analyze(report,symbols)
        report['results'][0]['scenes']=[]
        self.assertEqual(analyze(report,symbols)[0]['sample_count'],3)
        report['results'][0]['header']['draw_profiling']=''
        with self.assertRaises(ValueError): analyze(report,symbols)


if __name__=='__main__': unittest.main()
