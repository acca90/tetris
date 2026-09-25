#!/usr/bin/env python3
"""Headless test driver for the core: plays scripted input, saves frames, checks audio.

  python3 tools/harness.py build/native/pixeltetris_libretro.so OUTDIR
"""
import ctypes as C
import os
import sys

from PIL import Image

core_path, outdir = sys.argv[1], sys.argv[2]
os.makedirs(outdir, exist_ok=True)
core = C.CDLL(os.path.abspath(core_path))

ENV_SET_PIXEL_FORMAT, ENV_GET_SAVE_DIRECTORY = 10, 31
save_dir = C.c_char_p(outdir.encode())
frame = {}
audio = {"frames": 0, "peak": 0}
buttons = set()

ENV = C.CFUNCTYPE(C.c_bool, C.c_uint, C.c_void_p)
VIDEO = C.CFUNCTYPE(None, C.c_void_p, C.c_uint, C.c_uint, C.c_size_t)
ASAMPLE = C.CFUNCTYPE(None, C.c_int16, C.c_int16)
ABATCH = C.CFUNCTYPE(C.c_size_t, C.POINTER(C.c_int16), C.c_size_t)
POLL = C.CFUNCTYPE(None)
STATE = C.CFUNCTYPE(C.c_int16, C.c_uint, C.c_uint, C.c_uint, C.c_uint)

@ENV
def env(cmd, data):
    if cmd == ENV_SET_PIXEL_FORMAT:
        return C.cast(data, C.POINTER(C.c_int))[0] == 1  # XRGB8888
    if cmd == ENV_GET_SAVE_DIRECTORY:
        C.cast(data, C.POINTER(C.c_char_p))[0] = save_dir.value
        return True
    return cmd in (18, 11)  # SUPPORT_NO_GAME, INPUT_DESCRIPTORS

@VIDEO
def video(data, w, h, pitch):
    frame["img"] = (C.string_at(data, pitch * h), w, h)

@ASAMPLE
def asample(l, r): pass

@ABATCH
def abatch(data, n):
    audio["frames"] += n
    audio["peak"] = max(audio["peak"], max(abs(data[i]) for i in range(0, n * 2, 7)))
    return n

@POLL
def poll(): pass

@STATE
def state(port, device, index, id_):
    return 1 if (port == 0 and device == 1 and id_ in buttons) else 0

cbs = [env, video, asample, abatch, poll, state]
core.retro_set_environment(env)
core.retro_set_video_refresh(video)
core.retro_set_audio_sample(asample)
core.retro_set_audio_sample_batch(abatch)
core.retro_set_input_poll(poll)
core.retro_set_input_state(state)
core.retro_init()
assert core.retro_load_game(None), "load_game failed"

B, Y, SELECT, START, UP, DOWN, LEFT, RIGHT, A, X = 0, 1, 2, 3, 4, 5, 6, 7, 8, 9

def run(n, hold=()):
    buttons.clear(); buttons.update(hold)
    for _ in range(n):
        core.retro_run()

def tap(btn, after=6):
    run(2, (btn,)); run(after)

def shot(name, scale_down=True):
    raw, w, h = frame["img"]
    img = Image.frombuffer("RGBX", (w, h), raw, "raw", "BGRX").convert("RGB")
    if scale_down:
        img = img.resize((w // 2, h // 2), Image.NEAREST)
    img.save(os.path.join(outdir, name + ".png"))

run(80); shot("1-boot")
run(100); shot("2-title")
tap(START, 10)
# play: drop pieces spread across the board
for i in range(14):
    for _ in range(i % 5): tap(LEFT if i % 2 else RIGHT, 2)
    if i % 3 == 0: tap(A, 2)
    if i == 4: tap(X, 4)
    tap(UP, 3)
run(20); shot("3-game")
tap(START, 20); shot("4-paused"); tap(START, 5)
for _ in range(60): tap(UP, 1)
run(200); shot("5-over")
print("audio frames", audio["frames"], "peak", audio["peak"])
print("hiscore file:", open(os.path.join(outdir, "pixeltetris.hi")).read().strip()
      if os.path.exists(os.path.join(outdir, "pixeltetris.hi")) else "missing")
