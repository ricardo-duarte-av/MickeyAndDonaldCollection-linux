// Linux: SDL window and event loop (windowed or full screen), keys / mouse / controller events, and the parts of
// App that are per platform. The drawing, sound and controllers are the Android build's SDL + OpenGL ES 3 layer
// (src/platform/android); the window is an ordinary desktop one, as on Windows.
#include "app/app.h"
#include "platform/android/sdl_keys.h"
#include <SDL.h>
#include <GLES3/gl3.h>
#include <algorithm>
#include <cstdlib>
#include <cstring>

namespace {
int g_argc = 0;
char** g_argv = nullptr;

const char* kTitle = "Mickey & Donald Collection";
}  // namespace

int App::run() {
    // The window's class / app id matches the .desktop file, so docks and task bars show its icon.
    setenv("SDL_VIDEO_X11_WMCLASS", "mickey-donald-collection", 0);
    setenv("SDL_VIDEO_WAYLAND_WMCLASS", "mickey-donald-collection", 0);
    // OpenGL ES through EGL everywhere (X11 and Wayland): the renderer is the Android one (GLES 3.0).
    SDL_SetHint(SDL_HINT_VIDEO_X11_FORCE_EGL, "1");
    SDL_SetHint(SDL_HINT_OPENGL_ES_DRIVER, "1");
    SDL_SetHint(SDL_HINT_VIDEO_MINIMIZE_ON_FOCUS_LOSS, "0");
    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO | SDL_INIT_GAMECONTROLLER | SDL_INIT_EVENTS) != 0) {
        fprintf(stderr, "SDL_Init failed: %s\n", SDL_GetError());
        return 1;
    }
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_ES);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 3);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 0);
    SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1);
    SDL_GL_SetAttribute(SDL_GL_DEPTH_SIZE, 0);

    settings_path_ = path_join(data_dir(), L"settings.ini");
    settings_.load(settings_path_);
    make_dir(path_join(data_dir(), L"roms"));

    games_[0].module.reset(create_coi_module());
    games_[1].module.reset(create_qs_module());
    games_[2].module.reset(create_woi_module());

    // --autotest <dir> <script>: the scripted test run (keys, screenshots into <dir>; see App::load_script), as on
    // Windows; --capture-size WxH fixes the window's size for it.
    for (int i = 1; i + 2 < g_argc; i++)
        if (!strcmp(g_argv[i], "--autotest")) {
            autotest_dir_ = from_utf8(g_argv[i + 1]);
            make_dir(autotest_dir_);
            autotest_ = load_script(from_utf8(g_argv[i + 2]));
        }
    for (int i = 1; i + 1 < g_argc; i++)
        if (!strcmp(g_argv[i], "--capture-size") && sscanf(g_argv[i + 1], "%dx%d", &capture_w_, &capture_h_) != 2)
            capture_w_ = capture_h_ = 0;
    if (autotest_) { settings_.fullscreen = false; settings_.vsync = true; }
    else capture_w_ = capture_h_ = 0;

    const int ext = wide_ext(settings_.widescreen);
    const int scale = std::max(1, settings_.window_scale);
    const int win_w = capture_w_ > 0 ? capture_w_ : (320 + ext * 2) * scale;
    const int win_h = capture_w_ > 0 ? capture_h_ : 224 * scale;
    window_ = SDL_CreateWindow(kTitle, SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED, win_w, win_h,
                               SDL_WINDOW_OPENGL | SDL_WINDOW_RESIZABLE | SDL_WINDOW_ALLOW_HIGHDPI);
    if (!window_) {
        fprintf(stderr, "Window could not be created: %s\n", SDL_GetError());
        SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, kTitle, "The window could not be created (OpenGL ES 3.0 is needed).", nullptr);
        return 1;
    }
    SDL_SetWindowMinimumSize(window_, 480, 300);
    int w = 0, h = 0;
    SDL_GL_GetDrawableSize(window_, &w, &h);
    const bool video_ok = video_.init(window_, w, h);
    {
        // One line for bug reports: which display server and GL driver this is running on.
        const char* drv = SDL_GetCurrentVideoDriver();
        const char *ver = nullptr, *ren = nullptr;
        if (SDL_GL_GetCurrentContext()) {
            ver = (const char*)glGetString(GL_VERSION);
            ren = (const char*)glGetString(GL_RENDERER);
        }
        fprintf(stderr, "MickeyDonald: video driver %s, GL %s, renderer %s\n", drv ? drv : "?", ver ? ver : "(none)",
                ren ? ren : "(none)");
    }
    if (!video_ok) {
        fprintf(stderr, "OpenGL ES 3.0 could not be started: %s\n", SDL_GetError());
        SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, kTitle, "OpenGL ES 3.0 could not be started.", window_);
        return 1;
    }
    audio_.init(48000);
    input_.init(window_);

    find_game_roms();
    scan_soundtracks();
    apply_settings();
    ach_startup();
    if (settings_.fullscreen) set_fullscreen(true);
    for (int g = 0; g < kGameCount; g++)
        if (settings_.last_game == games_[g].module->id()) launcher_sel_ = launcher_last_card_ = g;

    // Command line: --game coi|qs|woi starts a game directly.
    for (int i = 1; i + 1 < g_argc; i++)
        if (!strcmp(g_argv[i], "--game"))
            for (int g = 0; g < kGameCount; g++)
                if (games_[g].module->id() == std::string(g_argv[i + 1])) launch(g);

    // Window coordinates (mouse) -> drawable pixels (what the launcher lays out in), for high-DPI screens.
    auto to_pixels = [this](int x, int y, float& px, float& py) {
        int ww = 1, wh = 1;
        SDL_GetWindowSize(window_, &ww, &wh);
        px = x * (float)video_.width() / std::max(1, ww);
        py = y * (float)video_.height() / std::max(1, wh);
    };
    SDL_Cursor* arrow = SDL_CreateSystemCursor(SDL_SYSTEM_CURSOR_ARROW);
    SDL_Cursor* hand = SDL_CreateSystemCursor(SDL_SYSTEM_CURSOR_HAND);

    const Uint64 freq = SDL_GetPerformanceFrequency();
    Uint64 last = SDL_GetPerformanceCounter();
    bool minimized = false;
    while (running_) {
        SDL_Event e;
        while (SDL_PollEvent(&e)) {
            switch (e.type) {
            case SDL_QUIT: running_ = false; break;
            case SDL_WINDOWEVENT:
                switch (e.window.event) {
                case SDL_WINDOWEVENT_SIZE_CHANGED:
                case SDL_WINDOWEVENT_RESIZED:
                    SDL_GL_GetDrawableSize(window_, &w, &h);
                    video_.resize(w, h);
                    break;
                case SDL_WINDOWEVENT_MINIMIZED: minimized = true; break;
                case SDL_WINDOWEVENT_RESTORED:
                case SDL_WINDOWEVENT_SHOWN: minimized = false; break;
                // Losing focus releases all keys (no stuck keys).
                case SDL_WINDOWEVENT_FOCUS_LOST:
                    active_window_ = false;
                    input_.on_focus_lost();
                    break;
                case SDL_WINDOWEVENT_FOCUS_GAINED: active_window_ = true; break;
                default: break;
                }
                break;
            case SDL_KEYDOWN:
            case SDL_KEYUP: {
                if (e.key.repeat) break;
                // Alt+Enter: full screen (as on Windows; F11 is handled by the app itself)
                if (e.type == SDL_KEYDOWN && (e.key.keysym.mod & KMOD_ALT) &&
                    (e.key.keysym.scancode == SDL_SCANCODE_RETURN || e.key.keysym.scancode == SDL_SCANCODE_KP_ENTER)) {
                    settings_.fullscreen = !is_fullscreen_;
                    set_fullscreen(settings_.fullscreen);
                    break;
                }
                int vk = vk_from_scancode(e.key.keysym.scancode);
                if (vk) input_.on_key(vk, e.type == SDL_KEYDOWN);
                break;
            }
            case SDL_CONTROLLERDEVICEADDED:
            case SDL_CONTROLLERDEVICEREMOVED:
            case SDL_JOYDEVICEADDED:
            case SDL_JOYDEVICEREMOVED:
                input_.on_device_change();
                break;
            // The launcher's logo / Patreon page and the update badge are links: a hand over them, a click opens them.
            case SDL_MOUSEMOTION:
                if (!is_fullscreen_) {
                    float x, y;
                    to_pixels(e.motion.x, e.motion.y, x, y);
                    const bool link = mode_ == Mode::Launcher && !menu_.active() &&
                                      (over_launcher_link(x, y) || over_update_badge(x, y));
                    SDL_SetCursor(link ? hand : arrow);
                }
                break;
            case SDL_MOUSEBUTTONDOWN:
                if (e.button.button == SDL_BUTTON_LEFT && mode_ == Mode::Launcher && !menu_.active()) {
                    float x, y;
                    to_pixels(e.button.x, e.button.y, x, y);
                    if (over_launcher_link(x, y)) open_patreon();
                    else if (over_update_badge(x, y)) open_update_page();
                }
                break;
            default: break;
            }
        }
        if (!running_) break;
        Uint64 now = SDL_GetPerformanceCounter();
        double dt = (double)(now - last) / freq;
        last = now;
        if (dt > 0.25) dt = 0.25;
        if (minimized) {
            audio_.set_paused(true);
            SDL_Delay(20);
            continue;
        }
        audio_.set_paused(false);
        frame(dt);
        if (!settings_.vsync) {
            // Software pacing when not synchronised to the display (fast forward too: it runs more game frames
            // per displayed frame, not more displayed frames).
            const double target = 1.0 / (kEmuFps * hfr_subs_);
            for (;;) {
                const double el = (double)(SDL_GetPerformanceCounter() - last) / freq;
                if (el >= target) break;
                if (target - el > 0.002) SDL_Delay(1);
            }
        }
    }
    save_settings();   // (not in an autotest run: see App::save_settings)
    jukebox_.close();
    session_.reset();
    for (auto& g : games_) g.preview.reset();
    input_.shutdown();
    audio_.shutdown();
    video_.shutdown();
    SDL_FreeCursor(arrow);
    SDL_FreeCursor(hand);
    SDL_DestroyWindow(window_);
    SDL_Quit();
    return 0;
}

