/* Desktop test harness: loads the core as a shared library, types a short
 * script through the keyboard callback, and dumps screenshots + the saved file. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <dlfcn.h>
#include <sys/stat.h>
#include "libretro.h"

static retro_keyboard_event_t kb;
static uint16_t frame_copy[640 * 480];

static void (*p_run)(void);

static bool env(unsigned cmd, void *data)
{
   switch (cmd) {
   case RETRO_ENVIRONMENT_SET_PIXEL_FORMAT: return true;
   case RETRO_ENVIRONMENT_GET_SAVE_DIRECTORY: *(const char **)data = "./test_saves"; return true;
   case RETRO_ENVIRONMENT_SET_KEYBOARD_CALLBACK: kb = ((struct retro_keyboard_callback *)data)->callback; return true;
   case RETRO_ENVIRONMENT_SET_SUPPORT_NO_GAME: return true;
   }
   return false;
}
static void video(const void *d, unsigned w, unsigned h, size_t pitch)
{ (void)w; (void)h; (void)pitch; if (d) memcpy(frame_copy, d, sizeof frame_copy); }
static void poll(void) { }
static int16_t input(unsigned a, unsigned b, unsigned c, unsigned d) { (void)a; (void)b; (void)c; (void)d; return 0; }
static void audio(int16_t l, int16_t r) { (void)l; (void)r; }
static size_t audio_batch(const int16_t *d, size_t n) { (void)d; return n; }

static void key(unsigned k, uint32_t ch, uint16_t mod)
{ kb(true, k, ch, mod); kb(false, k, ch, mod); p_run(); }
static void typestr(const char *s)
{ for (; *s; s++) key((unsigned)tolower((unsigned char)*s), (unsigned char)*s, 0); }
static void enter(void) { key(RETROK_RETURN, 13, 0); }
static void tab(void) { key(RETROK_TAB, 9, 0); }

static void dump(const char *name)
{
   FILE *f = fopen(name, "wb");
   fprintf(f, "P6\n640 480\n255\n");
   for (int i = 0; i < 640 * 480; i++) {
      uint16_t p = frame_copy[i];
      unsigned char rgb[3] = { (unsigned char)((p >> 8) & 0xF8), (unsigned char)((p >> 3) & 0xFC), (unsigned char)((p << 3) & 0xF8) };
      fwrite(rgb, 1, 3, f);
   }
   fclose(f);
}

int main(int argc, char **argv)
{
   const char *so = argc > 1 ? argv[1] : "./freewrite_libretro.so";
   void *h = dlopen(so, RTLD_NOW);
   if (!h) { fprintf(stderr, "dlopen: %s\n", dlerror()); return 1; }
   void (*set_env)(retro_environment_t) = dlsym(h, "retro_set_environment");
   void (*set_video)(retro_video_refresh_t) = dlsym(h, "retro_set_video_refresh");
   void (*set_audio)(retro_audio_sample_t) = dlsym(h, "retro_set_audio_sample");
   void (*set_batch)(retro_audio_sample_batch_t) = dlsym(h, "retro_set_audio_sample_batch");
   void (*set_poll)(retro_input_poll_t) = dlsym(h, "retro_set_input_poll");
   void (*set_input)(retro_input_state_t) = dlsym(h, "retro_set_input_state");
   void (*init)(void) = dlsym(h, "retro_init");
   bool (*load)(const struct retro_game_info *) = dlsym(h, "retro_load_game");
   void (*unload)(void) = dlsym(h, "retro_unload_game");
   p_run = dlsym(h, "retro_run");

   mkdir("test_saves", 0755);
   set_env(env); set_video(video); set_audio(audio); set_batch(audio_batch);
   set_poll(poll); set_input(input);
   init();
   struct retro_game_info gi = { "Script.fw", "fountain\n", 9, NULL };
   if (!load(&gi)) { fprintf(stderr, "load failed\n"); return 1; }
   if (!kb) { fprintf(stderr, "no keyboard callback registered\n"); return 1; }

   tab(); typestr("kitchen - night"); enter();
   typestr("Rain hammers the window. MARY (30s) stirs a pot, lost in thought."); enter();
   tab(); tab(); typestr("mary"); enter();
   typestr("(quietly)"); enter();
   typestr("I told you not to come back here."); enter();
   typestr("jack"); enter();
   typestr("You never gave me a choice."); enter();
   tab(); typestr("cut to:"); enter();
   typestr("ext. street - day"); enter();
   typestr("Jack walks away. Mary watches from the doorway until he is gone."); enter();
   typestr("oops"); for (int i = 0; i < 4; i++) key(RETROK_BACKSPACE, 8, 0);
   for (int i = 0; i < 100; i++) p_run();
   dump("shot_script.ppm");

   key(RETROK_F1, 0, 0);
   for (int i = 0; i < 10; i++) p_run();
   dump("shot_help.ppm");
   key('a', 'a', 0);   /* closes help */
   key(RETROK_F4, 0, 0);

   unload();
   return 0;
}
