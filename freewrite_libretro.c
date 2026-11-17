/*
 * Scriptwriter core for libretro (RG35XX / GarlicOS)
 *
 * A small screenplay editor that behaves like a "game". Type with a USB
 * keyboard; elements (scene heading, action, character, parenthetical,
 * dialogue, transition) are formatted as you type. The script is saved as
 * plain Fountain text (.fountain) in RetroArch's save directory, so it
 * imports into WriterDuet, Highland, Final Draft, etc.
 *
 * Editing model: append-only at the end of the script (like a typewriter /
 * freewrite device). Backspace works; there is no cursor movement.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <stdbool.h>
#include <time.h>
#include <ctype.h>
#include <unistd.h>
#include "libretro.h"

#define W 640
#define H 480
#define MX 16
#define MY 12
#define HUDH 28
#define MAX_TEXT (1 << 20)
#define WIN 6000
#define MAXL 8192
#define MAXV 9000

enum { T_ACTION, T_SCENE, T_CHAR, T_PAREN, T_DIAL, T_TRANS, T_BLANK };
enum { F_NONE, F_SCENE, F_CHAR, F_TRANS };

static const char *type_names[] = {
   "ACTION", "SCENE", "CHARACTER", "PAREN", "DIALOGUE", "TRANSITION", "ACTION"
};

/* ---------------------------------------------------------------- font */
/* Classic 5x7 font, 5 column bytes per glyph, bit0 = top row. ASCII 32..126 */
static const uint8_t font5x7[95][5] = {
   {0x00,0x00,0x00,0x00,0x00},{0x00,0x00,0x5F,0x00,0x00},{0x00,0x07,0x00,0x07,0x00},
   {0x14,0x7F,0x14,0x7F,0x14},{0x24,0x2A,0x7F,0x2A,0x12},{0x23,0x13,0x08,0x64,0x62},
   {0x36,0x49,0x55,0x22,0x50},{0x00,0x05,0x03,0x00,0x00},{0x00,0x1C,0x22,0x41,0x00},
   {0x00,0x41,0x22,0x1C,0x00},{0x08,0x2A,0x1C,0x2A,0x08},{0x08,0x08,0x3E,0x08,0x08},
   {0x00,0x50,0x30,0x00,0x00},{0x08,0x08,0x08,0x08,0x08},{0x00,0x60,0x60,0x00,0x00},
   {0x20,0x10,0x08,0x04,0x02},{0x3E,0x51,0x49,0x45,0x3E},{0x00,0x42,0x7F,0x40,0x00},
   {0x42,0x61,0x51,0x49,0x46},{0x21,0x41,0x45,0x4B,0x31},{0x18,0x14,0x12,0x7F,0x10},
   {0x27,0x45,0x45,0x45,0x39},{0x3C,0x4A,0x49,0x49,0x30},{0x01,0x71,0x09,0x05,0x03},
   {0x36,0x49,0x49,0x49,0x36},{0x06,0x49,0x49,0x29,0x1E},{0x00,0x36,0x36,0x00,0x00},
   {0x00,0x56,0x36,0x00,0x00},{0x00,0x08,0x14,0x22,0x41},{0x14,0x14,0x14,0x14,0x14},
   {0x41,0x22,0x14,0x08,0x00},{0x02,0x01,0x51,0x09,0x06},{0x32,0x49,0x79,0x41,0x3E},
   {0x7E,0x11,0x11,0x11,0x7E},{0x7F,0x49,0x49,0x49,0x36},{0x3E,0x41,0x41,0x41,0x22},
   {0x7F,0x41,0x41,0x22,0x1C},{0x7F,0x49,0x49,0x49,0x41},{0x7F,0x09,0x09,0x09,0x01},
   {0x3E,0x41,0x49,0x49,0x7A},{0x7F,0x08,0x08,0x08,0x7F},{0x00,0x41,0x7F,0x41,0x00},
   {0x20,0x40,0x41,0x3F,0x01},{0x7F,0x08,0x14,0x22,0x41},{0x7F,0x40,0x40,0x40,0x40},
   {0x7F,0x02,0x0C,0x02,0x7F},{0x7F,0x04,0x08,0x10,0x7F},{0x3E,0x41,0x41,0x41,0x3E},
   {0x7F,0x09,0x09,0x09,0x06},{0x3E,0x41,0x51,0x21,0x5E},{0x7F,0x09,0x19,0x29,0x46},
   {0x46,0x49,0x49,0x49,0x31},{0x01,0x01,0x7F,0x01,0x01},{0x3F,0x40,0x40,0x40,0x3F},
   {0x1F,0x20,0x40,0x20,0x1F},{0x3F,0x40,0x38,0x40,0x3F},{0x63,0x14,0x08,0x14,0x63},
   {0x07,0x08,0x70,0x08,0x07},{0x61,0x51,0x49,0x45,0x43},{0x00,0x7F,0x41,0x41,0x00},
   {0x02,0x04,0x08,0x10,0x20},{0x00,0x41,0x41,0x7F,0x00},{0x04,0x02,0x01,0x02,0x04},
   {0x40,0x40,0x40,0x40,0x40},{0x00,0x01,0x02,0x04,0x00},{0x20,0x54,0x54,0x54,0x78},
   {0x7F,0x48,0x44,0x44,0x38},{0x38,0x44,0x44,0x44,0x20},{0x38,0x44,0x44,0x48,0x7F},
   {0x38,0x54,0x54,0x54,0x18},{0x08,0x7E,0x09,0x01,0x02},{0x0C,0x52,0x52,0x52,0x3E},
   {0x7F,0x08,0x04,0x04,0x78},{0x00,0x44,0x7D,0x40,0x00},{0x20,0x40,0x44,0x3D,0x00},
   {0x7F,0x10,0x28,0x44,0x00},{0x00,0x41,0x7F,0x40,0x00},{0x7C,0x04,0x18,0x04,0x78},
   {0x7C,0x08,0x04,0x04,0x78},{0x38,0x44,0x44,0x44,0x38},{0x7C,0x14,0x14,0x14,0x08},
   {0x08,0x14,0x14,0x18,0x7C},{0x7C,0x08,0x04,0x04,0x08},{0x48,0x54,0x54,0x54,0x20},
   {0x04,0x3F,0x44,0x40,0x20},{0x3C,0x40,0x40,0x20,0x7C},{0x1C,0x20,0x40,0x20,0x1C},
   {0x3C,0x40,0x30,0x40,0x3C},{0x44,0x28,0x10,0x28,0x44},{0x0C,0x50,0x50,0x50,0x3C},
   {0x44,0x64,0x54,0x4C,0x44},{0x00,0x08,0x36,0x41,0x00},{0x00,0x00,0x7F,0x00,0x00},
   {0x00,0x41,0x36,0x08,0x00},{0x08,0x04,0x08,0x10,0x08}
};

