/*
 * Pixel Boy Tetris — libretro core.
 *
 * A port of ../../index.html for handhelds running RetroArch (TrimUI Brick and friends).
 * The game draws a 256x192 frame — the Pixel Boy screen (176x168) inside a slim bezel —
 * and upscales it 4x itself to 1024x768 so pixels stay crisp whatever filter RetroArch uses.
 * Art comes from the same Aseprite-generated PNGs, embedded via tools/png2c.py.
 * Music (Korobeiniki) and sound effects are synthesised here, like the Web Audio version.
 */
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "libretro.h"
#include "assets.h"

#define SW 256
#define SH 192
#define SCALE 4
#define OW (SW * SCALE)
#define OH (SH * SCALE)
#define FPS 60
#define SR 44100
#define SPF (SR / FPS)
#define DT (1.0 / FPS)

/* ------------------------------------------------------------------ constants */
#define COLS 10
#define ROWS 22
#define HIDDEN 2
#define T 8
#define GHOST 7
#define DEAD 8
#define DAS 0.16
#define ARR 0.04
/* soft drop: a tap moves one row; holding repeats after SOFT_DELAY, every SOFT_RATE */
#define SOFT_DELAY 0.15
#define SOFT_RATE 0.07
#define LOCK_DELAY 0.5
#define MAX_RESETS 15
#define CLEAR_TIME 0.36
#define BOOT_TIME 2.4

/* the LCD sits inside the bezel at (LCD_X, LCD_Y), 176x168 like the web device */
#define LCD_X 40
#define LCD_Y 12
#define LCD_W 176
#define LCD_H 168
#define WELL_X 48
#define WELL_Y 4

enum {
  C_LCD = 0x1b2340, C_LCDDOT = 0x222c4d, C_WELL = 0x121a31, C_WELLDOT = 0x1c2644,
  C_FRAMEDARK = 0x0c1122, C_FRAMELITE = 0x3d4b78, C_LABEL = 0x6fcad6, C_VALUE = 0xf3ecd2,
  C_DIM = 0x6c78a3, C_CREAM = 0xf3ecd2, C_CREAMSHADE = 0xd9cfae, C_NAVY = 0x1d2438,
  C_TEAL = 0x2d9cb4, C_CORAL = 0xe8566c, C_MUSTARD = 0xf2a93b, C_YELLOW = 0xf6d45a,
  C_SAGE = 0x7fbf6a, C_LAV = 0x9a82e0, C_PINK = 0xf28aa0, C_BEZEL = 0x3e4865,
};
static const uint32_t RAINBOW[6] = { C_CORAL, C_MUSTARD, C_YELLOW, C_SAGE, C_LABEL, C_LAV };

/* Piece order matches tiles.png: I O T S Z J L */
static const int BASE_N[7] = { 4, 2, 3, 3, 3, 3, 3 };
static const char *BASE[7] = {
  "....####........", "####", ".#.###...", ".##.##...", "##..##...", "#..###...", "..####...",
};
static int SHAPES[7][4][4][2];

/* SRS kicks, y up as in the spec (applied as y - ky) */
typedef struct { int from, to; int k[5][2]; } Kick;
static const Kick KICK_JLSTZ[8] = {
  { 0, 1, { { 0, 0 }, { -1, 0 }, { -1, 1 }, { 0, -2 }, { -1, -2 } } },
  { 1, 0, { { 0, 0 }, { 1, 0 }, { 1, -1 }, { 0, 2 }, { 1, 2 } } },
  { 1, 2, { { 0, 0 }, { 1, 0 }, { 1, -1 }, { 0, 2 }, { 1, 2 } } },
  { 2, 1, { { 0, 0 }, { -1, 0 }, { -1, 1 }, { 0, -2 }, { -1, -2 } } },
  { 2, 3, { { 0, 0 }, { 1, 0 }, { 1, 1 }, { 0, -2 }, { 1, -2 } } },
  { 3, 2, { { 0, 0 }, { -1, 0 }, { -1, -1 }, { 0, 2 }, { -1, 2 } } },
  { 3, 0, { { 0, 0 }, { -1, 0 }, { -1, -1 }, { 0, 2 }, { -1, 2 } } },
  { 0, 3, { { 0, 0 }, { 1, 0 }, { 1, 1 }, { 0, -2 }, { 1, -2 } } },
};
static const Kick KICK_I[8] = {
  { 0, 1, { { 0, 0 }, { -2, 0 }, { 1, 0 }, { -2, -1 }, { 1, 2 } } },
  { 1, 0, { { 0, 0 }, { 2, 0 }, { -1, 0 }, { 2, 1 }, { -1, -2 } } },
  { 1, 2, { { 0, 0 }, { -1, 0 }, { 2, 0 }, { -1, 2 }, { 2, -1 } } },
  { 2, 1, { { 0, 0 }, { 1, 0 }, { -2, 0 }, { 1, -2 }, { -2, 1 } } },
  { 2, 3, { { 0, 0 }, { 2, 0 }, { -1, 0 }, { 2, 1 }, { -1, -2 } } },
  { 3, 2, { { 0, 0 }, { -2, 0 }, { 1, 0 }, { -2, -1 }, { 1, 2 } } },
  { 3, 0, { { 0, 0 }, { 1, 0 }, { -2, 0 }, { 1, -2 }, { -2, 1 } } },
  { 0, 3, { { 0, 0 }, { -1, 0 }, { 2, 0 }, { -1, 2 }, { 2, -1 } } },
};

/* ------------------------------------------------------------------ libretro plumbing */
static retro_environment_t env_cb;
static retro_video_refresh_t video_cb;
static retro_audio_sample_batch_t audio_batch_cb;
static retro_input_poll_t input_poll_cb;
static retro_input_state_t input_state_cb;
static char save_path[1024];

static uint32_t fb[SW * SH];
static uint32_t out[OW * OH];
static int16_t abuf[SPF * 2];

/* ------------------------------------------------------------------ drawing */
static int org_x, org_y, clip_x0, clip_y0, clip_x1 = SW, clip_y1 = SH;

static void view(int ox, int oy, int cw, int ch) {
  org_x = ox; org_y = oy;
  clip_x0 = ox; clip_y0 = oy; clip_x1 = ox + cw; clip_y1 = oy + ch;
}

