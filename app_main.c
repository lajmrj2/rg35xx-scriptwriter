/*
 * Standalone Linux front-end for the Scriptwriter editor.
 *
 * No libraries needed: draws straight to /dev/fb0 and reads USB keyboards from
 * /dev/input/event*. It reuses the editor from freewrite_libretro.c by
 * driving it with the same callbacks a libretro frontend would provide.
 * Build it static so it doesn't depend on whatever is on the device.
 *
 * Usage: ./scriptwriter [save_dir]     (default: <folder of binary>/scripts)
 * Keys:  F10 or Ctrl+Q saves and quits. See F1 inside the app for the rest.
 */
#define _GNU_SOURCE
#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include <dirent.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <linux/fb.h>
#include <linux/input.h>

#define STANDALONE_APP 1
#include "freewrite_libretro.c"

static FILE *logfp;
#define LOG(...) do { if (logfp) { fprintf(logfp, __VA_ARGS__); fputc('\n', logfp); fflush(logfp); } } while (0)

/* ---------------------------------------------------------- framebuffer */
static volatile sig_atomic_t quit_flag;
static void on_signal(int s) { (void)s; quit_flag = 1; }

static int fbfd = -1;
static unsigned char *devfb;
static size_t fbsize;
static struct fb_var_screeninfo vinfo;
static struct fb_fix_screeninfo finfo;
static int xmap[4096];
static uint32_t lut[65536];

static uint32_t pack_pixel(unsigned r, unsigned g, unsigned b)
{
   if (vinfo.bits_per_pixel == 32) {
      uint32_t v = ((uint32_t)r << vinfo.red.offset) | ((uint32_t)g << vinfo.green.offset) |
                   ((uint32_t)b << vinfo.blue.offset);
      if (vinfo.transp.length == 8) v |= 0xFFu << vinfo.transp.offset;
      return v;
   }
   return ((r >> (8 - vinfo.red.length)) << vinfo.red.offset) |
          ((g >> (8 - vinfo.green.length)) << vinfo.green.offset) |
          ((b >> (8 - vinfo.blue.length)) << vinfo.blue.offset);
}

static int fb_open(void)
{
   fbfd = open("/dev/fb0", O_RDWR);
   if (fbfd < 0) { perror("open /dev/fb0"); return -1; }
   if (ioctl(fbfd, FBIOGET_VSCREENINFO, &vinfo) || ioctl(fbfd, FBIOGET_FSCREENINFO, &finfo)) {
      perror("framebuffer ioctl"); return -1;
   }
   if (vinfo.bits_per_pixel != 16 && vinfo.bits_per_pixel != 32) {
      fprintf(stderr, "unsupported framebuffer depth: %u bpp\n", vinfo.bits_per_pixel);
      return -1;
   }
   if (vinfo.xres == 0 || vinfo.xres > 4096 || vinfo.yres == 0) {
      fprintf(stderr, "odd framebuffer size %ux%u\n", vinfo.xres, vinfo.yres);
      return -1;
   }
   fbsize = finfo.smem_len;
   devfb = mmap(NULL, fbsize, PROT_READ | PROT_WRITE, MAP_SHARED, fbfd, 0);
   if (devfb == MAP_FAILED) { perror("mmap framebuffer"); return -1; }
   for (unsigned x = 0; x < vinfo.xres; x++) xmap[x] = (int)(x * W / vinfo.xres);
   for (unsigned p = 0; p < 65536; p++) {
      unsigned r = (p >> 11) & 31, g = (p >> 5) & 63, b = p & 31;
      lut[p] = pack_pixel((r << 3) | (r >> 2), (g << 2) | (g >> 4), (b << 3) | (b >> 2));
   }
   fprintf(stderr, "framebuffer: %ux%u, %u bpp\n", vinfo.xres, vinfo.yres, vinfo.bits_per_pixel);
   return 0;
}