/* -------------------------------------------------------------- state */
static retro_environment_t env_cb;
static retro_video_refresh_t video_cb;
static retro_audio_sample_t audio_cb;
static retro_audio_sample_batch_t audio_batch_cb;
static retro_input_poll_t input_poll_cb;
static retro_input_state_t input_state_cb;

static uint16_t fb[W * H];
static int16_t silence[735 * 2];

static char text[MAX_TEXT];
static int len;
static int cur_force;           /* element forced for the line being typed */
static int dirty = 1;
static int changed;             /* unsaved edits */
static int started;
static unsigned long frame, session_frames;
static int last_blink = -1;
static int sc = 3;              /* text scale: 2 or 3 */
static int show_hud = 1;
static int show_help;
static int words_cache, words_valid;

static char save_dir[512];
static char save_path[1024];
static int save_err;
static unsigned long last_save_frame;

/* key repeat */
static unsigned held_key;
static uint32_t held_char;
static uint16_t held_mod;
static unsigned held_frames;
static int frontend_repeats;
static int prev_start, prev_r;

/* ------------------------------------------------------------ colours */
#define RGB(r, g, b) ((uint16_t)((((r) & 0xF8) << 8) | (((g) & 0xFC) << 3) | ((b) >> 3)))
#define C_BG    RGB(18, 18, 22)
#define C_TEXT  RGB(226, 222, 210)
#define C_SCENE RGB(244, 204, 120)
#define C_CHAR  RGB(160, 214, 255)
#define C_PAREN RGB(170, 170, 160)
#define C_TRANS RGB(222, 160, 160)
#define C_DIM   RGB(120, 120, 132)
#define C_RULE  RGB(60, 60, 72)

