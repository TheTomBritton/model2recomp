/*
 * SDL2 platform layer for Model 2 recompilation.
 *
 * Provides windowing, rendering, audio output, input polling,
 * and frame synchronization.
 */

#include "model2recomp/platform.h"

#ifdef _MSC_VER
#pragma comment(lib, "SDL2.lib")
#pragma comment(lib, "SDL2main.lib")
#endif

#include <SDL.h>
#include <stdio.h>
#include <stdlib.h>

#ifdef __EMSCRIPTEN__
#include <emscripten.h>
/*
 * The browser build. The recompiled game owns the frame loop and never
 * returns, so the page cannot be driven from a host loop: the build uses JSPI
 * and the field boundary suspends the whole wasm stack here, which is when
 * the browser paints the canvas and delivers input. One field per animation
 * frame on a 60 Hz display (the board's 57.5 Hz, near enough); a faster
 * display is paced to the board's period.
 */
EM_ASYNC_JS(void, platform_web_wait_field, (double period_ms), {
  const raf = () => new Promise(r => requestAnimationFrame(r));
  const st = Module.__m2field || (Module.__m2field = { next: 0, raf: 16.7, last: 0 });
  const t0 = performance.now();
  if (!st.next || t0 - st.next > 250) st.next = t0;   /* first field, or far behind: re-anchor, no catch-up */
  st.next += period_ms;
  await raf();
  const t1 = performance.now();
  if (st.last) st.raf = st.raf * 0.9 + (t1 - st.last) * 0.1;   /* the display's refresh period, smoothed */
  st.last = t1;
  if (st.raf < period_ms * 0.75) {                     /* > ~75 Hz display: wait out the rest of the period */
    while (performance.now() < st.next - st.raf * 0.5) await raf();
  } else if (t1 < st.next - period_ms) {               /* 60 Hz display runs the board at 60: never let the */
    st.next = t1;                                      /* schedule drift ahead into a double-length frame */
  }
});
#endif

static SDL_Window   *s_window = NULL;
static SDL_Renderer *s_renderer = NULL;
static SDL_Texture  *s_texture = NULL;
static SDL_AudioDeviceID s_audio_dev = 0;

static int s_width = 0;
static int s_height = 0;

static uint64_t s_frame_start = 0;
static const double FRAME_TIME_MS = 1000.0 / 57.5; /* Model 2 runs at ~57.5 Hz */

/*
 * A light gun's border: a white frame round the picture for a camera gun
 * (Sinden) to find the screen by, with a black gap inside it so a bright
 * scene never merges with the band - the same construction the Time Crisis
 * engines draw. Both are a percentage of the window's short edge; the
 * picture is the largest 4:3 that fits inside. Off by default; a page or a
 * launcher sets it, and the mouse is mapped through the same rectangle so
 * the aim lands where the picture is.
 */
static int s_border_pct = 0;
static int s_gap_pct = 0;

void platform_set_gun_border(int border_pct, int gap_pct)
{
    if (border_pct < 0) border_pct = 0;
    if (gap_pct < 0) gap_pct = 0;
    if (border_pct + gap_pct > 6) gap_pct = 6 - border_pct;
    if (gap_pct < 0) { gap_pct = 0; border_pct = 6; }
    s_border_pct = border_pct;
    s_gap_pct = gap_pct;
}

static int border_px(int W, int H, int pct)
{
    int m = W < H ? W : H;
    return pct > 0 ? (pct * m) / 100 : 0;
}

/* The picture inside a W x H window, in the same pixels. */
static SDL_Rect picture_rect(int W, int H, int *border)
{
    int b = border_px(W, H, s_border_pct);
    int inner = s_gap_pct > 0 ? border_px(W, H, s_border_pct + s_gap_pct) : b;
    int ow = W - 2 * inner, oh = H - 2 * inner;
    int w, h;
    if (ow * 3 <= oh * 4) { w = ow; h = (ow * 3) / 4; }
    else                  { h = oh; w = (oh * 4) / 3; }
    SDL_Rect r = { inner + (ow - w) / 2, inner + (oh - h) / 2, w, h };
    if (border) *border = b;
    return r;
}

bool platform_init(const char *title, int width, int height, int scale)
{
    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO | SDL_INIT_GAMECONTROLLER) < 0) {
        fprintf(stderr, "[platform] SDL_Init failed: %s\n", SDL_GetError());
        return false;
    }

    s_width = width;
    s_height = height;

#ifdef __EMSCRIPTEN__
    /* The page's canvas: keys go to it (not the whole page), and it keeps the
     * board's own pixel size - the page's CSS scales it. */
    SDL_SetHint(SDL_HINT_EMSCRIPTEN_KEYBOARD_ELEMENT, "#canvas");
    scale = 1;
    const uint32_t wflags = SDL_WINDOW_SHOWN;
#else
    const uint32_t wflags = SDL_WINDOW_SHOWN | SDL_WINDOW_RESIZABLE;