static inline void put(int x, int y, uint32_t c, int a) {
  x += org_x; y += org_y;
  if (a <= 0 || x < clip_x0 || y < clip_y0 || x >= clip_x1 || y >= clip_y1) return;
  uint32_t *d = &fb[y * SW + x];
  if (a >= 255) { *d = c; return; }
  uint32_t s = *d;
  int r = (((s >> 16) & 255) * (255 - a) + ((c >> 16) & 255) * a) / 255;
  int g = (((s >> 8) & 255) * (255 - a) + ((c >> 8) & 255) * a) / 255;
  int b = ((s & 255) * (255 - a) + (c & 255) * a) / 255;
  *d = (uint32_t)(r << 16 | g << 8 | b);
}
static void rect(int x, int y, int w, int h, uint32_t c, int a) {
  for (int yy = y; yy < y + h; yy++)
    for (int xx = x; xx < x + w; xx++) put(xx, yy, c, a);
}
static void circle(float cx, float cy, float r, uint32_t c) {
  for (int y = (int)(cy - r - 1); y <= (int)(cy + r + 1); y++)
    for (int x = (int)(cx - r - 1); x <= (int)(cx + r + 1); x++) {
      float dx = x + 0.5f - cx, dy = y + 0.5f - cy;
      if (dx * dx + dy * dy <= r * r) put(x, y, c, 255);
    }
}
/* tint < 0 keeps the image colours; k = integer scale; alpha multiplies the image alpha */
static void blit(const uint32_t *img, int iw, int sx, int sy, int sw, int sh,
                 int dx, int dy, int k, int alpha, long tint) {
  for (int y = 0; y < sh; y++)
    for (int x = 0; x < sw; x++) {
      uint32_t p = img[(sy + y) * iw + sx + x];
      int a = (int)(p >> 24) * alpha / 255;
      if (!a) continue;
      uint32_t c = tint >= 0 ? (uint32_t)tint : (p & 0xffffff);
      for (int j = 0; j < k; j++)
        for (int i = 0; i < k; i++) put(dx + x * k + i, dy + y * k + j, c, a);
    }
}
static void tile(int idx, int x, int y, int alpha) {
  blit(IMG_TILES, IMG_TILES_W, idx * T, 0, T, T, x, y, 1, alpha, -1);
}

static int text_w(const char *s, int k) { int n = (int)strlen(s); return n ? n * 4 * k - k : 0; }
static void text_multi(const char *s, int x, int y, const uint32_t *cols, int ncol, int k) {
  for (int i = 0; s[i]; i++) {
    char ch = s[i];
    if (ch >= 'a' && ch <= 'z') ch -= 32;
    const char *g = strchr(FONT_CHARS, ch);
    if (!g || g == FONT_CHARS || !ch) continue;
    int gi = (int)(g - FONT_CHARS);
    blit(IMG_FONT, IMG_FONT_W, gi * 3, 0, 3, 5, x + i * 4 * k, y, k, 255, cols[i % ncol]);
  }
}
static void text(const char *s, int x, int y, uint32_t c, int k) { text_multi(s, x, y, &c, 1, k); }
static void text_c(const char *s, int cx, int y, uint32_t c, int k) { text(s, cx - text_w(s, k) / 2, y, c, k); }
static void text_c_multi(const char *s, int cx, int y, const uint32_t *cols, int n, int k) {
  text_multi(s, cx - text_w(s, k) / 2, y, cols, n, k);
}
static void shadow_text(const char *s, int cx, int y, const uint32_t *cols, int n, int k) {
  text_c(s, cx + 1, y + 1, C_FRAMEDARK, k);
  text_c_multi(s, cx, y, cols, n, k);
}

/* ------------------------------------------------------------------ audio */
enum { OSC_SQUARE, OSC_TRIANGLE };
enum { BUS_MUSIC, BUS_SFX };
typedef struct {
  bool on; int type, bus;
  double f0, f1, phase;
  int64_t start, len;
  float vol;
} Voice;
#define MAXV 48
static Voice voices[MAXV];
static int64_t sclock;           /* sample clock; frozen while paused */
static double HZ[128];
static bool muted;

static void voice(int bus, int type, double f, int64_t at, double dur, float vol, double slide_to) {
  int slot = 0; int64_t oldest = INT64_MAX;
  for (int i = 0; i < MAXV; i++) {
    if (!voices[i].on) { slot = i; oldest = -1; break; }
    if (voices[i].start < oldest) { oldest = voices[i].start; slot = i; }
  }
  Voice *v = &voices[slot];
  v->on = true; v->type = type; v->bus = bus; v->f0 = f; v->f1 = slide_to > 0 ? slide_to : f;
  v->phase = 0; v->start = at; v->len = (int64_t)(dur * SR); v->vol = vol;
  if (v->len < 2) v->len = 2;
}
#define SEC(s) ((int64_t)((s) * SR))

/* Korobeiniki (Russian folk song, public domain) — the classic Type-A arrangement. */
typedef struct { int note; float beats; } Note;
static const Note PART_A[] = {
  {76,1},{71,.5},{72,.5},{74,1},{72,.5},{71,.5},
  {69,1},{69,.5},{72,.5},{76,1},{74,.5},{72,.5},
  {71,1.5},{72,.5},{74,1},{76,1},
  {72,1},{69,1},{69,1},{0,1},
  {0,.5},{74,1},{77,.5},{81,1},{79,.5},{77,.5},
  {76,1.5},{72,.5},{76,1},{74,.5},{72,.5},
  {71,1},{71,.5},{72,.5},{74,1},{76,1},
  {72,1},{69,1},{69,1},{0,1},
};
static const Note PART_C[] = {
  {76,2},{72,2},{74,2},{71,2},{72,2},{69,2},{68,2},{71,1},{0,1},
  {76,2},{72,2},{74,2},{71,2},{72,1},{76,1},{81,2},{80,4},
};
#define NA ((int)(sizeof PART_A / sizeof PART_A[0]))
#define NC ((int)(sizeof PART_C / sizeof PART_C[0]))
static Note MELODY[NA * 2 + NC];
static Note BASS[24 * 8];
static const int BASS_ROOTS[24] = { 40,45,40,45,50,48,40,45, 40,45,40,45,50,48,40,45, 45,40,45,40,45,40,45,40 };

typedef struct { const Note *ev; int n, i; double b; int type; float vol, gap; } Track;
static Track tracks[2];
static bool music_on;
static int64_t anchor_s; static double anchor_b, spb_s;

