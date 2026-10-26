#!/usr/bin/env python3
"""Prepare and start the native Linux or Windows PS4RT runtime.

Future games require reverse engineering and source changes. Not a general PS4 emulator.
"""
# SPDX-License-Identifier: GPL-2.0-or-later
import argparse
import ctypes
import os
from pathlib import Path
import re
import subprocess
import sys
from prepare import load_sfo, prepare
from link_modules import link
from content_profile import prepare as prepare_content

ROOT = Path(__file__).resolve().parent.parent
TITLE_ID_PATTERN = re.compile(r'CUSA\d{5}\Z')


def wait_for_process(pid: int) -> None:
    """Wait for an old Windows runtime to release its GPU device during an in-game restart."""
    if os.name != 'nt' or pid <= 0:
        return
    synchronize = 0x00100000
    handle = ctypes.windll.kernel32.OpenProcess(synchronize, False, pid)
    if handle:
        try:
            ctypes.windll.kernel32.WaitForSingleObject(handle, 60_000)
        finally:
            ctypes.windll.kernel32.CloseHandle(handle)


def main(argv=None) -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--game', type=Path, required=True,
                        help='Your extracted plaintext CUSAxxxxx game directory')
    parser.add_argument('--prepare-only', action='store_true')
    parser.add_argument('--upscaler', choices=('off', 'fsr3', 'fsr4'),
                        help='Override the saved graphics setting for this launch')
    parser.add_argument('--output-resolution', choices=('1080', '1440', '4k'))
    parser.add_argument('--language', type=int, choices=range(0, 23), default=1,
                        help='PS4 system language code exposed to the game')
    parser.add_argument('--save-root', type=Path,
                        help='Optional savedata root; slots are stored under <root>/<user>/<title>')
    parser.add_argument('--after', type=int, default=0, help=argparse.SUPPRESS)
    args = parser.parse_args(argv)
    wait_for_process(args.after)
    game = args.game.expanduser().resolve()
    title_id = str(load_sfo(game / 'sce_sys/param.sfo').get('TITLE_ID', ''))
    if not TITLE_ID_PATTERN.fullmatch(title_id):
        parser.error('Expected a PS4 game with CUSAxxxxx TITLE_ID in param.sfo')
    runtime = ROOT / 'out' / ('ps4rt.exe' if os.name == 'nt' else 'ps4rt')
    if not args.prepare_only and not runtime.is_file():
        parser.error('Build the runtime first: ' +
                     (r'.\build-windows.ps1' if os.name == 'nt' else 'bash build.sh'))
    out = ROOT / 'out' / title_id
    prepare(game, out)
    link(game, out)
    prepare_content(game, out, 'full')
    if args.prepare_only:
        return 0
    user_dir = out / 'user'
    gpu_user = user_dir / 'gpu'
    gpu_user.mkdir(parents=True, exist_ok=True)
    env = os.environ.copy()
    # Do not leak compatibility overrides from the caller's shell into a new title.
    for key in ('PS4RT_TRANSITION_SNAPSHOT', 'PS4RT_BC6_FLOAT',
                'PS4RT_WQM_HELPERS', 'PS4RT_ASYNC_SHADERS',
                'PS4RT_OBJECT_MOTION', 'PS4RT_FSR1', 'PS4RT_PARTIAL_HEIGHT_VIEWS',
                'PS4RT_UPSCALER', 'PS4RT_OUTPUT_RES', 'PS4RT_RENDER_RES',
                'PS4RT_UPSCALE_PRESET', 'PS4RT_UPSCALE_BEFORE_CS',
                'PS4RT_UI_TRIGGER_VS', 'PS4RT_PRESET_FILE', 'PS4RT_RCAS',
                'PS4RT_FSR_SHARPNESS', 'PS4RT_JITTER'):
        env.pop(key, None)
    # Start with the known working runtime behavior; future ports adapt it in-source.
    # Do not force PS4RT_ASYNC_SHADERS here: the in-game menu writes its
    # choice to out/<title_id>/ps4rt.ini. New configs default to Sync.
    env.update(PS4RT_BC6_FLOAT='unsigned', PS4RT_TRANSITION_SNAPSHOT='1',
               PS4RT_OBJECT_MOTION='0', PS4RT_FSR1='0',
               PS4RT_PARTIAL_HEIGHT_VIEWS='0', PS4RT_UPSCALER='off')
    env['PS4RT_GPU_USER_DIR'] = str(gpu_user)
    env['PS4RT_CONFIG'] = str(out / 'ps4rt.ini')
    env['PS4RT_LANGUAGE'] = str(args.language)
    if args.save_root is not None:
        save_root = args.save_root.expanduser().resolve()
        save_root.mkdir(parents=True, exist_ok=True)
        env['PS4RT_SAVEDATA_ROOT'] = str(save_root)
    # Load local game art with a stable absolute path, regardless of launch cwd.
    # A manually specified background overrides this default.
    if not env.get('PS4RT_OVERLAY_BACKGROUND'):
        overlay_art = ROOT / 'gpu/assets/star_ocean_overlay.png'
        if overlay_art.is_file():
            env['PS4RT_OVERLAY_BACKGROUND'] = str(overlay_art)
    if not env.get('PS4RT_MENU_SOUND_DIR'):
        sound_dir = ROOT / 'gpu/assets/sounds'
        if sound_dir.is_dir():
            env['PS4RT_MENU_SOUND_DIR'] = str(sound_dir)
    if args.upscaler is not None:
        env['PS4RT_UPSCALER'] = args.upscaler
    if args.output_resolution is not None:
        env['PS4RT_OUTPUT_RES'] = {
            '1080': '1920x1080', '1440': '2560x1440', '4k': '3840x2160'
        }[args.output_resolution]
    if os.name == 'nt':
        # Source builds load their DLLs from MSYS2. Packaged builds keep the same DLLs beside
        # ps4rt.exe, which Windows resolves before PATH, so this remains harmless there.
        msys2_bin = Path(os.environ.get('MSYS2_ROOT', r'C:\msys64')) / 'clang64' / 'bin'
        if msys2_bin.is_dir():
            env['PATH'] = str(msys2_bin) + os.pathsep + env.get('PATH', '')
    print(f'PS4RT native-port template: {title_id}', flush=True)
    command = [str(runtime), str(out / 'boot-linked.bin'), '--app0', str(game),
               '--user', str(user_dir), '--content-profile', str(out / 'content.bin'),
               '--timeout', '0']
    if os.name == 'nt':
        restart = [sys.executable, str(Path(__file__).resolve()), '--game', str(game),
                   '--after', str(os.getpid())]
        if args.upscaler is not None:
            restart.extend(('--upscaler', args.upscaler))
        if args.output_resolution is not None:
            restart.extend(('--output-resolution', args.output_resolution))
        restart.extend(('--language', str(args.language)))
        if args.save_root is not None:
            restart.extend(('--save-root', str(args.save_root)))
        env['PS4RT_RESTART_COMMAND'] = subprocess.list2cmdline(restart)
        return subprocess.run(command, env=env, cwd=ROOT, check=False).returncode
    os.execve(str(runtime), command, env)
    return 0


if __name__ == '__main__':
    try:
        sys.exit(main())
    except (OSError, ValueError, KeyError) as error:
        sys.exit(f'PS4RT: {error}')
