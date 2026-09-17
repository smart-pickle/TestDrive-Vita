#include "host.h"

#if defined(__psp2__) || defined(__vita__)
#include <SDL2/SDL.h>
#include <psp2/ctrl.h>
#include <psp2/kernel/processmgr.h>
#elif __has_include(<SDL2/SDL.h>)
#include <SDL2/SDL.h>
#else
#include <SDL.h>
#endif

#include <dirent.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

#define AUDIO_RATE 44100
#define AUDIO_AMPLITUDE 5000

static SDL_Window *window;
static SDL_Renderer *renderer;
static SDL_Texture *texture;
static SDL_AudioDeviceID audio_dev;
static SDL_GameController *gamepad;
static char *game_dir;

static void (*tick_handler)(void);
static bool (*frame_source)(u32 *);
static u32 *frame;
static int frame_w, frame_h;

/* Aspect ratio mode: 1 = 4:3 pillarbox (default), 0 = 16:9 stretch, 2 = 2x integer */
static int aspect_mode = HOST_ASPECT_4_3;

/* Tick clock: tick n is due at start + n * 11927 / 1193182 s (exact rational arithmetic). */
static Uint64 perf_freq;
static Uint64 clock_start_ns;
static Uint64 ticks_run;

/* BIOS keyboard buffer (15 keys, like the real one). */
#define KBD_SIZE 16
static u16 kbd_buf[KBD_SIZE];
static int kbd_head, kbd_tail;

/* Speaker state and square-wave generator. */
static u16 spk_div;
static bool spk_on;
static double spk_phase;
static double samples_per_tick_frac;

static void process_events(void);
static void pool_shutdown(void);

static inline Uint64 host_ticks_ns(void)
{
    if (!perf_freq) perf_freq = SDL_GetPerformanceFrequency();
    return (SDL_GetPerformanceCounter() * 1000000000ULL) / perf_freq;
}

uint64_t host_time_ns(void) { return host_ticks_ns(); }

static void log_debug(const char *fmt, ...)
{
    FILE *f = fopen("ux0:data/TestDrive/debug.log", "a");
    if (!f) return;
    va_list ap;
    va_start(ap, fmt);
    vfprintf(f, fmt, ap);
    va_end(ap);
    fclose(f);
}

void host_cycle_aspect_ratio(void)
{
    aspect_mode = (aspect_mode + 1) % 3;
    log_debug("[ASPECT] Cycled aspect mode to: %d\n", aspect_mode);
}

int host_aspect_ratio(void)
{
    return aspect_mode;
}

static void compute_dst_rect(SDL_Rect *dst)
{
    int win_w = 960, win_h = 544;
    if (renderer) {
        SDL_GetRendererOutputSize(renderer, &win_w, &win_h);
    }
    if (aspect_mode == HOST_ASPECT_STRETCH) {
        /* Fullscreen 16:9 stretch across the PS Vita display */
        dst->x = 0;
        dst->y = 0;
        dst->w = win_w;
        dst->h = win_h;
    } else if (aspect_mode == HOST_ASPECT_4_3) {
        /* 4:3 aspect ratio fitted inside the window height (725x544 on Vita) */
        int target_w = win_h * 4 / 3;
        if (target_w > win_w) {
            target_w = win_w;
            int target_h = win_w * 3 / 4;
            dst->x = 0;
            dst->y = (win_h - target_h) / 2;
            dst->w = target_w;
            dst->h = target_h;
        } else {
            dst->x = (win_w - target_w) / 2;
            dst->y = 0;
            dst->w = target_w;
            dst->h = win_h;
        }
    } else {
        /* 2x integer scaled retro mode (640x480 centered on 960x544) */
        int target_w = 640;
        int target_h = 480;
        if (target_w > win_w) target_w = win_w;
        if (target_h > win_h) target_h = win_h;
        dst->x = (win_w - target_w) / 2;
        dst->y = (win_h - target_h) / 2;
        dst->w = target_w;
        dst->h = target_h;
    }
}

bool host_init(const char *dir, int window_scale)
{
    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO | SDL_INIT_GAMECONTROLLER) < 0) {
        fprintf(stderr, "SDL_Init failed: %s\n", SDL_GetError());
        return false;
    }
    game_dir = strdup(dir);
    if (window_scale < 1) window_scale = 3;