// Full screen on the window's display, at the desktop's own mode (no mode switch).
void App::set_fullscreen(bool fs) {
    if (!window_ || fs == is_fullscreen_) return;
    SDL_SetWindowFullscreen(window_, fs ? SDL_WINDOW_FULLSCREEN_DESKTOP : 0);
    SDL_ShowCursor(fs ? SDL_DISABLE : SDL_ENABLE);
    is_fullscreen_ = fs;
    settings_.fullscreen = fs;
}

void App::resize_window_to_scale() {
    if (!window_ || is_fullscreen_ || capture_w_ > 0) return;
    const int ext = wide_ext(settings_.widescreen);
    const int scale = std::max(1, settings_.window_scale);
    SDL_SetWindowSize(window_, (320 + ext * 2) * scale, 224 * scale);
}

// (the window manager places windows on Linux: drag the window to another display)
void App::move_to_monitor(int) {}

double App::refresh_rate() {
    if (test_refresh_ > 0) return test_refresh_;
    SDL_DisplayMode m;
    const int d = window_ ? std::max(0, SDL_GetWindowDisplayIndex(window_)) : 0;
    if (SDL_GetCurrentDisplayMode(d, &m) == 0 && m.refresh_rate > 1) return m.refresh_rate == 59 ? 59.94 : (double)m.refresh_rate;
    return 60.0;
}

// (slang presets need librashader's Direct3D 11 runtime: Windows only)
void App::apply_slang() {}
MenuPage App::slang_page() { return MenuPage(); }

// Trailer recording is a Windows test feature.
void App::start_recording(const std::string&) {}
void App::record_frame() {}
void App::stop_recording() {}

int main(int argc, char* argv[]) {
    g_argc = argc;
    g_argv = argv;
    App app;
    return app.run();
}