/* ------------------------------------------------------------ drawing */
static void fill_rect(int x, int y, int w, int h, uint16_t col)
{
   if (x < 0) { w += x; x = 0; }
   if (y < 0) { h += y; y = 0; }
   if (x + w > W) w = W - x;
   if (y + h > H) h = H - y;
   if (w <= 0 || h <= 0) return;
   for (int j = 0; j < h; j++) {
      uint16_t *p = fb + (y + j) * W + x;
      for (int i = 0; i < w; i++) p[i] = col;
   }
}

static void draw_glyph(int x, int y, int c, int s, uint16_t col)
{
   if (c < 32 || c > 126) c = '?';
   const uint8_t *g = font5x7[c - 32];
   for (int cx = 0; cx < 5; cx++)
      for (int r = 0; r < 7; r++)
         if (g[cx] & (1 << r)) fill_rect(x + cx * s, y + r * s, s, s, col);
}

static void draw_text(int x, int y, const char *s, int n, int scale,
      uint16_t col, int bold)
{
   for (int i = 0; i < n; i++) {
      draw_glyph(x + i * 6 * scale, y, (unsigned char)s[i], scale, col);
      if (bold) draw_glyph(x + i * 6 * scale + 1, y, (unsigned char)s[i], scale, col);
   }
}

/* ------------------------------------------------------ text analysis */
static int is_upper_line(const char *s, int n)
{
   int letters = 0, depth = 0;
   for (int i = 0; i < n; i++) {
      char c = s[i];
      if (c == '(') depth++;
      else if (c == ')') { if (depth) depth--; }
      else if (!depth) {
         if (islower((unsigned char)c)) return 0;
         if (isupper((unsigned char)c)) letters++;
      }
   }
   return letters > 0;
}

static int has_prefix_nocase(const char *s, int n, const char *p)
{
   int m = (int)strlen(p);
   if (n < m) return 0;
   for (int i = 0; i < m; i++)
      if (toupper((unsigned char)s[i]) != p[i]) return 0;
   return 1;
}

static int is_scene(const char *s, int n)
{
   static const char *pre[] = {
      "INT.", "INT ", "EXT.", "EXT ", "EST.", "EST ", "I/E.", "I/E ", NULL
   };
   if (n >= 2 && s[0] == '.' && s[1] != '.') return 1;
   for (int i = 0; pre[i]; i++)
      if (has_prefix_nocase(s, n, pre[i])) return 1;
   return 0;
}

static int ends_with_to(const char *s, int n)
{
   return n >= 3 && s[n - 1] == ':' && s[n - 2] == 'O' && s[n - 3] == 'T';
}

/* Parsed window (tail of the script, starting at a paragraph boundary) */
static int ws, nl;
static int ls[MAXL], ll[MAXL];
static unsigned char lt[MAXL];

static int forced_type(void)
{
   switch (cur_force) {
   case F_SCENE: return T_SCENE;
   case F_CHAR: return T_CHAR;
   case F_TRANS: return T_TRANS;
   }
   return T_ACTION;
}

static int line_blank(int i)
{
   if (ll[i] != 0) return 0;
   if (i == nl - 1 && ((i > 0 && ll[i - 1] > 0) || cur_force)) return 0;
   return 1;
}

static void parse(void)
{
   ws = 0;
   if (len > WIN) {
      int p = len - WIN, q = -1;
      for (int i = p; i + 1 < len; i++)
         if (text[i] == '\n' && text[i + 1] == '\n') { q = i + 2; break; }
      if (q < 0)
         for (int i = p; i < len; i++)
            if (text[i] == '\n') { q = i + 1; break; }
      ws = q < 0 ? p : q;
   }

   nl = 0;
   for (int i = ws; nl < MAXL;) {
      int j = i;
      while (j < len && text[j] != '\n') j++;
      ls[nl] = i; ll[nl] = j - i; nl++;
      if (j >= len) break;
      i = j + 1;
   }

   for (int i = 0; i < nl;) {
      if (line_blank(i)) { lt[i] = T_BLANK; i++; continue; }
      int e = i + 1;
      while (e < nl && !line_blank(e)) e++;
      const char *s = text + ls[i];
      int n = ll[i], t;
      if (is_scene(s, n)) t = T_SCENE;
      else if (n > 0 && s[0] == '>' && s[n - 1] != '<') t = T_TRANS;
      else if (e - i == 1 && n >= 3 && is_upper_line(s, n) && ends_with_to(s, n)) t = T_TRANS;
      else if (n > 0 && s[0] == '@') t = T_CHAR;
      else if (e - i >= 2 && is_upper_line(s, n)) t = T_CHAR;
      else t = T_ACTION;
      if (i == nl - 1 && cur_force) t = forced_type();
      lt[i] = (unsigned char)t;
      for (int k = i + 1; k < e; k++) {
         if (t == T_CHAR)
            lt[k] = (ll[k] > 0 && text[ls[k]] == '(') ? T_PAREN : T_DIAL;
         else
            lt[k] = T_ACTION;
      }
      i = e;
   }
}