#if defined(__psp2__) || defined(__vita__)
    window = SDL_CreateWindow("Test Drive Enhanced", SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
                              960, 544, 0);
#else
    window = SDL_CreateWindow("Test Drive Enhanced", SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
                              320 * window_scale, 240 * window_scale, SDL_WINDOW_RESIZABLE);
#endif
    if (!window) {
        fprintf(stderr, "SDL_CreateWindow failed: %s\n", SDL_GetError());
        return false;
    }

    renderer = SDL_CreateRenderer(window, -1, SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC);
    if (!renderer) {
        renderer = SDL_CreateRenderer(window, -1, 0);
    }
    if (!renderer) {
        fprintf(stderr, "SDL_CreateRenderer failed: %s\n", SDL_GetError());
        return false;
    }

    SDL_AudioSpec want, have;
    memset(&want, 0, sizeof(want));
    want.freq = AUDIO_RATE;
    want.format = AUDIO_S16SYS;
    want.channels = 1;
    want.samples = 1024;
    want.callback = NULL;

    audio_dev = SDL_OpenAudioDevice(NULL, 0, &want, &have, 0);
    if (audio_dev) {
        SDL_PauseAudioDevice(audio_dev, 0);
        static s16 silence[AUDIO_RATE / 20];               /* 50 ms of lead-in against underruns */
        SDL_QueueAudio(audio_dev, silence, sizeof(silence));
    } else {
        fprintf(stderr, "audio unavailable: %s\n", SDL_GetError());
    }

    perf_freq = SDL_GetPerformanceFrequency();
    clock_start_ns = host_ticks_ns();
    ticks_run = 0;
    return true;
}

void host_shutdown(void)
{
    pool_shutdown();
    if (gamepad) SDL_GameControllerClose(gamepad);
    if (audio_dev) SDL_CloseAudioDevice(audio_dev);
    if (texture) SDL_DestroyTexture(texture);
    free(frame);
    frame = NULL;
    if (renderer) SDL_DestroyRenderer(renderer);
    if (window) SDL_DestroyWindow(window);
    free(game_dir);
    SDL_Quit();
}

void host_set_tick_handler(void (*handler)(void)) { tick_handler = handler; }

void host_set_frame_source(bool (*compose)(u32 *), int w, int h)
{
    frame_source = compose;
    if (w == frame_w && h == frame_h) return;
    free(frame);
    frame = calloc((size_t)w * (size_t)h, sizeof *frame);
    frame_w = w;
    frame_h = h;
    if (texture) SDL_DestroyTexture(texture);
    texture = SDL_CreateTexture(renderer, SDL_PIXELFORMAT_ARGB8888, SDL_TEXTUREACCESS_STREAMING, w, h);
    SDL_SetTextureScaleMode(texture, w > 320 ? SDL_ScaleModeLinear : SDL_ScaleModeNearest);
}

static inline Uint64 tick_due_ns(Uint64 n)
{
    return clock_start_ns + n * (Uint64)PIT_DIV_GAME * 1000000000ULL / PIT_HZ;
}

static void audio_for_one_tick(void)
{
    if (!audio_dev) return;
    samples_per_tick_frac += (double)AUDIO_RATE * PIT_DIV_GAME / PIT_HZ;
    int n = (int)samples_per_tick_frac;
    samples_per_tick_frac -= n;
    /* Drop output if the device is far behind (e.g. after a stall) instead of building latency. */
    if (SDL_GetQueuedAudioSize(audio_dev) > AUDIO_RATE / 4 * (Uint32)sizeof(s16)) return;
    s16 buf[1024];
    if (n > (int)(sizeof(buf) / sizeof(buf[0]))) n = (int)(sizeof(buf) / sizeof(buf[0]));
    double freq = (double)PIT_HZ / (spk_div ? spk_div : 65536);
    double step = freq / AUDIO_RATE;
    for (int i = 0; i < n; i++) {
        if (spk_on) {
            buf[i] = spk_phase < 0.5 ? AUDIO_AMPLITUDE : -AUDIO_AMPLITUDE;
            spk_phase += step;
            spk_phase -= (int)spk_phase;
        } else {
            buf[i] = 0;
        }
    }
    SDL_QueueAudio(audio_dev, buf, (Uint32)n * (Uint32)sizeof(s16));
}


