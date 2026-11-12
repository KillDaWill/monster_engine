"""Pruebas headless de política, fallback e integridad de entorno."""
import contextlib
import io
import json
import os
import unittest
from unittest.mock import patch
import run_monster_engine_gpu as gpu


class SelectionTests(unittest.TestCase):
    def setUp(self):
        self.amd = dict(pci='0000:07:00.0', vendor=0x1002, device=0x1638,
                        driver='amdgpu', integrated=True, usable=True)
        self.nv = dict(pci='0000:01:00.0', vendor=0x10de, device=0x25a5,
                       driver='', integrated=False, usable=False)
        self.info = dict(vendor='AMD', renderer='AMD Radeon Graphics', version='4.6 Mesa',
                         vendor_id=0x1002, device_id=0x1638, accelerated=1, hardware=True)

    def choose(self, policy, devices):
        result = type('Result', (), {'stdout': json.dumps(self.info), 'returncode': 0})()
        with patch.object(gpu.subprocess, 'run', return_value=result), contextlib.redirect_stdout(io.StringIO()):
            return gpu.select(policy, devices)

    def test_auto_warns_and_falls_back(self):
        with contextlib.redirect_stderr(io.StringIO()) as warnings:
            device, _, _ = self.choose('auto', [self.nv, self.amd])
        self.assertEqual(device, self.amd)
        self.assertIn('Fallback', warnings.getvalue())

    def test_explicit_missing_never_falls_back(self):
        for policy in ['high-performance', '0000:01:00.0', 'bad-device']:
            with self.assertRaises(RuntimeError):
                self.choose(policy, [self.nv, self.amd])

    def test_pci_and_integrated(self):
        for policy in ['integrated', '07:00.0', '0000:07:00.0', 'pci-0000_07_00_0']:
            self.assertEqual(self.choose(policy, [self.amd])[0], self.amd)

    def test_no_software_or_wrong_device(self):
        for changes in [dict(hardware=False), dict(device_id=123), dict(accelerated=0)]:
            self.assertFalse(gpu.matches(self.amd, dict(self.info, **changes), [self.amd]))

    def test_duplicate_ids_not_certified(self):
        other = dict(self.amd, pci='0000:08:00.0')
        self.assertFalse(gpu.matches(self.amd, self.info, [self.amd, other]))

    def test_environment_scoped_to_child(self):
        with patch.dict(os.environ, {'DRI_PRIME': '9', '__GLX_VENDOR_LIBRARY_NAME': 'nvidia'}):
            env = gpu.environment(self.amd)
            self.assertEqual(env['DRI_PRIME'], 'pci-0000_07_00_0')
            self.assertNotIn('__GLX_VENDOR_LIBRARY_NAME', env)
            self.assertEqual(os.environ['DRI_PRIME'], '9')

    def test_nvidia_glx_not_vulkan(self):
        env = gpu.environment(dict(self.nv, driver='nvidia'))
        self.assertEqual(env['__GLX_VENDOR_LIBRARY_NAME'], 'nvidia')
        self.assertEqual(env['__NV_PRIME_RENDER_OFFLOAD'], '1')
        self.assertEqual(env['SDL_VIDEODRIVER'], 'x11')
        self.assertNotIn('DRI_PRIME', env)


if __name__ == '__main__':
    unittest.main()