/* ------------------------------------------------------------ editing */
static void put(char c)
{
   if (len >= MAX_TEXT - 1) return;
   text[len++] = c;
   text[len] = 0;
   dirty = 1; changed = 1; words_valid = 0; started = 1;
}

static int last_line_start(void)
{
   int s = len;
   while (s > 0 && text[s - 1] != '\n') s--;
   return s;
}

static void upcase_last_line(void)
{
   for (int i = last_line_start(); i < len; i++)
      text[i] = (char)toupper((unsigned char)text[i]);
}

static void type_char(char c)
{
   put(c);
   parse();
   int t = lt[nl - 1];
   if (t == T_SCENE || t == T_TRANS || t == T_CHAR) upcase_last_line();
}

static void do_backspace(void)
{
   if (len > 0) {
      len--;
      text[len] = 0;
      dirty = 1; changed = 1; words_valid = 0;
   }
   cur_force = F_NONE;
}

static void do_enter(int soft)
{
   parse();
   int t = lt[nl - 1];
   int n = ll[nl - 1];
   if (soft || n == 0) { put('\n'); cur_force = F_NONE; return; }

   switch (t) {
   case T_SCENE: {
      int s = last_line_start();
      if (!is_scene(text + s, len - s) && len < MAX_TEXT - 2) {
         memmove(text + s + 1, text + s, (size_t)(len - s));
         text[s] = '.';
         len++; text[len] = 0;
      }
      put('\n'); put('\n'); cur_force = F_NONE;
      break;
   }
   case T_CHAR:
      put('\n'); cur_force = F_NONE;
      break;
   case T_PAREN:
      put('\n'); cur_force = F_NONE;
      break;
   case T_DIAL:
      put('\n'); put('\n'); cur_force = F_CHAR;
      break;
   case T_TRANS:
      put('\n'); put('\n'); cur_force = F_SCENE;
      break;
   default:
      put('\n'); put('\n'); cur_force = F_NONE;
      break;
   }
}

/* Tab on an empty line cycles: scene -> character -> transition -> action */
static void do_tab(void)
{
   int s = last_line_start();
   int n = len - s;
   const char *ln = text + s;
   switch (cur_force) {
   case F_NONE:
      if (n == 0) {
         cur_force = F_SCENE;
         put('I'); put('N'); put('T'); put('.'); put(' ');
      }
      break;
   case F_SCENE:
      if (n == 0 || (n == 5 && !memcmp(ln, "INT. ", 5))) {
         len = s; text[len] = 0; cur_force = F_CHAR; dirty = 1; words_valid = 0;
      }
      break;
   case F_CHAR:
      if (n == 0) { cur_force = F_TRANS; put('>'); }
      break;
   case F_TRANS:
      if (n == 0 || (n == 1 && ln[0] == '>')) {
         len = s; text[len] = 0; cur_force = F_NONE; dirty = 1; words_valid = 0;
      }
      break;
   }
}

/* --------------------------------------------------------------- save */
static void make_path(void)
{
   const char *dir = save_dir[0] ? save_dir : ".";
   char name[64];
   time_t t = time(NULL);
   struct tm *tmv = localtime(&t);
   if (tmv) strftime(name, sizeof name, "script_%Y%m%d_%H%M%S", tmv);
   else snprintf(name, sizeof name, "script");
   for (int n = 1; n < 1000; n++) {
      if (n == 1) snprintf(save_path, sizeof save_path, "%s/%s.fountain", dir, name);
      else snprintf(save_path, sizeof save_path, "%s/%s_%d.fountain", dir, name, n);
      FILE *f = fopen(save_path, "rb");
      if (!f) return;
      fclose(f);
   }
}