static void present(void)
{
    if (!texture) return;
    SDL_UpdateTexture(texture, NULL, frame, frame_w * (int)sizeof(u32));
    SDL_SetRenderDrawColor(renderer, 0, 0, 0, 255);
    SDL_RenderClear(renderer);
    SDL_Rect dst;
    compute_dst_rect(&dst);
    SDL_RenderCopy(renderer, texture, NULL, &dst);
    SDL_RenderPresent(renderer);
}

static Uint64 last_present_ns;

void host_present_now(void)
{
    if (frame_source && frame_source(frame)) {
        present();
        last_present_ns = host_ticks_ns();
    }
}

void host_pump(void)
{
    process_events();

    bool worked = false;
    Uint64 now = host_ticks_ns();
    int budget = 50;                                /* at most 0.5 s of catch-up per call */
    while (tick_due_ns(ticks_run + 1) <= now && budget-- > 0) {
        ticks_run++;
        if (tick_handler) tick_handler();
        audio_for_one_tick();
        worked = true;
    }
    if (budget < 0) {                               /* fell too far behind: resynchronise the clock */
        clock_start_ns = now - (tick_due_ns(ticks_run) - clock_start_ns);
    }

    /* Present at most once per ~8 ms; VSync paces it further. */
    if (frame_source && now - last_present_ns >= 8000000ULL) {
        if (frame_source(frame)) {
            present();
            last_present_ns = host_ticks_ns();
            worked = true;
        }
    }
    if (!worked) {
        Uint64 next = tick_due_ns(ticks_run + 1);
        now = host_ticks_ns();
        if (next > now) {
            Uint64 diff = next - now;
            if (diff > 1000000ULL) diff = 1000000ULL;
            SDL_Delay((Uint32)(diff / 1000000ULL));
        }
    }
}

static int frame_rate = 8;          /* ~1987 PC AT + EGA */
static Uint64 next_frame_ns;

/* ---------------------------------------------------------------- worker pool */

#define POOL_MAX 15
static SDL_Thread *pool_threads[POOL_MAX];
static int pool_size = -1;                          /* -1 = not started */
static SDL_sem *pool_wake, *pool_done;
static SDL_atomic_t pool_next;
static int pool_count;
static void (*pool_fn)(int, void *);
static void *pool_ctx;
static bool pool_quit;

static void pool_run(void)
{
    for (;;) {
        int i = SDL_AtomicAdd(&pool_next, 1);       /* returns previous value */
        if (i >= pool_count) return;
        pool_fn(i, pool_ctx);
    }
}

static int pool_worker(void *unused)
{
    (void)unused;
    for (;;) {
        SDL_SemWait(pool_wake);
        if (pool_quit) return 0;
        pool_run();
        SDL_SemPost(pool_done);
    }
}

static void pool_start(void)
{
    int n = SDL_GetCPUCount() - 1;
    if (n > POOL_MAX) n = POOL_MAX;
    pool_size = 0;
    if (n <= 0) return;
    pool_wake = SDL_CreateSemaphore(0);
    pool_done = SDL_CreateSemaphore(0);
    if (!pool_wake || !pool_done) return;
    for (int i = 0; i < n; i++) {
        pool_threads[i] = SDL_CreateThread(pool_worker, "pool", NULL);
        if (!pool_threads[i]) break;
        pool_size++;
    }
}

static void pool_shutdown(void)
{
    if (pool_size <= 0) return;
    pool_quit = true;
    for (int i = 0; i < pool_size; i++) SDL_SemPost(pool_wake);
    for (int i = 0; i < pool_size; i++) SDL_WaitThread(pool_threads[i], NULL);
    SDL_DestroySemaphore(pool_wake);
    SDL_DestroySemaphore(pool_done);
    pool_size = 0;
}

