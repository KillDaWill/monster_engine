#!/usr/bin/env python3
"""Selección Linux por proceso; sondea SDL/GL antes de ejecutar, sin cambiar el sistema."""
import argparse
import ctypes as C
import ctypes.util
import json
import os
from pathlib import Path
import re
import shutil
import socket
import subprocess
import sys
import tempfile
import time
from urllib.parse import urlencode
from urllib.request import urlopen

ROOT = Path(__file__).resolve().parents[1]
SELECTION = ('DRI_PRIME', '__NV_PRIME_RENDER_OFFLOAD', '__GLX_VENDOR_LIBRARY_NAME',
             '__NV_PRIME_RENDER_OFFLOAD_PROVIDER')


def inventory():
    devices = []
    for path in sorted(Path('/sys/bus/pci/devices').glob('*')):
        def text(name, default=''):
            try:
                return (path / name).read_text().strip()
            except OSError:
                return default
        if int(text('class', '0'), 16) >> 16 != 3:
            continue
        pci = path.name
        driver = (path / 'driver').resolve().name if (path / 'driver').exists() else ''
        name = pci
        if shutil.which('lspci'):
            name = subprocess.check_output(['lspci', '-s', pci, '-nn'], text=True).strip()
        vendor = int(text('vendor', '0'), 16)
        nodes = list((path / 'drm').glob('renderD*'))
        accessible = any(os.access('/dev/dri/' + node.name, os.R_OK | os.W_OK) for node in nodes)
        if driver == 'nvidia':
            accessible |= os.access('/dev/nvidiactl', os.R_OK | os.W_OK)
        devices.append(dict(pci=pci, vendor=vendor,
                            device=int(text('device', '0'), 16), name=name, driver=driver,
                            # Heurística de portátil, no una prueba universal de UMA.
                            integrated=vendor != 0x10de and text('boot_vga') == '1',
                            usable=bool(driver and accessible)))
    return devices


def environment(device):
    env = os.environ.copy()
    for key in SELECTION:
        env.pop(key, None)
    env['MONSTER_GPU'] = 'launcher'  # No ejecutar una segunda selección en C.
    if device['driver'] == 'nvidia':
        env.update(__NV_PRIME_RENDER_OFFLOAD='1', __GLX_VENDOR_LIBRARY_NAME='nvidia')
        env['SDL_VIDEODRIVER'] = 'x11'  # GLX explícito incluso en escritorio Wayland.
    else:
        env['DRI_PRIME'] = 'pci-' + device['pci'].replace(':', '_').replace('.', '_')
    return env


def probe():
    """Contexto oculto con el mismo SDL que la demo, aislado en otro proceso."""
    sdl = C.CDLL(ctypes.util.find_library('SDL2') or 'libSDL2-2.0.so.0')
    sdl.SDL_CreateWindow.argtypes = [C.c_char_p, C.c_int, C.c_int, C.c_int, C.c_int, C.c_uint]
    sdl.SDL_CreateWindow.restype = C.c_void_p
    sdl.SDL_GL_CreateContext.argtypes = [C.c_void_p]
    sdl.SDL_GL_CreateContext.restype = C.c_void_p
    sdl.SDL_GL_GetProcAddress.argtypes = [C.c_char_p]
    sdl.SDL_GL_GetProcAddress.restype = C.c_void_p
    sdl.SDL_GetError.restype = C.c_char_p
    if sdl.SDL_Init(32) != 0:
        raise RuntimeError(sdl.SDL_GetError().decode())
    window = sdl.SDL_CreateWindow(b'GPU probe', 0, 0, 32, 32, 2 | 8)
    if not window or not sdl.SDL_GL_CreateContext(window):
        raise RuntimeError(sdl.SDL_GetError().decode())
    get = C.CFUNCTYPE(C.c_char_p, C.c_uint)(sdl.SDL_GL_GetProcAddress(b'glGetString'))
    result = {name: get(enum).decode() for name, enum in
              [('vendor', 0x1F00), ('renderer', 0x1F01), ('version', 0x1F02)]}
    # GLX_MESA_query_renderer identifica vendor/device del contexto, no sólo texto.
    address = sdl.SDL_GL_GetProcAddress(b'glXQueryCurrentRendererIntegerMESA')
    if address:
        query = C.CFUNCTYPE(C.c_int, C.c_int, C.POINTER(C.c_uint))(address)
        for name, enum in [('vendor_id', 0x8183), ('device_id', 0x8184), ('accelerated', 0x8186)]:
            value = C.c_uint()
            if query(enum, C.byref(value)):
                result[name] = value.value
    result['hardware'] = not any(x in result['renderer'].lower() for x in
                                 ('llvmpipe', 'softpipe', 'swrast', 'software', 'swiftshader'))
    print(json.dumps(result), flush=True)