static void save_now(void)
{
   if (len == 0) return;
   if (!save_path[0]) make_path();
   FILE *f = fopen(save_path, "wb");
   if (!f) { save_err = 1; dirty = 1; return; }
   fwrite(text, 1, (size_t)len, f);
   if (text[len - 1] != '\n') fputc('\n', f);
   fflush(f);
   fsync(fileno(f));
   fclose(f);
   changed = 0; save_err = 0; dirty = 1;
   last_save_frame = frame;
}

/* -------------------------------------------------------------- input */
static void cycle_size(void) { sc = (sc == 3) ? 2 : 3; dirty = 1; }

static void apply_key(unsigned k, uint32_t ch, uint16_t mod)
{
   switch (k) {
   case RETROK_BACKSPACE: do_backspace(); break;
   case RETROK_RETURN:
   case RETROK_KP_ENTER: do_enter(mod & RETROKMOD_SHIFT); break;
   case RETROK_TAB: do_tab(); break;
   default:
      if (ch == 0 && k >= 32 && k < 127 && !(mod & (RETROKMOD_CTRL | RETROKMOD_ALT))) {
         ch = k;
         if ((mod & RETROKMOD_SHIFT) && k >= 'a' && k <= 'z') ch = (uint32_t)toupper((int)k);
      }
      if (ch >= 32 && ch < 127 && !(mod & RETROKMOD_CTRL)) type_char((char)ch);
      break;
   }
}

static void kb_cb(bool down, unsigned k, uint32_t ch, uint16_t mod)
{
   if (!down) {
      if (k == held_key) held_key = 0;
      return;
   }
   int rep = (k == held_key);
   if (rep) frontend_repeats = 1;

   if (show_help) { if (!rep) { show_help = 0; dirty = 1; } return; }

   switch (k) {
   case RETROK_F1: if (!rep) { show_help = 1; dirty = 1; } return;
   case RETROK_F2: if (!rep) cycle_size(); return;
   case RETROK_F3: if (!rep) { show_hud = !show_hud; dirty = 1; } return;
   case RETROK_F4: if (!rep) save_now(); return;
   default: break;
   }

   held_key = k; held_char = ch; held_mod = mod;
   if (!rep) held_frames = 0;
   apply_key(k, ch, mod);
}

/* ------------------------------------------------------------- render */
static int nv;
static int vp[MAXV], vlen[MAXV], vx[MAXV];
static unsigned char vtype[MAXV];

static void addv(int p, int n, int x, int t)
{
   if (nv >= MAXV) return;
   vp[nv] = p; vlen[nv] = n; vx[nv] = x; vtype[nv] = (unsigned char)t; nv++;
}

static void wrap_line(const char *s, int base, int n, int x, int w, int t, int cols)
{
   int i = 0;
   if (w < 6) w = 6;
   do {
      int j = i, last = -1, cnt = 0, end, next;
      while (j < n && cnt < w) { if (s[j] == ' ') last = j; j++; cnt++; }
      if (j >= n) { end = n; next = n + 1; }
      else if (s[j] == ' ') { end = j; next = j + 1; }
      else if (last > i) { end = last; next = last + 1; }
      else { end = j; next = j; }
      int xx = x;
      if (t == T_TRANS) { xx = cols - (end - i); if (xx < 0) xx = 0; }
      addv(base + i, end - i, xx, t);
      i = next;
   } while (i <= n);
}

static int count_words(void)
{
   if (words_valid) return words_cache;
   int w = 0, in = 0;
   for (int i = 0; i < len; i++) {
      if (isspace((unsigned char)text[i])) in = 0;
      else if (!in) { in = 1; w++; }
   }
   words_cache = w; words_valid = 1;
   return w;
}

static const uint16_t type_color[] = {
   C_TEXT, C_SCENE, C_CHAR, C_PAREN, C_TEXT, C_TRANS, C_TEXT
};