#endif
    s_window = SDL_CreateWindow(
        title,
        SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
        width * scale, height * scale,
        wflags
    );
    if (!s_window) {
        fprintf(stderr, "[platform] Window creation failed: %s\n", SDL_GetError());
        return false;
    }

    /*
     * No SDL_RENDERER_ACCELERATED here, deliberately. SDL already tries the
     * accelerated drivers first when no flag demands one, so asking for it
     * buys nothing on a desktop - and under SDL_VIDEODRIVER=dummy, which is
     * how the corpus sweep boots every game without a window, it is the
     * difference between a software renderer and SDL_CreateRenderer failing
     * outright. Presentation is not where a Model 2 port spends its time.
     */
    s_renderer = SDL_CreateRenderer(s_window, -1,
        platform_fast_mode() ? 0u : (uint32_t)SDL_RENDERER_PRESENTVSYNC);
    if (!s_renderer) {
        fprintf(stderr, "[platform] Renderer creation failed: %s\n", SDL_GetError());
        return false;
    }

    SDL_RenderSetLogicalSize(s_renderer, width, height);

    /*
     * The framebuffer holds red, green, blue, 0xFF in that byte order, which
     * is the 32-bit value (0xFF << 24) | (b << 16) | (g << 8) | r - and SDL
     * names packed formats from the most significant byte down, so that is
     * ABGR8888.
     *
     * It was RGBX8888, which reads the same value as red 0xFF, green from our
     * blue and blue from our green. Every pixel came out with red at full:
     * on screen that is a solid red pane with the game showing through it,
     * tinted. Screenshots were unaffected because the PPM writer takes bytes
     * 0, 1 and 2 straight off the framebuffer, so the two disagreed and only
     * the window was wrong.
     */
    s_texture = SDL_CreateTexture(s_renderer,
        SDL_PIXELFORMAT_ABGR8888, SDL_TEXTUREACCESS_STREAMING,
        width, height);
    if (!s_texture) {
        fprintf(stderr, "[platform] Texture creation failed: %s\n", SDL_GetError());
        return false;
    }

    /*
     * Ask SDL what that format means and check it against how the framebuffer
     * is packed. Getting this wrong is invisible in screenshots - they are
     * written straight from the framebuffer - so the window can be wrong on
     * its own, and stay wrong.
     */
    {
        int bpp;
        Uint32 rm, gm, bm, am;

        if (!SDL_PixelFormatEnumToMasks(SDL_PIXELFORMAT_ABGR8888, &bpp,
                                        &rm, &gm, &bm, &am) ||
            bpp != 32 || rm != 0x000000FFu || gm != 0x0000FF00u ||
            bm != 0x00FF0000u) {
            fprintf(stderr, "[platform] ERROR: texture format does not match "
                            "the framebuffer (r=%08X g=%08X b=%08X)\n",
                    rm, gm, bm);
            return false;
        }
    }

    /* Audio setup: 44100 Hz stereo int16 */
    SDL_AudioSpec want = {0};
    want.freq = 44100;
    want.format = AUDIO_S16SYS;
    want.channels = 2;
    want.samples = 1024;
    want.callback = NULL; /* Push mode */

    SDL_AudioSpec have;
    s_audio_dev = SDL_OpenAudioDevice(NULL, 0, &want, &have, 0);
    if (s_audio_dev > 0) {
        SDL_PauseAudioDevice(s_audio_dev, 0);
    }

    s_frame_start = SDL_GetPerformanceCounter();

    printf("[platform] SDL2 initialized: %dx%d @ %dx\n", width, height, scale);
    return true;
}

void platform_shutdown(void)
{
    if (s_audio_dev > 0) {
        SDL_CloseAudioDevice(s_audio_dev);
        s_audio_dev = 0;
    }
    if (s_texture)  { SDL_DestroyTexture(s_texture);   s_texture = NULL; }
    if (s_renderer) { SDL_DestroyRenderer(s_renderer); s_renderer = NULL; }
    if (s_window)   { SDL_DestroyWindow(s_window);     s_window = NULL; }
    SDL_Quit();
}

bool platform_poll_events(void)
{
    SDL_Event event;
    while (SDL_PollEvent(&event)) {
        switch (event.type) {
            case SDL_QUIT:
                return false;
            case SDL_KEYDOWN:
                if (event.key.keysym.sym == SDLK_ESCAPE)
                    return false;
                break;
        }
    }
    return true;
}

