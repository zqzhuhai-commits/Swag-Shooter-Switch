#define SDL_MAIN_HANDLED
#include "runtime_services.h"
#include "controls.h"
#include "android_graphics.h"
#include "batch_suite.h"
#include <switch.h>
#include <SDL2/SDL.h>
#include <GLES3/gl3.h>
#include <math.h>
#include <stdlib.h>
#include <string.h>

static GLuint shader(GLenum kind, const char *source, FILE *log) {
    GLuint s = glCreateShader(kind); GLint ok = 0;
    if (!s) return 0;
    glShaderSource(s, 1, &source, NULL); glCompileShader(s); glGetShaderiv(s, GL_COMPILE_STATUS, &ok);
    if (!ok) { char text[1024]; glGetShaderInfoLog(s, sizeof text, NULL, text); fprintf(log, "GPU_SHADER_FAIL %s\n", text); glDeleteShader(s); return 0; }
    return s;
}
static int gpu_probe(FILE *log) {
    const char *version = (const char *)glGetString(GL_VERSION);
    const char *renderer = (const char *)glGetString(GL_RENDERER);
    const char *extensions = (const char *)glGetString(GL_EXTENSIONS);
    GLint texture_size = 0, units = 0;
    glGetIntegerv(GL_MAX_TEXTURE_SIZE, &texture_size); glGetIntegerv(GL_MAX_TEXTURE_IMAGE_UNITS, &units);
    fprintf(log, "GPU_CAPS version=%s renderer=%s max_texture=%d units=%d astc=%d\n",
            version ? version : "null", renderer ? renderer : "null", texture_size, units,
            extensions && strstr(extensions, "texture_compression_astc") != NULL);
    GLuint vs = shader(GL_VERTEX_SHADER, "attribute vec2 p; void main(){gl_Position=vec4(p,0.0,1.0);}", log);
    GLuint fs = shader(GL_FRAGMENT_SHADER, "precision mediump float; void main(){gl_FragColor=vec4(0.25,0.5,0.75,1.0);}", log);
    GLuint program = 0, buffer = 0, texture = 0, fbo = 0; int ok = 0;
    if (!vs || !fs) goto done;
    program = glCreateProgram(); glAttachShader(program, vs); glAttachShader(program, fs);
    glBindAttribLocation(program, 0, "p"); glLinkProgram(program); GLint linked = 0;
    glGetProgramiv(program, GL_LINK_STATUS, &linked); if (!linked) goto done;
    glGenTextures(1, &texture); glBindTexture(GL_TEXTURE_2D, texture);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, 16, 16, 0, GL_RGBA, GL_UNSIGNED_BYTE, NULL);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glGenFramebuffers(1, &fbo); glBindFramebuffer(GL_FRAMEBUFFER, fbo);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, texture, 0);
    if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE) goto done;
    const GLfloat vertices[] = {-1,-1,3,-1,-1,3};
    glGenBuffers(1, &buffer); glBindBuffer(GL_ARRAY_BUFFER, buffer);
    glBufferData(GL_ARRAY_BUFFER, sizeof vertices, vertices, GL_STATIC_DRAW);
    glUseProgram(program); glEnableVertexAttribArray(0); glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 0, NULL);
    glViewport(0, 0, 16, 16); glDrawArrays(GL_TRIANGLES, 0, 3);
    unsigned char pixel[4] = {0}; glReadPixels(8, 8, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, pixel);
    GLenum error = glGetError();
    ok = abs((int)pixel[0] - 64) <= 3 && abs((int)pixel[1] - 128) <= 3 && abs((int)pixel[2] - 191) <= 3 && pixel[3] == 255 && error == GL_NO_ERROR;
    fprintf(log, "GPU_DRAW_READBACK result=%d rgba=%u,%u,%u,%u gl_error=%x\n", ok, pixel[0], pixel[1], pixel[2], pixel[3], error);