static void blit(void)
{
   unsigned yoff = vinfo.yoffset;
   if ((size_t)(yoff + vinfo.yres) * finfo.line_length > fbsize) yoff = 0;
   unsigned char *base = devfb + (size_t)yoff * finfo.line_length;
   for (unsigned y = 0; y < vinfo.yres; y++) {
      const uint16_t *src = fb + (size_t)(y * H / vinfo.yres) * W;
      unsigned char *dst = base + (size_t)y * finfo.line_length;
      if (vinfo.bits_per_pixel == 32) {
         uint32_t *d = (uint32_t *)dst;
         for (unsigned x = 0; x < vinfo.xres; x++) d[x] = lut[src[xmap[x]]];
      } else {
         uint16_t *d = (uint16_t *)dst;
         for (unsigned x = 0; x < vinfo.xres; x++) d[x] = (uint16_t)lut[src[xmap[x]]];
      }
   }
}

/* ---------------------------------------------------------------- keys */
static int key_to_char(int code, int shift, int caps)
{
   const char *lo, *hi;
   int idx;
   if (code >= 2 && code <= 13) { lo = "1234567890-="; hi = "!@#$%^&*()_+"; idx = code - 2; }
   else if (code >= 16 && code <= 27) { lo = "qwertyuiop[]"; hi = "QWERTYUIOP{}"; idx = code - 16; }
   else if (code >= 30 && code <= 41) { lo = "asdfghjkl;'`"; hi = "ASDFGHJKL:\"~"; idx = code - 30; }
   else if (code == 43) return shift ? '|' : '\\';
   else if (code >= 44 && code <= 53) { lo = "zxcvbnm,./"; hi = "ZXCVBNM<>?"; idx = code - 44; }
   else if (code == 57) return ' ';
   else return 0;
   int s = shift;
   if (isalpha((unsigned char)lo[idx])) s = shift ^ caps;
   return s ? hi[idx] : lo[idx];
}

static int shift_down, ctrl_down, alt_down, caps_on;

static void handle_key(int code, int val)
{
   switch (code) {
   case KEY_LEFTSHIFT: case KEY_RIGHTSHIFT: shift_down = (val != 0); return;
   case KEY_LEFTCTRL: case KEY_RIGHTCTRL: ctrl_down = (val != 0); return;
   case KEY_LEFTALT: case KEY_RIGHTALT: alt_down = (val != 0); return;
   case KEY_CAPSLOCK: if (val == 1) caps_on = !caps_on; return;
   default: break;
   }
   if (val == 1 && (code == KEY_F10 || (ctrl_down && code == KEY_Q))) { quit_flag = 1; return; }

   unsigned rk = 0;
   uint32_t ch = 0;
   switch (code) {
   case KEY_BACKSPACE: rk = RETROK_BACKSPACE; break;
   case KEY_ENTER: case KEY_KPENTER: rk = RETROK_RETURN; break;
   case KEY_TAB: rk = RETROK_TAB; break;
   case KEY_ESC: rk = RETROK_ESCAPE; break;
   case KEY_F1: rk = RETROK_F1; break;
   case KEY_F2: rk = RETROK_F2; break;
   case KEY_F3: rk = RETROK_F3; break;
   case KEY_F4: rk = RETROK_F4; break;
   default:
      ch = (uint32_t)key_to_char(code, shift_down, caps_on);
      rk = (unsigned)key_to_char(code, 0, 0);
      break;
   }
   if (!rk) return;
   uint16_t mod = (uint16_t)((shift_down ? RETROKMOD_SHIFT : 0) | (ctrl_down ? RETROKMOD_CTRL : 0) |
                             (alt_down ? RETROKMOD_ALT : 0));
   kb_cb(val != 0, rk, ch, mod);   /* val 2 (autorepeat) arrives as another "down" */
}