def matches(device, info, devices):
    if not info['hardware'] or info.get('accelerated') == 0:
        return False
    if 'device_id' in info:
        matching = [d for d in devices if (d['vendor'], d['device']) ==
                    (info['vendor_id'], info['device_id'])]
        return len(matching) == 1 and matching[0]['pci'] == device['pci']
    # Sin extensión Mesa, aceptar sólo un NVIDIA y vendor real NVIDIA.
    return (device['driver'] == 'nvidia' and 'NVIDIA' in info['vendor'] and
            len([d for d in devices if d['vendor'] == 0x10de]) == 1)


def select(policy, devices):
    if policy in ('auto', 'high-performance'):
        candidates = sorted(devices, key=lambda d: d['integrated'])
        if policy == 'high-performance':
            candidates = [d for d in candidates if not d['integrated']]
    elif policy == 'integrated':
        candidates = [d for d in devices if d['integrated']]
    else:
        pci = policy.removeprefix('pci-').replace('_', ':', 2)
        if pci.count(':') == 2 and '.' not in pci:
            pci = pci[:-2] + '.' + pci[-1]
        if re.fullmatch(r'[0-9a-fA-F]{2}:[0-9a-fA-F]{2}\.[0-7]', pci):
            pci = '0000:' + pci
        candidates = [d for d in devices if d['pci'].lower() == pci.lower()]
    if len([d for d in candidates if d['usable'] and not d['integrated']]) > 1:
        print('[GPU WARNING] Varias dedicadas: orden PCI heurístico, no ranking medido; use --gpu PCI.', file=sys.stderr)
    failures = []
    for device in candidates:
        if not device['usable']:
            failures.append(device['pci'] + ': sin driver/render node accesible')
            continue
        env = environment(device)
        try:
            run = subprocess.run([sys.executable, __file__, '--probe'], env=env,
                                 capture_output=True, text=True, timeout=15)
            if run.returncode:
                raise RuntimeError(run.stderr.strip() or 'Fallo al crear contexto SDL/GL')
            info = json.loads(run.stdout.splitlines()[-1])
            if not matches(device, info, devices):
                raise RuntimeError('renderer no coincide o no se puede mapear inequívocamente: ' + info['renderer'])
            if not re.match(r'(?:[4-9]\.|3\.[3-9])', info['version']):
                raise RuntimeError('OpenGL >= 3.3 requerido')
            if failures:
                print('[GPU WARNING] Fallback: ' + '; '.join(failures), file=sys.stderr)
            if policy == 'auto' and device['integrated']:
                print('[GPU WARNING] Usando integrada; preferencia dedicada NO satisfecha (o equipo de una sola GPU).', file=sys.stderr)
            env.update(MONSTER_GPU_POLICY=policy, MONSTER_GPU_PCI=device['pci'],
                       MONSTER_GPU_EXPECT_RENDERER=info['renderer'],
                       MONSTER_GPU_CLASS='integrated' if device['integrated'] else 'discrete')
            print(f"[GPU request] policy={policy} PCI={device['pci']} driver={device['driver']}", flush=True)
            print('[GPU probe] ' + json.dumps(info), flush=True)
            return device, info, env
        except (subprocess.SubprocessError, ValueError, RuntimeError) as error:
            failures.append(device['pci'] + ': ' + str(error))
    raise RuntimeError('; '.join(failures) or 'Ningún dispositivo coincide con ' + policy)