void host_parallel_for(int n, void (*fn)(int i, void *ctx), void *ctx)
{
    if (pool_size < 0) pool_start();
    pool_fn = fn;
    pool_ctx = ctx;
    pool_count = n;
    SDL_AtomicSet(&pool_next, 0);
    int helpers = pool_size < n - 1 ? pool_size : n - 1;
    for (int i = 0; i < helpers; i++) SDL_SemPost(pool_wake);
    pool_run();
    for (int i = 0; i < helpers; i++) SDL_SemWait(pool_done);
}

void host_set_frame_rate(int fps) { frame_rate = fps < 0 ? 0 : fps; }
int  host_frame_rate(void) { return frame_rate; }

void host_frame_begin(void)
{
    host_pump();
    if (frame_rate <= 0) return;
    Uint64 period = 1000000000ULL / (Uint64)frame_rate;
    Uint64 now = host_ticks_ns();
    if (next_frame_ns == 0 || now > next_frame_ns + period) next_frame_ns = now;
    while (host_ticks_ns() < next_frame_ns) host_pump();
    next_frame_ns += period;
}

/* ---------------------------------------------------------------- keyboard */

static void kbd_push(u16 key)
{
    int next = (kbd_tail + 1) % KBD_SIZE;
    if (next == kbd_head) return;                   /* buffer full: BIOS beeps and drops key */
    kbd_buf[kbd_tail] = key;
    kbd_tail = next;
}

bool host_kbd_peek(u16 *key)
{
    process_events();
    if (kbd_head == kbd_tail) return false;
    if (key) *key = kbd_buf[kbd_head];
    return true;
}

bool host_kbd_read(u16 *key)
{
    process_events();
    if (kbd_head == kbd_tail) return false;
    if (key) *key = kbd_buf[kbd_head];
    kbd_head = (kbd_head + 1) % KBD_SIZE;
    return true;
}

void host_kbd_flush(void)
{
    process_events();
    kbd_head = kbd_tail = 0;
}

u8 host_kbd_shift_flags(void)
{
    SDL_Keymod m = SDL_GetModState();
    u8 f = 0;
    if (m & KMOD_RSHIFT) f |= 0x01;
    if (m & KMOD_LSHIFT) f |= 0x02;
    if (m & KMOD_CTRL)   f |= 0x04;
    if (m & KMOD_ALT)    f |= 0x08;
    if (m & KMOD_NUM)    f |= 0x20;
    if (m & KMOD_CAPS)   f |= 0x40;
    return f;
}

static bool held_keys = true;

void host_set_held_keys(bool on) { held_keys = on; }
bool host_held_keys(void) { return held_keys; }

#if defined(__psp2__) || defined(__vita__)
static uint32_t last_vita_buttons = 0;
#endif