static double beat_sample(double b) { return anchor_s + (b - anchor_b) * spb_s; }
static void set_tempo(int lvl) {
  if (sclock > anchor_s) { anchor_b += (sclock - anchor_s) / spb_s; anchor_s = sclock; }
  int bpm = 132 + (lvl - 1) * 6; if (bpm > 200) bpm = 200;
  spb_s = SR * 60.0 / bpm;
}
static void start_music(int lvl) {
  music_on = true; anchor_s = sclock + SEC(0.08); anchor_b = 0;
  for (int t = 0; t < 2; t++) { tracks[t].i = 0; tracks[t].b = 0; }
  set_tempo(lvl);
}
static void schedule_music(void) {
  if (!music_on) return;
  double horizon = (double)(sclock + SPF + SEC(0.02));
  for (int t = 0; t < 2; t++) {
    Track *tr = &tracks[t];
    while (beat_sample(tr->b) < horizon) {
      Note nt = tr->ev[tr->i];
      double at = beat_sample(tr->b);
      if (at < sclock) at = (double)sclock;
      if (nt.note) voice(BUS_MUSIC, tr->type, HZ[nt.note], (int64_t)at, nt.beats * spb_s / SR * tr->gap, tr->vol, 0);
      tr->b += nt.beats; tr->i = (tr->i + 1) % tr->n;
    }
  }
}

static void sfx_move(void)   { voice(BUS_SFX, OSC_SQUARE, 220, sclock, 0.03, 0.06f, 0); }
static void sfx_rotate(void) { voice(BUS_SFX, OSC_SQUARE, 523, sclock, 0.05, 0.08f, 784); }
static void sfx_hold(void)   { voice(BUS_SFX, OSC_TRIANGLE, 660, sclock, 0.06, 0.3f, 0); voice(BUS_SFX, OSC_TRIANGLE, 440, sclock + SEC(0.06), 0.08, 0.3f, 0); }
static void sfx_lock(void)   { voice(BUS_SFX, OSC_TRIANGLE, 140, sclock, 0.1, 0.5f, 70); }
static void sfx_drop(void)   { voice(BUS_SFX, OSC_SQUARE, 300, sclock, 0.12, 0.12f, 60); }
static void sfx_clear(int n) {
  static const int four[6] = { 72, 76, 79, 84, 88, 91 };
  int cnt = n == 4 ? 6 : n + 1;
  for (int i = 0; i < cnt; i++) voice(BUS_SFX, OSC_SQUARE, HZ[four[i]], sclock + SEC(i * 0.055), 0.09, 0.12f, 0);
}
static void sfx_level(void) { static const int m[3] = { 79, 84, 88 }; for (int i = 0; i < 3; i++) voice(BUS_SFX, OSC_TRIANGLE, HZ[m[i]], sclock + SEC(i * 0.08), 0.12, 0.35f, 0); }
static void sfx_over(void)  { static const int m[5] = { 67, 63, 60, 55, 48 }; for (int i = 0; i < 5; i++) voice(BUS_SFX, OSC_SQUARE, HZ[m[i]], sclock + SEC(i * 0.16), 0.18, 0.12f, 0); }
static void sfx_click(void) { voice(BUS_SFX, OSC_SQUARE, 880, sclock, 0.025, 0.05f, 0); }
static void sfx_ding(void)  { voice(BUS_SFX, OSC_SQUARE, HZ[84], sclock, 0.07, 0.12f, 0); voice(BUS_SFX, OSC_SQUARE, HZ[96], sclock + SEC(0.07), 0.6, 0.12f, 0); }

static void render_audio(bool paused) {
  if (paused) { memset(abuf, 0, sizeof abuf); return; }
  schedule_music();
  static const float BUS_GAIN[2] = { 0.55f * 0.5f, 0.7f * 0.5f };
  for (int i = 0; i < SPF; i++) {
    int64_t s = sclock + i;
    float mix = 0;
    for (int k = 0; k < MAXV; k++) {
      Voice *v = &voices[k];
      if (!v->on || s < v->start) continue;
      int64_t p = s - v->start;
      if (p >= v->len) { v->on = false; continue; }
      int64_t att = SEC(0.005), hold = (int64_t)(v->len * 0.7);
      if (hold <= att) hold = att + 1;
      float env = p < att ? (float)p / att : p < hold ? 1.f : (float)(v->len - p) / (v->len - hold);
      double f = v->f0 + (v->f1 - v->f0) * (double)p / v->len;
      v->phase += f / SR; v->phase -= (int)v->phase;
      float o = v->type == OSC_SQUARE ? (v->phase < 0.5 ? 1.f : -1.f)
                                      : (float)(v->phase < 0.5 ? 4 * v->phase - 1 : 3 - 4 * v->phase);
      mix += o * env * v->vol * BUS_GAIN[v->bus];
    }
    if (muted) mix = 0;
    int sample = (int)(mix * 32767);
    if (sample > 32767) sample = 32767; else if (sample < -32768) sample = -32768;
    abuf[i * 2] = abuf[i * 2 + 1] = (int16_t)sample;
  }
  sclock += SPF;
}

/* ------------------------------------------------------------------ game state */
enum { ST_BOOT, ST_TITLE, ST_PLAYING, ST_PAUSED, ST_CLEARING, ST_OVER };
static int state; static double state_t, gtime;
static int8_t board[ROWS][COLS];
static int queue[16], qlen;
typedef struct { int type, rot, x, y; } Piece;
static Piece cur; static bool has_cur;
static int hold = -1; static bool can_hold;
static int score, lines, level, tetrises, best; static bool new_best; static double play_time;
static double fall_acc, lock_t; static int resets, lowest_y;
static int clear_rows[4], nclear; static double clear_t;
typedef struct { char text[16]; double t; } Popup;
static Popup popups[4]; static int npop;
static double shake;
static bool held_l, held_r, held_d; static int dir; static double das_t, arr_t, soft_t;
static uint32_t rng = 0x9e3779b9;

static void set_state(int s) { state = s; state_t = 0; }
static uint32_t rnd(void) { rng ^= rng << 13; rng ^= rng >> 17; rng ^= rng << 5; return rng; }