static void render(void)
{
   const int cw = 6 * sc, lh = 8 * sc;
   const int cols = (W - 2 * MX) / cw;
   const int hud_h = show_hud ? HUDH : 0;
   const int rows = (H - 2 * MY - hud_h) / lh;

   fill_rect(0, 0, W, H, C_BG);
   parse();

   nv = 0;
   for (int i = 0; i < nl && nv < MAXV - 64; i++) {
      int t = lt[i], n = ll[i], base = ls[i];
      const char *s = text + base;
      if (t == T_BLANK) { addv(base, 0, 0, T_BLANK); continue; }
      if ((t == T_SCENE && n >= 2 && s[0] == '.' && s[1] != '.') ||
          (t == T_CHAR && n > 0 && s[0] == '@') ||
          (t == T_TRANS && n > 0 && s[0] == '>')) {
         s++; base++; n--;
      }
      int x, w;
      switch (t) {
      case T_CHAR:  x = cols * 11 / 32; w = cols - x; break;
      case T_PAREN: x = cols / 4;       w = cols / 2; break;
      case T_DIAL:  x = cols * 3 / 20;  w = cols * 7 / 10; break;
      default:      x = 0;              w = cols; break;
      }
      wrap_line(s, base, n, x, w, t, cols);
   }

   int first = nv > rows ? nv - rows : 0;
   for (int r = 0; r < rows && first + r < nv; r++) {
      int v = first + r;
      if (vtype[v] == T_BLANK || vlen[v] == 0) continue;
      draw_text(MX + vx[v] * cw, MY + r * lh, text + vp[v], vlen[v], sc,
            type_color[vtype[v]], vtype[v] == T_SCENE);
   }

   /* cursor */
   if (!show_help && ((frame / 30) & 1) == 0) {
      int v = nv - 1;
      int row = v - first;
      fill_rect(MX + (vx[v] + vlen[v]) * cw, MY + row * lh, 2 * sc / 2 + 1, 7 * sc,
            C_TEXT);
   }

   /* HUD */
   if (show_hud) {
      int cur = (cur_force == F_NONE) ? lt[nl - 1] : forced_type();
      char buf[96];
      int words = count_words();
      unsigned secs = (unsigned)(session_frames / 60);
      snprintf(buf, sizeof buf, "%s  %dw ~p%d  %02u:%02u  %s",
            type_names[cur], words, words / 180 + (words > 0), secs / 60, secs % 60,
            save_err ? "SAVE ERROR" : (changed ? "unsaved" : (len ? "saved" : "")));
      fill_rect(0, H - HUDH, W, 1, C_RULE);
      draw_text(MX, H - HUDH + 7, buf, (int)strlen(buf), 2, C_DIM, 0);
      const char *hint = "F1 help";
      draw_text(W - MX - 7 * 12, H - HUDH + 7, hint, 7, 2, C_DIM, 0);
   }

   /* help overlay */
   if (show_help) {
      static const char *lines[] = {
         "SCRIPTWRITER",
         "",
         "TAB       cycle element on empty line",
         "          scene > char > trans > action",
         "ENTER     next element, auto-format",
         "SH+ENTER  new line in same element",
         "F2        text size",
         "F3        show/hide status bar",
         "F4        save now (also START)",
#ifdef STANDALONE_APP
         "F10       save and quit",
#else
         "R         text size (gamepad)",
#endif
         "",
         "Autosaves .fountain to the save folder",
         "",
         "Any key to close"
      };
      fill_rect(40, 30, W - 80, 14 * 28 + 24, RGB(8, 8, 12));
      fill_rect(40, 30, W - 80, 2, C_DIM);
      fill_rect(40, 30 + 14 * 28 + 22, W - 80, 2, C_DIM);
      for (int i = 0; i < 14; i++)
         draw_text(60, 44 + i * 28, lines[i], (int)strlen(lines[i]), 2,
               i == 0 ? C_SCENE : C_TEXT, i == 0);
      if (save_path[0]) {
         const char *p = strrchr(save_path, '/');
         p = p ? p + 1 : save_path;
         draw_text(60, 44 + 12 * 28, p, (int)strlen(p), 2, C_DIM, 0);
      }
   }
}

/* ------------------------------------------------------------ libretro */
void retro_set_environment(retro_environment_t cb)
{
   env_cb = cb;
   bool no_game = true;
   cb(RETRO_ENVIRONMENT_SET_SUPPORT_NO_GAME, &no_game);
}
void retro_set_video_refresh(retro_video_refresh_t cb) { video_cb = cb; }
void retro_set_audio_sample(retro_audio_sample_t cb) { audio_cb = cb; }
void retro_set_audio_sample_batch(retro_audio_sample_batch_t cb) { audio_batch_cb = cb; }
void retro_set_input_poll(retro_input_poll_t cb) { input_poll_cb = cb; }
void retro_set_input_state(retro_input_state_t cb) { input_state_cb = cb; }