#define MAXDEV 32
static char dev_path[MAXDEV][300];
static char dev_name[MAXDEV][64];
static int dev_fd[MAXDEV];      /* >=0 open for events, -1 unusable */
static int dev_kb[MAXDEV];      /* 1 = looks like a keyboard */
static int ndev, nkeyboards;
static int log_events;
static char last_ev[160];
static int nonkb_down;          /* buttons currently held on non-keyboard devices */
static struct timespec nonkb_since;

static int has_key(const unsigned long *bits, int key)
{
   return (bits[key / (8 * sizeof(long))] >> (key % (8 * sizeof(long)))) & 1;
}

static void recount_keyboards(void)
{
   nkeyboards = 0;
   for (int i = 0; i < ndev; i++) if (dev_fd[i] >= 0 && dev_kb[i]) nkeyboards++;
}

static void scan_inputs(void)
{
   DIR *d = opendir("/dev/input");
   if (!d) return;
   struct dirent *e;
   while ((e = readdir(d))) {
      if (strncmp(e->d_name, "event", 5)) continue;
      char path[300];
      snprintf(path, sizeof path, "/dev/input/%s", e->d_name);
      int known = 0;
      for (int i = 0; i < ndev; i++) if (!strcmp(dev_path[i], path)) { known = 1; break; }
      if (known || ndev >= MAXDEV) continue;
      strcpy(dev_path[ndev], path);
      dev_fd[ndev] = -1; dev_kb[ndev] = 0;
      snprintf(dev_name[ndev], sizeof dev_name[ndev], "(cannot open)");
      int fd = open(path, O_RDONLY | O_NONBLOCK);
      if (fd >= 0) {
         char name[64] = "?";
         if (ioctl(fd, EVIOCGNAME(sizeof name), name) < 0) strcpy(name, "?");
         snprintf(dev_name[ndev], sizeof dev_name[ndev], "%s", name);
         unsigned long evbits[EV_MAX / (8 * sizeof(long)) + 1];
         unsigned long keybits[KEY_MAX / (8 * sizeof(long)) + 1];
         memset(evbits, 0, sizeof evbits);
         memset(keybits, 0, sizeof keybits);
         ioctl(fd, EVIOCGBIT(0, sizeof evbits), evbits);
         ioctl(fd, EVIOCGBIT(EV_KEY, sizeof keybits), keybits);
         int haskeys = has_key(evbits, EV_KEY);
         int kb = has_key(keybits, KEY_A) && has_key(keybits, KEY_ENTER) && has_key(keybits, KEY_SPACE);
         if (haskeys) { dev_fd[ndev] = fd; dev_kb[ndev] = kb; } else close(fd);
         LOG("device %s name='%s' has_keys=%d keyboard=%d", path, name, haskeys, kb);
      } else {
         LOG("device %s open failed: %s", path, strerror(errno));
      }
      ndev++;
   }
   closedir(d);
   recount_keyboards();
}

static void poll_inputs(void)
{
   for (int i = 0; i < ndev; i++) {
      if (dev_fd[i] < 0) continue;
      struct input_event ev;
      for (;;) {
         ssize_t n = read(dev_fd[i], &ev, sizeof ev);
         if (n == (ssize_t)sizeof ev) {
            if (ev.type != EV_KEY) continue;
            if (ev.value != 2) {
               snprintf(last_ev, sizeof last_ev, "%.40s: code %d value %d", dev_name[i], ev.code, ev.value);
               if (log_events < 400) { log_events++; LOG("event %s code=%d value=%d", dev_name[i], ev.code, ev.value); }
            }
            if (dev_kb[i]) {
               handle_key(ev.code, ev.value);
            } else if (ev.value == 1) {
               if (nonkb_down++ == 0) clock_gettime(CLOCK_MONOTONIC, &nonkb_since);
            } else if (ev.value == 0 && nonkb_down > 0) {
               nonkb_down--;
            }
         } else {
            if (n < 0 && errno != EAGAIN && errno != EINTR) {   /* unplugged */
               LOG("device removed: %s", dev_path[i]);
               if (!dev_kb[i]) nonkb_down = 0;
               close(dev_fd[i]);
               dev_fd[i] = -1;
               dev_path[i][0] = 0;   /* allow rediscovery */
            }
            break;
         }
      }
   }
   int j = 0;
   for (int i = 0; i < ndev; i++)
      if (dev_path[i][0]) {
         if (i != j) {
            strcpy(dev_path[j], dev_path[i]);
            strcpy(dev_name[j], dev_name[i]);
            dev_fd[j] = dev_fd[i];
            dev_kb[j] = dev_kb[i];
         }
         j++;
      }
   ndev = j;
   recount_keyboards();
}