static void push_bag(void) {
  int b[7] = { 0, 1, 2, 3, 4, 5, 6 };
  for (int i = 6; i > 0; i--) { int j = (int)(rnd() % (uint32_t)(i + 1)); int t = b[i]; b[i] = b[j]; b[j] = t; }
  for (int i = 0; i < 7; i++) queue[qlen++] = b[i];
}
static int next_type(void) {
  while (qlen < 8) push_bag();
  int t = queue[0];
  memmove(queue, queue + 1, sizeof(int) * (size_t)(--qlen));
  return t;
}

static bool collide(int type, int rot, int x, int y) {
  for (int i = 0; i < 4; i++) {
    int bx = x + SHAPES[type][rot][i][0], by = y + SHAPES[type][rot][i][1];
    if (bx < 0 || bx >= COLS || by >= ROWS) return true;
    if (by >= 0 && board[by][bx] >= 0) return true;
  }
  return false;
}
static bool grounded(void) { return collide(cur.type, cur.rot, cur.x, cur.y + 1); }
static double gravity(void) {
  double base = 0.8 - (level - 1) * 0.007, g = 1;
  for (int i = 1; i < level; i++) g *= base;
  return g;
}

static void save_best(void) {
  if (!save_path[0]) return;
  FILE *f = fopen(save_path, "w");
  if (f) { fprintf(f, "%d\n", best); fclose(f); }
}
static void load_best(void) {
  if (!save_path[0]) return;
  FILE *f = fopen(save_path, "r");
  if (f) { if (fscanf(f, "%d", &best) != 1) best = 0; fclose(f); }
}

static void game_over(void) {
  set_state(ST_OVER); music_on = false; sfx_over(); has_cur = false;
  if (score > best) { best = score; new_best = true; save_best(); }
}

static void spawn(int type) {
  cur.type = type; cur.rot = 0; cur.x = (COLS - BASE_N[type]) / 2; cur.y = 0;
  has_cur = true;
  fall_acc = 0; lock_t = 0; resets = 0;
  if (collide(type, 0, cur.x, cur.y)) { game_over(); return; }
  if (!grounded()) cur.y++;
  lowest_y = cur.y;
}

static void new_game(void) {
  memset(board, -1, sizeof board);
  qlen = 0; hold = -1; can_hold = true; npop = 0;
  score = 0; lines = 0; level = 1; new_best = false; play_time = 0; tetrises = 0;
  set_state(ST_PLAYING);
  spawn(next_type());
  start_music(level);
}

static void after_move(void) { if (grounded() && resets < MAX_RESETS) { lock_t = 0; resets++; } }
static bool move(int dx) {
  if (collide(cur.type, cur.rot, cur.x + dx, cur.y)) return false;
  cur.x += dx; after_move(); sfx_move(); return true;
}
static void rotate(int d) {
  if (cur.type == 1) return;
  int to = (cur.rot + d + 4) % 4;
  const Kick *tab = cur.type == 0 ? KICK_I : KICK_JLSTZ;
  for (int e = 0; e < 8; e++) {
    if (tab[e].from != cur.rot || tab[e].to != to) continue;
    for (int i = 0; i < 5; i++) {
      int kx = tab[e].k[i][0], ky = tab[e].k[i][1];
      if (!collide(cur.type, to, cur.x + kx, cur.y - ky)) {
        cur.x += kx; cur.y -= ky; cur.rot = to; after_move(); sfx_rotate(); return;
      }
    }
  }
}
static bool soft_step(void) {
  if (grounded()) return false;
  cur.y++; score += 1;
  if (cur.y > lowest_y) { lowest_y = cur.y; resets = 0; }
  return true;
}
static int drop_distance(void) {
  int d = 0;
  while (!collide(cur.type, cur.rot, cur.x, cur.y + d + 1)) d++;
  return d;
}

static void popup(const char *s) {
  if (!s[0]) return;
  if (npop == 4) { memmove(popups, popups + 1, sizeof(Popup) * 3); npop = 3; }
  snprintf(popups[npop].text, sizeof popups[npop].text, "%s", s);
  popups[npop++].t = 0;
}

static void after_lock(void) { can_hold = true; if (state == ST_PLAYING) spawn(next_type()); }

static void lock_piece(void) {
  bool all_hidden = true;
  for (int i = 0; i < 4; i++) {
    int x = cur.x + SHAPES[cur.type][cur.rot][i][0], y = cur.y + SHAPES[cur.type][cur.rot][i][1];
    if (y >= 0) board[y][x] = (int8_t)cur.type;
    if (y >= HIDDEN) all_hidden = false;
  }
  has_cur = false;
  if (all_hidden) { game_over(); return; }
  nclear = 0;
  for (int y = 0; y < ROWS; y++) {
    bool full = true;
    for (int x = 0; x < COLS; x++) if (board[y][x] < 0) { full = false; break; }
    if (full) clear_rows[nclear++] = y;
  }
  if (nclear) { set_state(ST_CLEARING); clear_t = 0; sfx_clear(nclear); }
  else { sfx_lock(); after_lock(); }
}

static void finish_clear(void) {
  int n = nclear, dst = ROWS - 1;
  int8_t nb[ROWS][COLS];
  memset(nb, -1, sizeof nb);
  for (int y = ROWS - 1; y >= 0; y--) {
    bool cleared = false;
    for (int i = 0; i < n; i++) if (clear_rows[i] == y) cleared = true;
    if (!cleared) memcpy(nb[dst--], board[y], COLS);
  }
  memcpy(board, nb, sizeof board);
  lines += n;
  if (n == 4) tetrises++;
  static const int pts[5] = { 0, 100, 300, 500, 800 };
  static const char *names[5] = { "", "", "DOUBLE", "TRIPLE", "TETRIS!" };
  score += pts[n] * level;
  popup(names[n]);
  int nl = lines / 10 + 1;
  if (nl > level) {
    char buf[24];
    level = nl; set_tempo(level); sfx_level();
    snprintf(buf, sizeof buf, "LEVEL %d", level); popup(buf);
  }
  set_state(ST_PLAYING);
  after_lock();
}

static void hard_drop(void) {
  int d = drop_distance();
  cur.y += d; score += 2 * d;
  sfx_drop(); shake = 0.09;
  lock_piece();
}
static void do_hold(void) {
  if (!can_hold) return;
  int t = cur.type;
  can_hold = false;
  if (hold < 0) { hold = t; spawn(next_type()); }
  else { int h = hold; hold = t; spawn(h); }
  sfx_hold();
}

/* ------------------------------------------------------------------ input */
static bool prev[16];