void retro_init(void) { }
void retro_deinit(void) { save_now(); }
unsigned retro_api_version(void) { return RETRO_API_VERSION; }

void retro_get_system_info(struct retro_system_info *info)
{
   memset(info, 0, sizeof *info);
   info->library_name = "Scriptwriter";
   info->library_version = "0.1";
   info->valid_extensions = "fw|fountain|txt";
   info->need_fullpath = false;
   info->block_extract = false;
}

void retro_get_system_av_info(struct retro_system_av_info *info)
{
   info->geometry.base_width = W;
   info->geometry.base_height = H;
   info->geometry.max_width = W;
   info->geometry.max_height = H;
   info->geometry.aspect_ratio = 4.0f / 3.0f;
   info->timing.fps = 60.0;
   info->timing.sample_rate = 44100.0;
}

void retro_set_controller_port_device(unsigned port, unsigned device) { (void)port; (void)device; }
void retro_reset(void) { }

bool retro_load_game(const struct retro_game_info *game)
{
   (void)game; /* the launch file is only a placeholder; always start fresh */
   enum retro_pixel_format fmt = RETRO_PIXEL_FORMAT_RGB565;
   if (!env_cb(RETRO_ENVIRONMENT_SET_PIXEL_FORMAT, &fmt)) return false;

   struct retro_keyboard_callback kb = { kb_cb };
   env_cb(RETRO_ENVIRONMENT_SET_KEYBOARD_CALLBACK, &kb);

   const char *dir = NULL;
   if (env_cb(RETRO_ENVIRONMENT_GET_SAVE_DIRECTORY, &dir) && dir && dir[0]) {
      strncpy(save_dir, dir, sizeof save_dir - 1);
   } else if (env_cb(RETRO_ENVIRONMENT_GET_SYSTEM_DIRECTORY, &dir) && dir && dir[0]) {
      strncpy(save_dir, dir, sizeof save_dir - 1);
   }

   len = 0; text[0] = 0; cur_force = F_NONE; changed = 0; started = 0;
   session_frames = 0; frame = 0; save_path[0] = 0; dirty = 1; words_valid = 0;
   return true;
}

bool retro_load_game_special(unsigned t, const struct retro_game_info *i, size_t n)
{ (void)t; (void)i; (void)n; return false; }

void retro_unload_game(void) { save_now(); }
unsigned retro_get_region(void) { return RETRO_REGION_NTSC; }
size_t retro_serialize_size(void) { return 0; }
bool retro_serialize(void *d, size_t s) { (void)d; (void)s; return false; }
bool retro_unserialize(const void *d, size_t s) { (void)d; (void)s; return false; }
void retro_cheat_reset(void) { }
void retro_cheat_set(unsigned i, bool e, const char *c) { (void)i; (void)e; (void)c; }
void *retro_get_memory_data(unsigned id) { (void)id; return NULL; }
size_t retro_get_memory_size(unsigned id) { (void)id; return 0; }

static unsigned long render_count;

void retro_run(void)
{
   input_poll_cb();

   int start = input_state_cb(0, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_START);
   int r = input_state_cb(0, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_R);
   if (start && !prev_start) save_now();
   if (r && !prev_r) cycle_size();
   prev_start = start; prev_r = r;

   frame++;
   if (started) session_frames++;

   /* software key repeat, only if the frontend doesn't repeat for us */
   if (held_key && !frontend_repeats && !show_help && held_key != RETROK_TAB) {
      held_frames++;
      if (held_frames >= 30 && (held_frames - 30) % 3 == 0)
         apply_key(held_key, held_char, held_mod);
   }

   if (changed && frame - last_save_frame >= 180) save_now();

   int blink = (int)((frame / 30) & 1);
   if (blink != last_blink) { last_blink = blink; dirty = 1; }
   if (started && frame % 60 == 0) dirty = 1;

   if (dirty) { render(); dirty = 0; render_count++; }

   video_cb(fb, W, H, W * sizeof(uint16_t));
   audio_batch_cb(silence, 735);
}