/* XT scan codes (set 1) and ASCII for a US keyboard, as INT 16h AH=00h reports them. */
static u16 bios_key(SDL_Keycode k, SDL_Scancode sc, SDL_Keymod mod)
{
    bool shift = (mod & KMOD_SHIFT) != 0, ctrl = (mod & KMOD_CTRL) != 0, alt = (mod & KMOD_ALT) != 0;
    bool caps = (mod & KMOD_CAPS) != 0, num = (mod & KMOD_NUM) != 0;
    static const u8 letter_scan[26] = { 0x1E,0x30,0x2E,0x20,0x12,0x21,0x22,0x23,0x17,0x24,0x25,0x26,0x32,
                                        0x31,0x18,0x19,0x10,0x13,0x1F,0x14,0x16,0x2F,0x11,0x2D,0x15,0x2C };
    if (k >= SDLK_a && k <= SDLK_z) {
        int i = (int)(k - SDLK_a);
        u8 scan = letter_scan[i];
        if (alt) return (u16)(scan << 8);
        if (ctrl) return (u16)(scan << 8 | (i + 1));
        bool upper = shift != caps;
        return (u16)(scan << 8 | ((upper ? 'A' : 'a') + i));
    }
    static const char digits_shift[] = ")!@#$%^&*(";
    if (k >= SDLK_0 && k <= SDLK_9) {
        int d = (int)(k - SDLK_0);
        u8 scan = d == 0 ? 0x0B : (u8)(0x01 + d);
        if (alt) return (u16)((0x78 + (d == 0 ? 9 : d - 1)) << 8);
        return (u16)(scan << 8 | (u8)(shift ? digits_shift[d] : '0' + d));
    }
    /* Numeric keypad: digits with Num Lock, cursor keys without. */
    struct { SDL_Keycode kc; u8 scan; char digit; } kp[] = {
        { SDLK_KP_7, 0x47, '7' }, { SDLK_KP_8, 0x48, '8' }, { SDLK_KP_9, 0x49, '9' },
        { SDLK_KP_4, 0x4B, '4' }, { SDLK_KP_5, 0x4C, '5' }, { SDLK_KP_6, 0x4D, '6' },
        { SDLK_KP_1, 0x4F, '1' }, { SDLK_KP_2, 0x50, '2' }, { SDLK_KP_3, 0x51, '3' },
        { SDLK_KP_0, 0x52, '0' }, { SDLK_KP_PERIOD, 0x53, '.' },
    };
    for (size_t i = 0; i < sizeof(kp)/sizeof(kp[0]); i++)
        if (k == kp[i].kc) return (u16)(kp[i].scan << 8 | ((num != shift) ? (u8)kp[i].digit : (kp[i].scan == 0x4C ? 0 : 0)));
    switch (k) {
    case SDLK_ESCAPE:    return 0x011B;
    case SDLK_RETURN:
    case SDLK_KP_ENTER:  return ctrl ? 0x1C0A : 0x1C0D;
    case SDLK_BACKSPACE: return ctrl ? 0x0E7F : 0x0E08;
    case SDLK_TAB:       return shift ? 0x0F00 : 0x0F09;
    case SDLK_SPACE:     return 0x3920;
    case SDLK_UP:        return 0x4800;
    case SDLK_DOWN:      return 0x5000;
    case SDLK_LEFT:      return 0x4B00;
    case SDLK_RIGHT:     return 0x4D00;
    case SDLK_HOME:      return 0x4700;
    case SDLK_END:       return 0x4F00;
    case SDLK_PAGEUP:    return 0x4900;
    case SDLK_PAGEDOWN:  return 0x5100;
    case SDLK_INSERT:    return 0x5200;
    case SDLK_DELETE:    return 0x5300;
    case SDLK_MINUS:     return shift ? 0x0C5F : 0x0C2D;
    case SDLK_EQUALS:    return shift ? 0x0D2B : 0x0D3D;
    case SDLK_LEFTBRACKET:  return shift ? 0x1A7B : 0x1A5B;
    case SDLK_RIGHTBRACKET: return shift ? 0x1B7D : 0x1B5D;
    case SDLK_SEMICOLON: return shift ? 0x273A : 0x273B;
    case SDLK_QUOTE:     return shift ? 0x2822 : 0x2827;
    case SDLK_BACKQUOTE: return shift ? 0x297E : 0x2960;
    case SDLK_BACKSLASH: return shift ? 0x2B7C : 0x2B5C;
    case SDLK_COMMA:     return shift ? 0x333C : 0x332C;
    case SDLK_PERIOD:    return shift ? 0x343E : 0x342E;
    case SDLK_SLASH:     return shift ? 0x353F : 0x352F;
    case SDLK_KP_MINUS:  return 0x4A2D;
    case SDLK_KP_PLUS:   return 0x4E2B;
    case SDLK_KP_MULTIPLY: return 0x372A;
    default: break;
    }
    if (k >= SDLK_F1 && k <= SDLK_F10) return (u16)((0x3B + (k - SDLK_F1)) << 8);
    (void)sc;
    return 0;
}