static void handle_input(void) {
  bool now[16];
  input_poll_cb();
  for (int i = 0; i < 16; i++) now[i] = input_state_cb(0, RETRO_DEVICE_JOYPAD, 0, (unsigned)i) != 0;
#define PRESSED(id) (now[id] && !prev[id])
#define RELEASED(id) (!now[id] && prev[id])

  if (RELEASED(RETRO_DEVICE_ID_JOYPAD_LEFT))  { held_l = false; if (dir == -1) { dir = held_r ? 1 : 0; das_t = arr_t = 0; } }
  if (RELEASED(RETRO_DEVICE_ID_JOYPAD_RIGHT)) { held_r = false; if (dir == 1)  { dir = held_l ? -1 : 0; das_t = arr_t = 0; } }
  held_d = now[RETRO_DEVICE_ID_JOYPAD_DOWN];

  bool start = PRESSED(RETRO_DEVICE_ID_JOYPAD_START), a = PRESSED(RETRO_DEVICE_ID_JOYPAD_A);
  if (PRESSED(RETRO_DEVICE_ID_JOYPAD_SELECT)) muted = !muted;

  if (state == ST_BOOT) { if (start && state_t > 0.3) set_state(ST_TITLE); }
  else if (state == ST_TITLE) { if (start || a) { sfx_click(); new_game(); } }
  else if (state == ST_OVER) { if (state_t > 1 && (start || a)) { sfx_click(); new_game(); } }
  else if (start && (state == ST_PLAYING || state == ST_PAUSED)) {
    set_state(state == ST_PLAYING ? ST_PAUSED : ST_PLAYING);
  } else if (state == ST_PLAYING && has_cur) {
    if (PRESSED(RETRO_DEVICE_ID_JOYPAD_LEFT))  { held_l = true; dir = -1; das_t = arr_t = 0; move(-1); }
    if (PRESSED(RETRO_DEVICE_ID_JOYPAD_RIGHT)) { held_r = true; dir = 1;  das_t = arr_t = 0; move(1); }
    if (a) rotate(1);
    if (PRESSED(RETRO_DEVICE_ID_JOYPAD_B)) rotate(-1);
    if (PRESSED(RETRO_DEVICE_ID_JOYPAD_DOWN)) { soft_t = -SOFT_DELAY; soft_step(); }
    if (PRESSED(RETRO_DEVICE_ID_JOYPAD_X) || PRESSED(RETRO_DEVICE_ID_JOYPAD_Y) ||
        PRESSED(RETRO_DEVICE_ID_JOYPAD_L) || PRESSED(RETRO_DEVICE_ID_JOYPAD_R)) do_hold();
    if (has_cur && PRESSED(RETRO_DEVICE_ID_JOYPAD_UP)) hard_drop();
  }
  memcpy(prev, now, sizeof prev);
}

/* ------------------------------------------------------------------ update */
static void update(void) {
  gtime += DT;
  state_t += DT;
  if (shake > 0) shake -= DT;
  int k = 0;
  for (int i = 0; i < npop; i++) { popups[i].t += DT; if (popups[i].t < 1.2) popups[k++] = popups[i]; }
  npop = k;

  if (state == ST_BOOT) {
    if (state_t >= 1.1 && state_t - DT < 1.1) sfx_ding();
    if (state_t >= BOOT_TIME) set_state(ST_TITLE);
    return;
  }
  if (state == ST_CLEARING) { clear_t += DT; if (clear_t >= CLEAR_TIME) finish_clear(); return; }
  if (state == ST_PLAYING) play_time += DT;
  if (state != ST_PLAYING || !has_cur) return;

  if (dir) {
    das_t += DT;
    if (das_t >= DAS) {
      arr_t += DT;
      while (arr_t >= ARR) { arr_t -= ARR; if (!move(dir)) { arr_t = 0; break; } }
    }
  }
  if (held_d) {
    soft_t += DT;
    while (soft_t >= SOFT_RATE) { soft_t -= SOFT_RATE; if (!soft_step()) { soft_t = 0; break; } }
  }

  double g = gravity();
  fall_acc += DT;
  while (fall_acc >= g) {
    fall_acc -= g;
    if (grounded()) { fall_acc = 0; break; }
    cur.y++;
    if (cur.y > lowest_y) { lowest_y = cur.y; resets = 0; }
  }
  if (grounded()) { lock_t += DT; if (lock_t >= LOCK_DELAY) lock_piece(); }
  else lock_t = 0;
}

/* ------------------------------------------------------------------ render */
static bool blink(double period) { double t = gtime - (int)(gtime / period) * period; return t < period / 2; }

static void dot_bg(int x0, int y0, int w, int h, uint32_t bg, uint32_t dot, int step) {
  rect(x0, y0, w, h, bg, 255);
  for (int y = 2; y < h; y += step)
    for (int x = 2; x < w; x += step) put(x0 + x, y0 + y, dot, 255);
}
static void panel(int x, int y, int w, int h) { rect(x - 1, y - 1, w + 2, h + 2, C_FRAMELITE, 255); rect(x, y, w, h, C_WELL, 255); }

typedef struct { const char *s; uint32_t c; int k; } Line;
static void dialog(int x, int y, int w, int h, const char *title, uint32_t bar, const Line *body, int n, const char *button) {
  rect(x + 2, y + 2, w, h, 0x05070f, 153);
  rect(x - 1, y - 1, w + 2, h + 2, C_NAVY, 255);
  rect(x, y, w, h, C_CREAM, 255);
  rect(x, y, w, 9, bar, 255);
  text(title, x + 3, y + 2, C_CREAM, 1);
  for (int i = 0; i < 2; i++) {
    int bx = x + w - 9 - i * 8;
    rect(bx, y + 1, 7, 7, C_NAVY, 255); rect(bx + 1, y + 2, 5, 5, C_CREAM, 255);
    if (i == 0) for (int q = 0; q < 3; q++) { put(bx + 2 + q, y + 3 + q, C_NAVY, 255); put(bx + 4 - q, y + 3 + q, C_NAVY, 255); }
    else rect(bx + 2, y + 5, 3, 1, C_NAVY, 255);
  }
  int ty = y + 14;
  for (int i = 0; i < n; i++) { text_c(body[i].s, x + w / 2, ty, body[i].c, body[i].k); ty += 5 * body[i].k + 4; }
  if (button) {
    int bw = text_w(button, 1) + 10, bx = x + w / 2 - bw / 2, by = y + h - 14;
    bool on = blink(0.9);
    rect(bx - 1, by - 1, bw + 3, 12, C_NAVY, 255);
    rect(bx, by, bw, 9, on ? C_TEAL : C_CREAMSHADE, 255);
    rect(bx, by + 8, bw, 1, on ? 0x1f7389 : 0xb9ad8a, 255);
    text_c(button, bx + bw / 2, by + 2, on ? C_CREAM : C_NAVY, 1);
  }
}

