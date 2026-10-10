"""Experimental smoke recipes remain isolated; no real emulator launch."""
from pathlib import Path
import sys
import tempfile
import unittest
from unittest.mock import Mock,patch

ROOT=Path(__file__).resolve().parents[1]
sys.path.insert(0,str(ROOT/'tools'))
import game_smoke


class SmokeOptionsTests(unittest.TestCase):
    def recipe(self,extra):
        with tempfile.TemporaryDirectory(prefix='doscraft-smoke-recipe-') as directory:
            vm=Path(directory); (vm/'scratch.img').write_bytes(b'fixture')
            owned=Mock(); volume=Mock(); volume.read.return_value=b'RESULT=FAIL\n'
            args=['game_smoke.py','--bios','seed/isa486.nvr','--report','unused/report',*extra]
            with patch.object(sys,'argv',args),patch.object(game_smoke,'build') as build,\
                 patch.object(game_smoke,'prepare',return_value=(vm,{})) as prepare,\
                 patch.object(game_smoke,'validate',return_value=['no-real-launch']),\
                 patch.object(game_smoke.subprocess,'Popen',return_value=owned) as launch,\
                 patch.object(game_smoke,'diagnostic_volume',return_value=volume),\
                 patch.object(game_smoke.time,'monotonic',side_effect=[0,1]):
                with self.assertRaisesRegex(RuntimeError,'New-game smoke FAILED'): game_smoke.main()
            owned.terminate.assert_called_once(); owned.wait.assert_called_once_with(timeout=30)
            launch.assert_called_once_with(['no-real-launch'],cwd=vm)
            return build.call_args.kwargs,prepare.call_args.kwargs

    def test_default_recipe_retains_production_directory_and_flags(self):
        build,prepare=self.recipe([])
        self.assertEqual(build,{'output':game_smoke.OUTPUT,'extra_adapter_flags':None})
        self.assertEqual(prepare,{'smoke':True,'bios':Path('seed/isa486.nvr'),'output':game_smoke.OUTPUT})

    def test_explicit_destination_probe_uses_only_requested_flag_and_private_output(self):
        build,prepare=self.recipe(['--trap-destination','--build-output','private/candidate'])
        self.assertEqual(build,{'output':Path('private/candidate'),
            'extra_adapter_flags':(*game_smoke.DEFAULT_ADAPTER_FLAGS,'-DDOS_TRAP_DESTINATION')})
        self.assertEqual(prepare['output'],Path('private/candidate'))

    def test_four_texel_probe_combines_with_accepted_transport_in_private_output(self):
        build,prepare=self.recipe(['--trap-four','--build-output','private/four'])
        self.assertEqual(build,{'output':Path('private/four'),
            'extra_adapter_flags':tuple(dict.fromkeys((*game_smoke.DEFAULT_ADAPTER_FLAGS,'-DDOS_TRAP_FOUR')))})
        self.assertEqual(prepare['output'],Path('private/four'))


if __name__=='__main__': unittest.main()