static void process_events(void)
{
    SDL_Event ev;
    while (SDL_PollEvent(&ev)) {
        switch (ev.type) {
        case SDL_QUIT:
            host_shutdown();
            exit(0);
        case SDL_KEYDOWN: {
            if (ev.key.keysym.sym == SDLK_RETURN && (ev.key.keysym.mod & KMOD_ALT)) {
                if (!ev.key.repeat) {
                    Uint32 flags = SDL_GetWindowFlags(window);
                    SDL_SetWindowFullscreen(window, (flags & SDL_WINDOW_FULLSCREEN) ? 0 : SDL_WINDOW_FULLSCREEN);
                }
                break;
            }
            u16 key = bios_key(ev.key.keysym.sym, ev.key.keysym.scancode, ev.key.keysym.mod);
            if (key) kbd_push(key);
            break;
        }
        case SDL_CONTROLLERDEVICEADDED:
            if (!gamepad) gamepad = SDL_GameControllerOpen(ev.cdevice.which);
            break;
        case SDL_CONTROLLERDEVICEREMOVED:
            if (gamepad && ev.cdevice.which == SDL_JoystickInstanceID(SDL_GameControllerGetJoystick(gamepad))) {
                SDL_GameControllerClose(gamepad);
                gamepad = NULL;
            }
            break;
#if !defined(__psp2__) && !defined(__vita__)
        case SDL_CONTROLLERBUTTONDOWN:
            if (ev.cbutton.button == SDL_CONTROLLER_BUTTON_BACK) {
                host_cycle_aspect_ratio();
            } else if (ev.cbutton.button == SDL_CONTROLLER_BUTTON_START) {
                kbd_push(0x011B); /* Esc */
            } else if (ev.cbutton.button == SDL_CONTROLLER_BUTTON_A) {
                kbd_push(0x1C0D); /* Enter */
            } else if (ev.cbutton.button == SDL_CONTROLLER_BUTTON_B) {
                kbd_push(0x3920); /* Space */
            }
            break;
#endif
        default:
            break;
        }
    }

#if defined(__psp2__) || defined(__vita__)
    SceCtrlData pad;
    if (sceCtrlPeekBufferPositive(0, &pad, 1) > 0) {
        uint32_t pressed = pad.buttons & ~last_vita_buttons;
        last_vita_buttons = pad.buttons;

        if (pressed & SCE_CTRL_SELECT) {
            host_cycle_aspect_ratio();
        }
        if (pressed & SCE_CTRL_START) {
            kbd_push(0x011B); /* Esc */
        }
        if (pressed & SCE_CTRL_CROSS) {
            kbd_push(0x1C0D); /* Enter */
        }
        if (pressed & SCE_CTRL_CIRCLE) {
            kbd_push(0x3920); /* Space */
        }
        if (pressed & SCE_CTRL_UP) {
            kbd_push(0x4800); /* Up */
        }
        if (pressed & SCE_CTRL_DOWN) {
            kbd_push(0x5000); /* Down */
        }
        if (pressed & SCE_CTRL_LEFT) {
            kbd_push(0x4B00); /* Left */
        }
        if (pressed & SCE_CTRL_RIGHT) {
            kbd_push(0x4D00); /* Right */
        }
    }
#endif
}

bool host_joy_read(s16 *x, s16 *y, u8 *buttons)
{
#if defined(__psp2__) || defined(__vita__)
    SceCtrlData pad;
    if (sceCtrlPeekBufferPositive(0, &pad, 1) > 0) {
        s16 ax = (s16)(((int)pad.lx - 128) * 256);
        s16 ay = (s16)(((int)pad.ly - 128) * 256);
        if (pad.buttons & SCE_CTRL_LEFT)  ax = -32768;
        if (pad.buttons & SCE_CTRL_RIGHT) ax = 32767;
        if (pad.buttons & SCE_CTRL_UP)    ay = -32768;
        if (pad.buttons & SCE_CTRL_DOWN)  ay = 32767;
        u8 b = 0;
        if (pad.buttons & (SCE_CTRL_CROSS | SCE_CTRL_RTRIGGER)) b |= 1;
        if (pad.buttons & (SCE_CTRL_SQUARE | SCE_CTRL_LTRIGGER)) b |= 2;
        if (x) *x = ax;
        if (y) *y = ay;
        if (buttons) *buttons = b;
        return true;
    }
#endif
    if (!gamepad) return false;
    s16 ax = SDL_GameControllerGetAxis(gamepad, SDL_CONTROLLER_AXIS_LEFTX);
    s16 ay = SDL_GameControllerGetAxis(gamepad, SDL_CONTROLLER_AXIS_LEFTY);
    if (SDL_GameControllerGetButton(gamepad, SDL_CONTROLLER_BUTTON_DPAD_LEFT))  ax = -32768;
    if (SDL_GameControllerGetButton(gamepad, SDL_CONTROLLER_BUTTON_DPAD_RIGHT)) ax = 32767;
    if (SDL_GameControllerGetButton(gamepad, SDL_CONTROLLER_BUTTON_DPAD_UP))    ay = -32768;
    if (SDL_GameControllerGetButton(gamepad, SDL_CONTROLLER_BUTTON_DPAD_DOWN))  ay = 32767;
    u8 b = 0;
    if (SDL_GameControllerGetButton(gamepad, SDL_CONTROLLER_BUTTON_A)) b |= 1;
    if (SDL_GameControllerGetButton(gamepad, SDL_CONTROLLER_BUTTON_B)) b |= 2;
    if (x) *x = ax;
    if (y) *y = ay;
    if (buttons) *buttons = b;
    return true;
}