static void draw_preview(int type, int cx, int cy, int alpha) {
  int minx = 9, miny = 9, maxx = -1, maxy = -1;
  for (int i = 0; i < 4; i++) {
    int x = SHAPES[type][0][i][0], y = SHAPES[type][0][i][1];
    if (x < minx) minx = x; if (x > maxx) maxx = x;
    if (y < miny) miny = y; if (y > maxy) maxy = y;
  }
  int w = (maxx - minx + 1) * T, h = (maxy - miny + 1) * T;
  int ox = cx - w / 2, oy = cy - h / 2;
  for (int i = 0; i < 4; i++)
    tile(type, ox + (SHAPES[type][0][i][0] - minx) * T, oy + (SHAPES[type][0][i][1] - miny) * T, alpha);
}

static void draw_game(void) {
  char buf[32];
  dot_bg(0, 0, LCD_W, LCD_H, C_LCD, C_LCDDOT, 4);

  text("HOLD", 4, 4, C_LABEL, 1);
  panel(4, 11, 38, 26);
  if (hold >= 0) draw_preview(hold, 4 + 19, 11 + 13, can_hold ? 255 : 89);
  text("LEVEL", 4, 44, C_LABEL, 1);
  snprintf(buf, sizeof buf, "%d", level); text(buf, 4, 51, C_VALUE, 2);
  text("LINES", 4, 68, C_LABEL, 1);
  snprintf(buf, sizeof buf, "%d", lines); text(buf, 4, 75, C_VALUE, 2);
  text("TIME", 4, 92, C_LABEL, 1);
  int secs = (int)play_time;
  snprintf(buf, sizeof buf, "%d:%02d", secs / 60, secs % 60); text(buf, 4, 99, C_VALUE, 2);
  text("TETRIS", 4, 116, C_LABEL, 1);
  snprintf(buf, sizeof buf, "%d", tetrises); text(buf, 4, 123, C_PINK, 2);
  text("SOUND", 4, 144, C_LABEL, 1);
  text(muted ? "OFF" : "ON", 4, 151, muted ? C_CORAL : C_SAGE, 1);

  text("NEXT", 134, 4, C_LABEL, 1);
  panel(134, 11, 38, 76);
  for (int i = 0; i < 3 && i < qlen; i++) draw_preview(queue[i], 134 + 19, 11 + 13 + i * 25, 255);
  text("SCORE", 134, 94, C_LABEL, 1);
  snprintf(buf, sizeof buf, "%d", score); text(buf, 134, 101, C_VALUE, 1);
  text("BEST", 134, 112, C_LABEL, 1);
  snprintf(buf, sizeof buf, "%d", score > best ? score : best); text(buf, 134, 119, C_YELLOW, 1);
  text("GOAL", 134, 132, C_LABEL, 1);
  int done = lines % 10;
  for (int i = 0; i < 10; i++) {
    rect(134 + i * 4, 139, 3, 7, i < done ? RAINBOW[i % 6] : C_WELL, 255);
    if (i < done) rect(134 + i * 4, 139, 3, 1, 0xffffff, 115);
  }
  snprintf(buf, sizeof buf, "%d TO GO", 10 - done); text(buf, 134, 150, C_DIM, 1);

  /* well */
  rect(WELL_X - 2, WELL_Y - 2, 84, 164, C_FRAMEDARK, 255);
  rect(WELL_X - 1, WELL_Y - 1, 82, 162, C_FRAMELITE, 255);
  int sy = shake > 0 ? 1 : 0;
  int wx = WELL_X, wy = WELL_Y + sy;
  dot_bg(wx, wy, COLS * T, 20 * T, C_WELL, C_WELLDOT, 8);
  int dead_rows = state == ST_OVER ? (int)(state_t * 26) : -1;
  for (int y = HIDDEN; y < ROWS; y++) {
    int vy = y - HIDDEN;
    for (int x = 0; x < COLS; x++) {
      if (vy < dead_rows) tile(DEAD, wx + x * T, wy + vy * T, 255);
      else if (board[y][x] >= 0) tile(board[y][x], wx + x * T, wy + vy * T, 255);
    }
  }
  if (state == ST_CLEARING) {
    bool on = ((int)(clear_t / 0.06)) % 2 == 0;
    double f = 1 - clear_t / CLEAR_TIME; if (f < 0) f = 0;
    int w = (int)(COLS * T * f);
    for (int i = 0; i < nclear; i++) rect(wx + (COLS * T - w) / 2, wy + (clear_rows[i] - HIDDEN) * T, w, T, C_CREAM, on ? 230 : 38);
  }
  if (has_cur && (state == ST_PLAYING || state == ST_PAUSED)) {
    int gd = drop_distance();
    for (int i = 0; i < 4; i++) {
      int gy = cur.y + SHAPES[cur.type][cur.rot][i][1] + gd - HIDDEN;
      if (gy >= 0) tile(GHOST, wx + (cur.x + SHAPES[cur.type][cur.rot][i][0]) * T, wy + gy * T, 255);
    }
    for (int i = 0; i < 4; i++) {
      int y = cur.y + SHAPES[cur.type][cur.rot][i][1] - HIDDEN;
      if (y >= 0) tile(cur.type, wx + (cur.x + SHAPES[cur.type][cur.rot][i][0]) * T, wy + y * T, 255);
    }
  }
  for (int i = 0; i < npop; i++) {
    int y = (int)(64 - popups[i].t * 24 + 0.5);
    if (popups[i].t > 0.9 && ((int)(popups[i].t * 20)) % 2) continue;
    text_c(popups[i].text, wx + COLS * T / 2 + 1, wy + y + 1, C_FRAMEDARK, 2);
    text_c_multi(popups[i].text, wx + COLS * T / 2, wy + y, RAINBOW, 6, 2);
  }

  if (state == ST_PAUSED) {
    rect(WELL_X, WELL_Y, COLS * T, 20 * T, 0x0c1122, 217);
    Line body[2] = { { "TAKE A BREAK", C_NAVY, 1 }, { "@", C_CORAL, 1 } };
    dialog(28, 56, 120, 52, "PAUSED", C_MUSTARD, body, 2, "RESUME");
  }
  if (state == ST_OVER && state_t > 0.9) {
    snprintf(buf, sizeof buf, "%d", score);
    Line body[3] = { { "SCORE", C_DIM, 1 }, { buf, C_NAVY, 2 }, { "NEW BEST!", C_CORAL, 1 } };
    dialog(28, 46, 120, new_best ? 76 : 66, "GAME OVER", C_CORAL, body, new_best ? 3 : 2, "RETRY");
  }
}