def editor(args, info, device, env):
    # Un perfil nuevo evita que un navegador ya abierto absorba la selección.
    browser = next((shutil.which(x) for x in ('google-chrome', 'chromium', 'chromium-browser')
                    if shutil.which(x)), None)
    if not browser:
        raise RuntimeError('Editor: Chromium/Chrome no disponible. Use server.py sin navegador; GPU WebGL sin verificar.')
    with socket.socket() as connection:
        if connection.connect_ex(('127.0.0.1', args.port)) == 0:
            raise RuntimeError('Puerto ocupado: no reutilizar ni sobrescribir otra sesión del editor; use --port.')
    with tempfile.TemporaryDirectory(prefix='monster-browser-') as profile:
        server = subprocess.Popen([sys.executable, str(ROOT / 'tools/creature_editor/server.py'),
                                   '--port', str(args.port), *args.command], cwd=ROOT)
        browser_process = None
        try:
            url = f'http://127.0.0.1:{args.port}'
            for _ in range(100):
                if server.poll() is not None:
                    raise RuntimeError('No se inició servidor propio; no reutilizar otra sesión del editor.')
                try:
                    with urlopen(url, timeout=.2):
                        break
                except OSError:
                    time.sleep(.1)
            else:
                raise RuntimeError('Timeout al iniciar editor')
            fragment = urlencode(dict(gpu=args.gpu, pci=device['pci'], vendor=info['vendor'],
                                      renderer=info['renderer'].split(' (')[0]))
            browser_process = subprocess.Popen([browser, '--user-data-dir=' + profile,
                '--ozone-platform=x11', '--use-gl=angle', '--use-angle=gl',
                '--no-first-run', '--no-default-browser-check', url + '/#' + fragment], env=env)
            print('[GPU editor] WebGL/ANGLE debe confirmar la GPU en el estado del visor; el sondeo SDL no lo certifica.', flush=True)
            while server.poll() is None:
                if browser_process.poll() is not None:
                    return browser_process.returncode
                time.sleep(.2)
            return server.returncode
        finally:
            server.terminate()
            server.wait()
            if browser_process and browser_process.poll() is None:
                browser_process.terminate()
                browser_process.wait()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--gpu', default=os.getenv('MONSTER_GPU_POLICY', 'auto'),
                        help='auto, high-performance (sin fallback), integrated o PCI 0000:01:00.0')
    parser.add_argument('--list', action='store_true')
    parser.add_argument('--probe', action='store_true', help=argparse.SUPPRESS)
    parser.add_argument('--editor', action='store_true')
    parser.add_argument('--port', type=int, default=8765)
    parser.add_argument('command', nargs=argparse.REMAINDER)
    args = parser.parse_args()
    if args.probe:
        probe()
        return 0
    devices = inventory()
    if args.list:
        print(json.dumps(devices, indent=2))
        return 0
    if args.command[:1] == ['--']:
        args.command.pop(0)
    device, info, env = select(args.gpu, devices)
    if args.editor:
        return editor(args, info, device, env)
    command = args.command or [str(ROOT / 'demos/demo_morph_dog_lizard'), '--realtime']
    os.execvpe(command[0], command, env)


if __name__ == '__main__':
    try:
        sys.exit(main())
    except (RuntimeError, OSError) as error:
        sys.exit('[GPU ERROR] ' + str(error) + '\nRevise lspci -nnk, /dev/dri y el driver para el kernel actual. No se cambió el sistema.')
    except KeyboardInterrupt:
        sys.exit(130)