bool host_xt_key_down(u8 xt)
{
    const Uint8 *ks = SDL_GetKeyboardState(NULL);
    bool num = (SDL_GetModState() & KMOD_NUM) != 0;
#define KP(sc) (!num && ks[sc])
    bool k_down = false;
    switch (xt) {
    case 0x1E: k_down = ks[SDL_SCANCODE_A]; break;
    case 0x2C: k_down = ks[SDL_SCANCODE_Z]; break;
    case 0x47: k_down = ks[SDL_SCANCODE_HOME]     || KP(SDL_SCANCODE_KP_7); break;
    case 0x48: k_down = ks[SDL_SCANCODE_UP]       || KP(SDL_SCANCODE_KP_8); break;
    case 0x49: k_down = ks[SDL_SCANCODE_PAGEUP]   || KP(SDL_SCANCODE_KP_9); break;
    case 0x4B: k_down = ks[SDL_SCANCODE_LEFT]     || KP(SDL_SCANCODE_KP_4); break;
    case 0x4D: k_down = ks[SDL_SCANCODE_RIGHT]    || KP(SDL_SCANCODE_KP_6); break;
    case 0x4F: k_down = ks[SDL_SCANCODE_END]      || KP(SDL_SCANCODE_KP_1); break;
    case 0x50: k_down = ks[SDL_SCANCODE_DOWN]     || KP(SDL_SCANCODE_KP_2); break;
    case 0x51: k_down = ks[SDL_SCANCODE_PAGEDOWN] || KP(SDL_SCANCODE_KP_3); break;
    default:   k_down = false; break;
    }
#undef KP
    if (k_down) return true;

#if defined(__psp2__) || defined(__vita__)
    SceCtrlData pad;
    if (sceCtrlPeekBufferPositive(0, &pad, 1) > 0) {
        switch (xt) {
        case 0x1E: /* Shift Up: Triangle */
            return (pad.buttons & SCE_CTRL_TRIANGLE) != 0;
        case 0x2C: /* Shift Down: Square or Circle */
            return (pad.buttons & (SCE_CTRL_SQUARE | SCE_CTRL_CIRCLE)) != 0;
        case 0x48: /* Accelerate: Up, Cross, or R Trigger */
            return (pad.buttons & (SCE_CTRL_UP | SCE_CTRL_CROSS | SCE_CTRL_RTRIGGER | SCE_CTRL_R1)) != 0 || pad.ly < 80;
        case 0x50: /* Brake: Down, Square, or L Trigger */
            return (pad.buttons & (SCE_CTRL_DOWN | SCE_CTRL_SQUARE | SCE_CTRL_LTRIGGER | SCE_CTRL_L1)) != 0 || pad.ly > 175;
        case 0x4B: /* Steer Left */
            return (pad.buttons & SCE_CTRL_LEFT) != 0 || pad.lx < 80;
        case 0x4D: /* Steer Right */
            return (pad.buttons & SCE_CTRL_RIGHT) != 0 || pad.lx > 175;
        default: break;
        }
    }
#endif

    if (gamepad) {
        switch (xt) {
        case 0x1E: /* Shift Up: Y */
            return SDL_GameControllerGetButton(gamepad, SDL_CONTROLLER_BUTTON_Y) != 0;
        case 0x2C: /* Shift Down: X */
            return SDL_GameControllerGetButton(gamepad, SDL_CONTROLLER_BUTTON_X) != 0;
        case 0x48: /* Accelerate: Up, A, or R Trigger */
            return SDL_GameControllerGetButton(gamepad, SDL_CONTROLLER_BUTTON_DPAD_UP) ||
                   SDL_GameControllerGetButton(gamepad, SDL_CONTROLLER_BUTTON_A) ||
                   SDL_GameControllerGetButton(gamepad, SDL_CONTROLLER_BUTTON_RIGHTSHOULDER) ||
                   SDL_GameControllerGetAxis(gamepad, SDL_CONTROLLER_AXIS_LEFTY) < -16384;
        case 0x50: /* Brake: Down, B, or L Trigger */
            return SDL_GameControllerGetButton(gamepad, SDL_CONTROLLER_BUTTON_DPAD_DOWN) ||
                   SDL_GameControllerGetButton(gamepad, SDL_CONTROLLER_BUTTON_B) ||
                   SDL_GameControllerGetButton(gamepad, SDL_CONTROLLER_BUTTON_LEFTSHOULDER) ||
                   SDL_GameControllerGetAxis(gamepad, SDL_CONTROLLER_AXIS_LEFTY) > 16384;
        case 0x4B: /* Steer Left */
            return SDL_GameControllerGetButton(gamepad, SDL_CONTROLLER_BUTTON_DPAD_LEFT) ||
                   SDL_GameControllerGetAxis(gamepad, SDL_CONTROLLER_AXIS_LEFTX) < -16384;
        case 0x4D: /* Steer Right */
            return SDL_GameControllerGetButton(gamepad, SDL_CONTROLLER_BUTTON_DPAD_RIGHT) ||
                   SDL_GameControllerGetAxis(gamepad, SDL_CONTROLLER_AXIS_LEFTX) > 16384;
        default: break;
        }
    }

    return false;
}