static int SKY[22][4], SKY_H[22] = { 2,3,3,1,2,4,4,2,1,3,2,2,3,4,1,1,2,3,3,2,4,3 };

static void draw_title(void) {
  char buf[32];
  dot_bg(0, 0, LCD_W, LCD_H, C_LCD, C_LCDDOT, 4);
  for (int x = 0; x < 22; x++)
    for (int i = 0; i < SKY_H[x]; i++) tile(SKY[x][i], x * T, LCD_H - (i + 1) * T, 255);
  blit(IMG_LOGO, IMG_LOGO_W, 0, 0, IMG_LOGO_W, IMG_LOGO_H, 19, 12, 1, 255, -1);
  snprintf(buf, sizeof buf, "HI %06d", best);
  uint32_t y = C_YELLOW; shadow_text(buf, 88, 50, &y, 1, 1);
  Line body[2] = { { "READY TO PLAY?", C_NAVY, 1 }, { "@ KOROBEINIKI @", C_CORAL, 1 } };
  dialog(30, 66, 116, 54, "WELCOME.EXE", C_TEAL, body, 2, "START");
  static const int tw[6][2] = { { 12, 58 }, { 160, 48 }, { 150, 84 }, { 18, 96 }, { 100, 128 }, { 60, 132 } };
  for (int i = 0; i < 6; i++) if (((int)(gtime / 0.4) + i) % 3) text("*", tw[i][0], tw[i][1], RAINBOW[i], 1);
}

static void draw_boot(void) {
  dot_bg(0, 0, LCD_W, LCD_H, C_LCD, C_LCDDOT, 4);
  double t = state_t / 1.1; if (t > 1) t = 1;
  blit(IMG_LOGO, IMG_LOGO_W, 0, 0, IMG_LOGO_W, IMG_LOGO_H, 19, (int)(-32 + 100 * t + 0.5), 1, 255, -1);
  if (state_t > 1.2) shadow_text("PIXEL BOY", 88, 108, RAINBOW, 6, 1);
  if (state_t > 1.5) text_c("2026 VIRGILIO SOFT", 88, 122, C_DIM, 1);
}

static void glass(void) {
  for (int y = 1; y < LCD_H; y += 2) rect(0, y, LCD_W, 1, 0x000000, 26);
  for (int i = 0; i < 26; i++) { rect(140 + i, i, 3, 1, 0xffffff, 13); put(152 + i, i, 0xffffff, 13); }
}

static void draw_bezel(void) {
  static const char *label = "DOT MATRIX WITH STEREO SOUND";
  rect(0, 0, SW, SH, C_BEZEL, 255);
  int lx = SW / 2 - text_w(label, 1) / 2, rx = lx + text_w(label, 1) + 4;
  text(label, lx, 3, 0xc9cfe6, 1);
  rect(6, 4, lx - 10, 1, C_CORAL, 255); rect(6, 6, lx - 10, 1, C_TEAL, 255);
  rect(rx, 4, SW - 6 - rx, 1, C_CORAL, 255); rect(rx, 6, SW - 6 - rx, 1, C_TEAL, 255);
  rect(LCD_X - 1, LCD_Y - 1, LCD_W + 2, LCD_H + 2, 0x0c1020, 255);

  /* left: brand + power LED */
  static const uint32_t brand[5] = { C_CORAL, 0xf28c3b, C_SAGE, C_LABEL, C_LAV };
  text_multi("PIXEL", 20 - text_w("PIXEL", 1) / 2, 30, brand, 5, 1);
  text_c("BOY", 20, 37, C_CREAM, 1);
  circle(20.5f, 96.5f, 3, 0x2a2f45);
  circle(20.5f, 96.5f, 2.2f, 0xff5a73);
  put(19, 95, 0xffd0d8, 255);
  text_c("PWR", 20, 103, 0x9aa3c4, 1);

  /* right: control hints */
  static const char *hints[6][2] = {
    { "A B", "ROTATE" }, { "UP", "DROP" }, { "X Y L R", "HOLD" },
    { "DOWN", "SOFT" }, { "START", "PAUSE" }, { "SELECT", "MUTE" },
  };
  for (int i = 0; i < 6; i++) {
    text_c(hints[i][0], 236, 30 + i * 22, C_CREAM, 1);
    text_c(hints[i][1], 236, 37 + i * 22, C_DIM, 1);
  }
}

static void render(void) {
  view(0, 0, SW, SH);
  draw_bezel();
  view(LCD_X, LCD_Y, LCD_W, LCD_H);
  if (state == ST_BOOT) draw_boot();
  else if (state == ST_TITLE) draw_title();
  else draw_game();
  glass();
  view(0, 0, SW, SH);

  for (int y = 0; y < SH; y++) {
    uint32_t *line = &out[y * SCALE * OW];
    const uint32_t *src = &fb[y * SW];
    for (int x = 0; x < SW; x++) {
      uint32_t c = src[x];
      line[x * 4] = line[x * 4 + 1] = line[x * 4 + 2] = line[x * 4 + 3] = c;
    }
    for (int r = 1; r < SCALE; r++) memcpy(line + r * OW, line, OW * sizeof(uint32_t));
  }
}