/* Shown whenever no keyboard is detected, so you can see what the device sees. */
static void read_line_file(const char *path, char *out, size_t n)
{
   out[0] = 0;
   FILE *f = fopen(path, "r");
   if (!f) return;
   if (fgets(out, (int)n, f)) out[strcspn(out, "\r\n")] = 0;
   fclose(f);
}

static void diag_line(int *y, const char *s, uint16_t col)
{
   int n = (int)strlen(s);
   if (n > 52) n = 52;
   draw_text(12, *y, s, n, 2, col, 0);
   *y += 18;
}

static void draw_diag(void)
{
   char buf[300];
   int y = 10;
   fill_rect(0, 0, W, H, C_BG);
   draw_text(12, y, "SCRIPTWRITER - NO KEYBOARD FOUND", 32, 2, C_SCENE, 1);
   y += 26;
   diag_line(&y, "Plug in the keyboard dongle; it starts by itself.", C_TEXT);
   diag_line(&y, "To quit: hold any device button for 2 seconds.", C_TEXT);
   y += 8;
   diag_line(&y, "Input devices (K = keyboard, x = not usable):", C_CHAR);
   if (ndev == 0) diag_line(&y, "  none found in /dev/input", C_PAREN);
   for (int i = 0; i < ndev && i < 8; i++) {
      snprintf(buf, sizeof buf, "  [%c] %s", dev_fd[i] < 0 ? 'x' : (dev_kb[i] ? 'K' : '-'),
            dev_name[i][0] ? dev_name[i] : dev_path[i]);
      diag_line(&y, buf, C_TEXT);
   }
   y += 8;
   diag_line(&y, "USB devices:", C_CHAR);
   int shown = 0;
   DIR *d = opendir("/sys/bus/usb/devices");
   if (d) {
      struct dirent *e;
      while ((e = readdir(d)) && shown < 5) {
         if (e->d_name[0] == '.') continue;
         char p[400], prod[96], man[96];
         snprintf(p, sizeof p, "/sys/bus/usb/devices/%.200s/product", e->d_name);
         read_line_file(p, prod, sizeof prod);
         snprintf(p, sizeof p, "/sys/bus/usb/devices/%.200s/manufacturer", e->d_name);
         read_line_file(p, man, sizeof man);
         if (!prod[0] && !man[0]) continue;
         snprintf(buf, sizeof buf, "  %.20s %.30s", man, prod);
         diag_line(&y, buf, C_TEXT);
         shown++;
      }
      closedir(d);
   }
   if (!shown) diag_line(&y, d ? "  nothing detected on USB" : "  /sys/bus/usb not available", C_PAREN);
   y += 8;
   diag_line(&y, "Last button event:", C_CHAR);
   snprintf(buf, sizeof buf, "  %s", last_ev[0] ? last_ev : "(none yet)");
   diag_line(&y, buf, C_TEXT);
   diag_line(&y, "Details are saved in scriptwriter.log", C_DIM);
}

/* ------------------------------------------------- libretro glue / main */
static char app_save_dir[600];