/* ---------------------------------------------------------------- speaker, files, errors */

void host_speaker(u16 divisor, bool on)
{
    spk_div = divisor;
    spk_on = on;
}

static char *search_dir_case_insensitive(const char *dir, const char *name)
{
    DIR *d = opendir(dir);
    if (!d) return NULL;
    struct dirent *ent;
    char *found = NULL;
    while ((ent = readdir(d)) != NULL) {
        if (strcasecmp(ent->d_name, name) == 0) {
            size_t len = strlen(dir) + 1 + strlen(ent->d_name) + 1;
            found = malloc(len);
            if (found) snprintf(found, len, "%s/%s", dir, ent->d_name);
            break;
        }
    }
    closedir(d);
    return found;
}

char *host_game_path(const char *name, bool create)
{
#if defined(__psp2__) || defined(__vita__)
    if (create) {
        char *path = malloc(256);
        if (path) snprintf(path, 256, "ux0:data/TestDrive/%s", name);
        return path;
    }
    /* Check ux0:data/TestDrive first for user mod/save files */
    char *user_path = search_dir_case_insensitive("ux0:data/TestDrive", name);
    if (user_path) return user_path;
    /* Check app0:Game (VPK bundled assets) */
    char *app_path = search_dir_case_insensitive("app0:Game", name);
    if (app_path) return app_path;
#endif

    if (create) {
        size_t len = strlen(game_dir) + 1 + strlen(name) + 1;
        char *path = malloc(len);
        if (path) snprintf(path, len, "%s/%s", game_dir, name);
        return path;
    }

    /* Direct path check */
    size_t direct_len = strlen(game_dir) + 1 + strlen(name) + 1;
    char *direct = malloc(direct_len);
    if (direct) {
        snprintf(direct, direct_len, "%s/%s", game_dir, name);
        struct stat st;
        if (stat(direct, &st) == 0) return direct;
        free(direct);
    }

    return search_dir_case_insensitive(game_dir, name);
}

void host_free(void *p)
{
    free(p);
}

_Noreturn void host_fatal(const char *fmt, ...)
{
    char msg[512];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(msg, sizeof msg, fmt, ap);
    va_end(ap);
    fprintf(stderr, "fatal: %s\n", msg);
    SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, "Test Drive", msg, window);
    host_shutdown();
    exit(3);
}