/* ------------------------------------------------------------------ libretro API */
static void init_tables(void) {
  for (int t = 0; t < 7; t++) {
    int n = BASE_N[t];
    char m[4][4] = { { 0 } }, r[4][4] = { { 0 } };
    for (int y = 0; y < n; y++) for (int x = 0; x < n; x++) m[y][x] = BASE[t][y * n + x] == '#';
    for (int rot = 0; rot < 4; rot++) {
      int c = 0;
      for (int y = 0; y < n; y++) for (int x = 0; x < n; x++)
        if (m[y][x]) { SHAPES[t][rot][c][0] = x; SHAPES[t][rot][c][1] = y; c++; }
      for (int y = 0; y < n; y++) for (int x = 0; x < n; x++) r[y][x] = m[n - 1 - x][y];
      memcpy(m, r, sizeof m);
    }
  }
  double f = 440;
  HZ[69] = 440;
  for (int i = 70; i < 128; i++) HZ[i] = (f *= 1.0594630943592953);
  f = 440;
  for (int i = 68; i >= 0; i--) HZ[i] = (f /= 1.0594630943592953);

  int n = 0;
  for (int rep = 0; rep < 2; rep++) for (int i = 0; i < NA; i++) MELODY[n++] = PART_A[i];
  for (int i = 0; i < NC; i++) MELODY[n++] = PART_C[i];
  for (int b = 0; b < 24; b++)
    for (int i = 0; i < 8; i++) { BASS[b * 8 + i].note = i % 2 ? BASS_ROOTS[b] + 12 : BASS_ROOTS[b]; BASS[b * 8 + i].beats = .5f; }
  tracks[0] = (Track){ MELODY, NA * 2 + NC, 0, 0, OSC_SQUARE, 0.16f, 0.88f };
  tracks[1] = (Track){ BASS, 24 * 8, 0, 0, OSC_TRIANGLE, 0.32f, 0.8f };

  uint32_t s = 7;
  for (int x = 0; x < 22; x++)
    for (int i = 0; i < SKY_H[x]; i++) { s = s * 1103515245u + 12345u; SKY[x][i] = (int)((s >> 16) % 7); }
}

RETRO_API void retro_set_environment(retro_environment_t cb) {
  env_cb = cb;
  bool no_game = true;
  cb(RETRO_ENVIRONMENT_SET_SUPPORT_NO_GAME, &no_game);
}
RETRO_API void retro_set_video_refresh(retro_video_refresh_t cb) { video_cb = cb; }
RETRO_API void retro_set_audio_sample(retro_audio_sample_t cb) { (void)cb; }
RETRO_API void retro_set_audio_sample_batch(retro_audio_sample_batch_t cb) { audio_batch_cb = cb; }
RETRO_API void retro_set_input_poll(retro_input_poll_t cb) { input_poll_cb = cb; }
RETRO_API void retro_set_input_state(retro_input_state_t cb) { input_state_cb = cb; }

RETRO_API void retro_init(void) { init_tables(); }
RETRO_API void retro_deinit(void) {}
RETRO_API unsigned retro_api_version(void) { return RETRO_API_VERSION; }

RETRO_API void retro_get_system_info(struct retro_system_info *info) {
  memset(info, 0, sizeof *info);
  info->library_name = "Pixel Boy Tetris";
  info->library_version = "1.0";
  info->valid_extensions = "";
  info->need_fullpath = false;
  info->block_extract = false;
}
RETRO_API void retro_get_system_av_info(struct retro_system_av_info *info) {
  memset(info, 0, sizeof *info);
  info->geometry.base_width = OW; info->geometry.base_height = OH;
  info->geometry.max_width = OW; info->geometry.max_height = OH;
  info->geometry.aspect_ratio = 4.0f / 3.0f;
  info->timing.fps = FPS;
  info->timing.sample_rate = SR;
}
RETRO_API void retro_set_controller_port_device(unsigned port, unsigned device) { (void)port; (void)device; }

RETRO_API void retro_reset(void) {
  memset(voices, 0, sizeof voices);
  music_on = false; has_cur = false; board[0][0] = -1;
  set_state(ST_TITLE);
}

RETRO_API void retro_run(void) {
  handle_input();
  update();
  render();
  video_cb(out, OW, OH, OW * sizeof(uint32_t));
  render_audio(state == ST_PAUSED);
  audio_batch_cb(abuf, SPF);
}

RETRO_API bool retro_load_game(const struct retro_game_info *game) {
  (void)game;
  enum retro_pixel_format fmt = RETRO_PIXEL_FORMAT_XRGB8888;
  if (!env_cb(RETRO_ENVIRONMENT_SET_PIXEL_FORMAT, &fmt)) return false;

  static struct retro_input_descriptor desc[] = {
    { 0, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_LEFT, "Move left" },
    { 0, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_RIGHT, "Move right" },
    { 0, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_DOWN, "Soft drop" },
    { 0, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_UP, "Hard drop" },
    { 0, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_Y, "Hold" },
    { 0, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_A, "Rotate clockwise" },
    { 0, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_B, "Rotate counter-clockwise" },
    { 0, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_X, "Hold" },
    { 0, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_L, "Hold" },
    { 0, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_R, "Hold" },
    { 0, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_START, "Start / pause" },
    { 0, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_SELECT, "Mute" },
    { 0, 0, 0, 0, NULL },
  };
  env_cb(RETRO_ENVIRONMENT_SET_INPUT_DESCRIPTORS, desc);

  const char *dir_path = NULL;
  if (env_cb(RETRO_ENVIRONMENT_GET_SAVE_DIRECTORY, &dir_path) && dir_path && dir_path[0])
    snprintf(save_path, sizeof save_path, "%s/pixeltetris.hi", dir_path);
  load_best();

  rng ^= (uint32_t)time(NULL) * 2654435761u;
  if (!rng) rng = 1;
  memset(board, -1, sizeof board);
  sclock = 0;
  set_state(ST_BOOT);
  return true;
}
RETRO_API bool retro_load_game_special(unsigned type, const struct retro_game_info *info, size_t num) {
  (void)type; (void)info; (void)num; return false;
}
RETRO_API void retro_unload_game(void) {}
RETRO_API unsigned retro_get_region(void) { return RETRO_REGION_NTSC; }
RETRO_API size_t retro_serialize_size(void) { return 0; }
RETRO_API bool retro_serialize(void *data, size_t size) { (void)data; (void)size; return false; }
RETRO_API bool retro_unserialize(const void *data, size_t size) { (void)data; (void)size; return false; }
RETRO_API void retro_cheat_reset(void) {}
RETRO_API void retro_cheat_set(unsigned index, bool enabled, const char *code) { (void)index; (void)enabled; (void)code; }
RETRO_API void *retro_get_memory_data(unsigned id) { (void)id; return NULL; }
RETRO_API size_t retro_get_memory_size(unsigned id) { (void)id; return 0; }