static bool app_env(unsigned cmd, void *data)
{
   switch (cmd) {
   case RETRO_ENVIRONMENT_SET_PIXEL_FORMAT: return true;
   case RETRO_ENVIRONMENT_GET_SAVE_DIRECTORY: *(const char **)data = app_save_dir; return true;
   case RETRO_ENVIRONMENT_SET_KEYBOARD_CALLBACK: return true;
   case RETRO_ENVIRONMENT_SET_SUPPORT_NO_GAME: return true;
   }
   return false;
}
static void app_video(const void *d, unsigned w, unsigned h, size_t p) { (void)d; (void)w; (void)h; (void)p; }
static void app_poll(void) { }
static int16_t app_input(unsigned a, unsigned b, unsigned c, unsigned d) { (void)a; (void)b; (void)c; (void)d; return 0; }
static void app_audio(int16_t l, int16_t r) { (void)l; (void)r; }
static size_t app_audio_batch(const int16_t *d, size_t n) { (void)d; return n; }

static char app_dir[512];

int main(int argc, char **argv)
{
   char exe[512];
   ssize_t rn = readlink("/proc/self/exe", exe, sizeof exe - 1);
   snprintf(app_dir, sizeof app_dir, ".");
   if (rn > 0) {
      exe[rn] = 0;
      char *slash = strrchr(exe, '/');
      if (slash) { *slash = 0; snprintf(app_dir, sizeof app_dir, "%s", exe); }
   }
   if (argc > 1) snprintf(app_save_dir, sizeof app_save_dir, "%s", argv[1]);
   else snprintf(app_save_dir, sizeof app_save_dir, "%s/scripts", app_dir);
   mkdir(app_save_dir, 0755);

   char logpath[600];
   snprintf(logpath, sizeof logpath, "%s/scriptwriter.log", app_dir);
   logfp = fopen(logpath, "w");
   LOG("scriptwriter start; saving to %s", app_save_dir);

   signal(SIGINT, on_signal);
   signal(SIGTERM, on_signal);
   signal(SIGHUP, on_signal);

   if (fb_open() != 0) { LOG("framebuffer open failed"); return 1; }
   LOG("framebuffer %ux%u %ubpp", vinfo.xres, vinfo.yres, vinfo.bits_per_pixel);

   retro_set_environment(app_env);
   retro_set_video_refresh(app_video);
   retro_set_audio_sample(app_audio);
   retro_set_audio_sample_batch(app_audio_batch);
   retro_set_input_poll(app_poll);
   retro_set_input_state(app_input);
   retro_init();
   struct retro_game_info gi = { "Script.fw", NULL, 0, NULL };
   if (!retro_load_game(&gi)) { LOG("load failed"); return 1; }

   struct timespec next;
   clock_gettime(CLOCK_MONOTONIC, &next);
   unsigned long last_render = (unsigned long)-1, ticks = 0;
   int in_diag = 1;
   while (!quit_flag) {
      if (ticks % 120 == 0) scan_inputs();
      poll_inputs();

      if (nonkb_down > 0) {   /* failsafe: hold any device button 2s to quit */
         struct timespec now;
         clock_gettime(CLOCK_MONOTONIC, &now);
         if (now.tv_sec - nonkb_since.tv_sec >= 2) { LOG("quit: button held"); break; }
      }

      if (nkeyboards == 0) {
         if (!in_diag) { in_diag = 1; save_now(); LOG("keyboard gone"); }
         if (ticks % 15 == 0) { draw_diag(); blit(); }
      } else {
         if (in_diag) { in_diag = 0; dirty = 1; last_render = (unsigned long)-1; LOG("keyboard ready"); }
         retro_run();
         if (render_count != last_render) { blit(); last_render = render_count; }
      }
      ticks++;

      next.tv_nsec += 16666667;
      if (next.tv_nsec >= 1000000000L) { next.tv_nsec -= 1000000000L; next.tv_sec++; }
      clock_nanosleep(CLOCK_MONOTONIC, TIMER_ABSTIME, &next, NULL);
   }
   save_now();
   retro_unload_game();
   retro_deinit();
   LOG("clean exit");
   return 0;
}