done:
    glBindFramebuffer(GL_FRAMEBUFFER, 0); glUseProgram(0); glDisableVertexAttribArray(0);
    if (buffer) glDeleteBuffers(1, &buffer);
    if (texture) glDeleteTextures(1, &texture);
    if (fbo) glDeleteFramebuffers(1, &fbo);
    if (program) glDeleteProgram(program);
    if (vs) glDeleteShader(vs);
    if (fs) glDeleteShader(fs);
    return ok;
}
int runtime_platform_selftest(FILE *log) {
    fprintf(log, "PLATFORM_BEGIN native_backend_only=1 interaction_window=20s\n");
    /* Console and SDL share the default native window; release the console
     * before creating the EGL surface, then restore it after SDL shutdown. */
    consoleExit(NULL);
    SDL_SetMainReady();
    SDL_Window *window = NULL; SDL_GLContext context = NULL;
    SDL_AudioDeviceID audio = 0; SDL_Joystick *joystick[4] = {0};
    int graphics_ok = 0, audio_ok = 0, connected = 0, opened = 0;
    uint64_t observed_actions = 0;
    uint64_t buttons = 0; unsigned axis_motion = 0;
    if (SDL_Init(0)) {
        fprintf(log, "PLATFORM_INIT_FAIL %s\n", SDL_GetError()); goto done;
    }
    if (SDL_InitSubSystem(SDL_INIT_JOYSTICK | SDL_INIT_GAMECONTROLLER)) fprintf(log, "INPUT_INIT_FAIL %s\n", SDL_GetError());
    fprintf(log, "GPU_CONTEXT_BEGIN\n");
    int video_ready = SDL_InitSubSystem(SDL_INIT_VIDEO) == 0;
    if (!video_ready) fprintf(log, "GPU_VIDEO_INIT_FAIL %s\n", SDL_GetError());
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_ES);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 3); SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 0);
    SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1);
    if (video_ready) window = SDL_CreateWindow("SWAG batch test", 0, 0, 1280, 720, SDL_WINDOW_OPENGL | SDL_WINDOW_SHOWN);
    if (window) context = SDL_GL_CreateContext(window);
    if (!context && window) { SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 2); context = SDL_GL_CreateContext(window); }
    if (context) { SDL_GL_MakeCurrent(window, context); SDL_GL_SetSwapInterval(1); graphics_ok = gpu_probe(log);
        int bridge_ok = batch_run("android_egl_context_probe", android_graphics_selftest, graphics_ok, log);
        graphics_ok = graphics_ok && bridge_ok; }
    else fprintf(log, "GPU_CONTEXT_FAIL %s\n", SDL_GetError());
    fprintf(log, "AUDIO_BEGIN\n");
    if (!SDL_InitSubSystem(SDL_INIT_AUDIO)) {
        SDL_AudioSpec want = {0}, have = {0};
        want.freq = 48000; want.format = AUDIO_S16SYS; want.channels = 2; want.samples = 512;
        audio = SDL_OpenAudioDevice(NULL, 0, &want, &have, 0);
        if (audio) {
            size_t frames = 12000; int16_t *tone = calloc(frames * 2, sizeof *tone);
            if (tone) {
                for (size_t i = 0; i < frames; ++i) {
                    int16_t value = (int16_t)(900.0 * sin(6.28318530718 * 440.0 * i / 48000.0));
                    tone[2*i] = tone[2*i+1] = value;
                }
                audio_ok = SDL_QueueAudio(audio, tone, frames * 4) == 0;
                fprintf(log, "AUDIO_QUEUE result=%d driver=%s freq=%d channels=%u initial_bytes=%u\n", audio_ok,
                        SDL_GetCurrentAudioDriver(), have.freq, have.channels, SDL_GetQueuedAudioSize(audio));
                free(tone); SDL_PauseAudioDevice(audio, 0);
            }
        } else fprintf(log, "AUDIO_OPEN_FAIL %s\n", SDL_GetError());
    } else fprintf(log, "AUDIO_INIT_FAIL %s\n", SDL_GetError());
    fprintf(log, "INPUT_BEGIN\n");
    connected = SDL_NumJoysticks();
    fprintf(log, "INPUT_DEVICES count=%d\n", connected);
    for (int i = 0; i < connected && i < 4; ++i) {
        joystick[i] = SDL_JoystickOpen(i);
        if (joystick[i]) { ++opened; fprintf(log, "INPUT_DEVICE index=%d name=%s axes=%d buttons=%d\n", i,
            SDL_JoystickName(joystick[i]), SDL_JoystickNumAxes(joystick[i]), SDL_JoystickNumButtons(joystick[i]));
        } else fprintf(log, "INPUT_OPEN_FAIL index=%d %s\n", i, SDL_GetError());
    }
    PadState pad; SwagControls controls = {0}; uint64_t previous_actions = 0;
    padConfigureInput(1, HidNpadStyleSet_NpadStandard); padInitializeDefault(&pad);
    uint32_t start = SDL_GetTicks();
    while (appletMainLoop() && SDL_GetTicks() - start < 20000) {
        padUpdate(&pad);
        uint64_t actions = swag_controls_update(&controls, padGetButtons(&pad));
        observed_actions |= actions;
        swag_controls_log(log, actions & ~previous_actions, controls.cursor); previous_actions = actions;
        SDL_Event event;
        while (SDL_PollEvent(&event)) {
            if (event.type == SDL_JOYBUTTONDOWN && event.jbutton.button < 64) buttons |= UINT64_C(1) << event.jbutton.button;
            if (event.type == SDL_JOYAXISMOTION && abs(event.jaxis.value) > 8000) ++axis_motion;
        }
        if (context) {
            int width, height; SDL_GL_GetDrawableSize(window, &width, &height); glViewport(0,0,width,height);
            glClearColor(graphics_ok ? 0.04f : 0.4f, 0.12f, controls.cursor ? 0.8f : (buttons ? 0.45f : 0.2f), 1);
            glClear(GL_COLOR_BUFFER_BIT); SDL_GL_SwapWindow(window);
        } else SDL_Delay(16);
    }
    if (audio) { uint32_t left = SDL_GetQueuedAudioSize(audio); audio_ok = audio_ok && left == 0; fprintf(log, "AUDIO_DRAIN result=%d remaining_bytes=%u audible_confirmation=NOT_AUTOMATIC\n", audio_ok, left); }
done:
    {
        const char *names[] = {"jump","crouch","prone","reload","map","primary_smg","pistol","knife","cycle_item","grenade","aim","fire","toggle_cursor","confirm","cancel"};
        for (unsigned i=0;i<sizeof names/sizeof *names;++i)
            fprintf(log,"CONTROL_COVERAGE action=%s status=%s\n",names[i],observed_actions & (UINT64_C(1)<<i) ? "OBSERVED" : "NOT_OBSERVED");
    }
    fprintf(log, "PLATFORM_DONE graphics=%d audio_queue=%d input_connected=%d input_opened=%d buttons_seen=%llx axis_motion=%u game_backends_integrated=0\n",
            graphics_ok, audio_ok, connected, opened, (unsigned long long)buttons, axis_motion);
    for (int i = 0; i < 4; ++i) if (joystick[i]) SDL_JoystickClose(joystick[i]);
    if (audio) SDL_CloseAudioDevice(audio);
    if (context) SDL_GL_DeleteContext(context);
    if (window) SDL_DestroyWindow(window);
    SDL_Quit(); consoleInit(NULL);
    return graphics_ok && audio_ok && opened > 0 ? 0 : -1;
}