void platform_present_frame(const uint8_t *framebuffer, int width, int height)
{
    if (!s_texture || !s_renderer || !framebuffer) return;

    SDL_UpdateTexture(s_texture, NULL, framebuffer, width * 4);
    if (s_border_pct > 0) {
        int b;
        SDL_Rect pic = picture_rect(s_width, s_height, &b);
        SDL_Rect gap = { b, b, s_width - 2 * b, s_height - 2 * b };
        SDL_SetRenderDrawColor(s_renderer, 255, 255, 255, 255);   /* the border */
        SDL_RenderClear(s_renderer);
        SDL_SetRenderDrawColor(s_renderer, 0, 0, 0, 255);         /* the gap, and the letterbox */
        SDL_RenderFillRect(s_renderer, &gap);
        SDL_RenderCopy(s_renderer, s_texture, NULL, &pic);
    } else {
        SDL_SetRenderDrawColor(s_renderer, 0, 0, 0, 255);
        SDL_RenderClear(s_renderer);
        SDL_RenderCopy(s_renderer, s_texture, NULL, NULL);
    }
    SDL_RenderPresent(s_renderer);
}

void platform_queue_audio(const int16_t *samples, int num_samples)
{
    if (s_audio_dev > 0 && samples) {
        /* A run that outpaces real time (MODEL2_FAST) would queue without
         * bound; past a second of backlog the samples are dropped. */
        if (SDL_GetQueuedAudioSize(s_audio_dev) > 44100u * 4u) return;
        SDL_QueueAudio(s_audio_dev, samples, num_samples * 2 * sizeof(int16_t));
    }
}

int platform_audio_queued_frames(void)
{
    if (s_audio_dev <= 0) return -1;
    return (int)(SDL_GetQueuedAudioSize(s_audio_dev) / 4);
}

/* MODEL2_FAST=1 runs the board as fast as the host can, for automated tests:
 * no vsync, no frame delay, and no wall-clock gate on the field boundary. A
 * run that takes four minutes of real time to reach attract mode takes a few
 * seconds, which is the difference between being able to bisect a boot and
 * not. Timing-dependent behaviour is not preserved - the game's own counters
 * still run in fields, but anything the host clock feeds is compressed. */
bool platform_fast_mode(void)
{
    static int fast = -1;
    if (fast < 0) {
        const char *e = getenv("MODEL2_FAST");
        fast = (e && atoi(e)) ? 1 : 0;
    }
    return fast != 0;
}

void platform_frame_sync(void)
{
#ifdef __EMSCRIPTEN__
    /* Every field yields to the browser, fast mode or not: a page that never
     * suspends never paints. Fast mode only drops the pacing. */
    if (platform_fast_mode()) { emscripten_sleep(0); return; }
    platform_web_wait_field(FRAME_TIME_MS);
    s_frame_start = SDL_GetPerformanceCounter();
    return;
#endif
    if (platform_fast_mode())
        return;

    uint64_t now = SDL_GetPerformanceCounter();
    double elapsed = (double)(now - s_frame_start) / SDL_GetPerformanceFrequency() * 1000.0;

    if (elapsed < FRAME_TIME_MS) {
        SDL_Delay((uint32_t)(FRAME_TIME_MS - elapsed));
    }

    s_frame_start = SDL_GetPerformanceCounter();
}

void platform_get_mouse(int *x, int *y, bool *left_button, bool *right_button,
                        bool *middle_button)
{
    int mx, my;
    uint32_t buttons = SDL_GetMouseState(&mx, &my);

    /* With the window unfocused the pointer is wherever the desktop left it,
     * which aims the lightgun somewhere arbitrary - and off-screen means
     * "reload" to this game. Park it in the centre instead, so a headless run
     * is repeatable. */
    if (!(SDL_GetWindowFlags(s_window) & SDL_WINDOW_MOUSE_FOCUS)) {
        if (x) *x = s_width / 2;
        if (y) *y = s_height / 2;
        if (left_button) *left_button = false;
        if (right_button) *right_button = false;
        if (middle_button) *middle_button = false;
        return;
    }

    /* Scale mouse coordinates to game resolution, through the picture's
     * rectangle when a border is drawn: aiming at the border reads as past
     * the edge, which the game takes as off-screen. */
    int ww, wh;
    SDL_GetWindowSize(s_window, &ww, &wh);
    int gx = (mx * s_width) / ww;
    int gy = (my * s_height) / wh;
    if (s_border_pct > 0) {
        SDL_Rect pic = picture_rect(s_width, s_height, NULL);
        gx = pic.w > 0 ? ((gx - pic.x) * s_width) / pic.w : gx;
        gy = pic.h > 0 ? ((gy - pic.y) * s_height) / pic.h : gy;
        if (gx < 0) gx = 0; if (gx >= s_width) gx = s_width - 1;
        if (gy < 0) gy = 0; if (gy >= s_height) gy = s_height - 1;
    }
    if (x) *x = gx;
    if (y) *y = gy;
    if (left_button)   *left_button   = (buttons & SDL_BUTTON_LMASK) != 0;
    if (right_button)  *right_button  = (buttons & SDL_BUTTON_RMASK) != 0;
    if (middle_button) *middle_button = (buttons & SDL_BUTTON_MMASK) != 0;
}

bool platform_key_pressed(int scancode)
{
    const uint8_t *state = SDL_GetKeyboardState(NULL);
    return state[scancode] != 0;
}
