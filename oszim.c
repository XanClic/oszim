#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif

#include <errno.h>
#include <fcntl.h>
#include <getopt.h>
#include <math.h>
#include <signal.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <GL/gl.h>
#include <SDL.h>

#define SAMPLE_RATE 44100
#define BATCH_SIZE (SAMPLE_RATE / 100)
#define WINDOW_SIZE 1024

#define __STRINGIFY(r) #r
#define STRINGIFY(r) __STRINGIFY(r)

static inline float min(float a, float b)
{
    return a < b ? a : b;
}

static inline void set_color(float x1, float y1, float x2, float y2, int main_channel)
{
    float len = sqrtf(powf(x1 - x2, 2.f) + powf(y1 - y2, 2.f));
    switch (main_channel) {
        case 0: glColor3f(min(.014f / len, 1.f), min(.0014f / len, 1.f), min(.0014f / len, 1.f)); break;
        case 1: glColor3f(min(.0010f / len, 1.f), min(.010f / len, 1.f), min(.0010f / len, 1.f)); break;
        case 2: glColor3f(min(.0018f / len, 1.f), min(.0018f / len, 1.f), min(.018f / len, 1.f)); break;
    }
}

typedef struct Frame {
    float l, r;
} Frame;

typedef struct SoundSource {
    Frame *buffer;
    int position;
    int frames;
} SoundSource;

void audio_callback(void *opaque, uint8_t *stream, int len)
{
    SoundSource *ss = opaque;

    if (ss->position + len > ss->frames) {
        return;
    }

    memcpy(stream, ss->buffer + ss->position, len);
    ss->position += len / sizeof(Frame);
}

int main(int argc, char **argv)
{
    static const struct option options[] = {
        {"help", no_argument, NULL, 'h'},
        {"rotate", no_argument, NULL, 'r'},
        {"delay", required_argument, NULL, 'd'},
        {"visualize", no_argument, NULL, 'v'},
        {"fullscreen", no_argument, NULL, 'f'},

        {NULL, 0, NULL, 0}
    };


    bool help = false, rotate = false, visualize = false, fullscreen = false;
    unsigned long delay = 0;

    for (;;) {
        int option = getopt_long(argc, argv, "hrd:vf", options, NULL);
        if (option == -1) {
            break;
        }

        switch (option) {
            case 'h':
            case '?':
                help = true;
                break;

            case 'r':
                rotate = true;
                break;

            case 'd': {
                char *endp;
                errno = 0;
                delay = strtoul(optarg, &endp, 0);
                if (errno || (delay > WINDOW_SIZE) || *endp) {
                    fprintf(stderr, "Invalid argument given for --delay\n");
                    return 1;
                }
                break;
            }

            case 'v':
                visualize = true;
                break;

            case 'f':
                fullscreen = true;
                break;
        }
    }

    if (help || (optind != argc - 1) || (visualize && delay)) {
        fprintf(stderr, "Usage: oszim [-r] [-d <delay> | -v] <sound input>\n");
        return 1;
    }

    char *raw_name = tmpnam(NULL), *system_buf;
    asprintf(&system_buf, "ffmpeg -loglevel error -i \"%s\" -f f32le -ac 2 \"%s\"", argv[optind], raw_name);
    if (system(system_buf)) {
        return 1;
    }

    free(system_buf);

    FILE *fp = fopen(raw_name, "rb");
    if (!fp) {
        perror("Failed to open sound file");
        return 1;
    }

    fseek(fp, 0, SEEK_END);
    size_t lof = ftell(fp);
    rewind(fp);

    SoundSource *ss = malloc(sizeof(*ss));
    ss->position = 0;
    ss->frames = lof / sizeof(Frame);
    ss->buffer = malloc(lof);
    fread(ss->buffer, 1, lof, fp);
    rewind(fp);

    SDL_AudioSpec sdl_as = {0};
    sdl_as.callback = audio_callback;
    sdl_as.channels = 2;
    sdl_as.format = AUDIO_F32LSB;
    sdl_as.freq = SAMPLE_RATE;
    sdl_as.samples = SAMPLE_RATE / 10; // 0.1 s delay
    sdl_as.userdata = ss;

    SDL_Init(SDL_INIT_VIDEO | SDL_INIT_EVENTS | SDL_INIT_TIMER | SDL_INIT_AUDIO);
    atexit(SDL_Quit);

    int wsx = WINDOW_SIZE, wsy = WINDOW_SIZE;
    if (rotate) {
        wsx = (int)(WINDOW_SIZE * 1.25f);
        wsy = (int)(WINDOW_SIZE * 0.703125f);
    }

    SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 0);

    SDL_Window *wnd = SDL_CreateWindow("oszim", SDL_WINDOWPOS_UNDEFINED, SDL_WINDOWPOS_UNDEFINED, wsx, wsy, SDL_WINDOW_OPENGL | SDL_WINDOW_RESIZABLE);
    if (fullscreen) {
        SDL_SetWindowFullscreen(wnd, SDL_WINDOW_FULLSCREEN_DESKTOP);
    }
    SDL_GL_CreateContext(wnd);

    if (SDL_OpenAudio(&sdl_as, NULL) < 0) {
        fprintf(stderr, "Failed to open audio: %s\n", SDL_GetError());
        unlink(raw_name);
        return 1;
    }

    Frame *samples, last_sample, *input_data;
    input_data = calloc(BATCH_SIZE * 2, sizeof(Frame));

    if (delay || visualize) {
        samples = malloc(BATCH_SIZE * sizeof(Frame));
    } else {
        samples = input_data + BATCH_SIZE;
    }

    glViewport(0, 0, wsx, wsy);
    glClear(GL_COLOR_BUFFER_BIT);

    if (rotate) {
        glMatrixMode(GL_VIEWPORT);
        glRotatef(45.f, 0.f, 0.f, -1.f);
    }

    glLineWidth(3.f);

    glEnable(GL_BLEND);

    SDL_PauseAudio(0);
    SDL_Delay(100);

    int32_t expected = 0, start_ticks = SDL_GetTicks();
    bool quit = false;

    int start_channel = visualize ? 0 : 1;
    int end_channel = visualize ? 2 : 1;
    int channel_delay[3] = { 0 };
    if (delay) {
        channel_delay[0] = channel_delay[1] = channel_delay[2] = delay;
    } else if (visualize) {
        channel_delay[0] = 402;
        channel_delay[1] = 53;
        channel_delay[2] = 11;
    }

    while (!quit) {
        if (delay || visualize) {
            memcpy(input_data, input_data + BATCH_SIZE, sizeof(Frame) * BATCH_SIZE);
        }

        fread(input_data + BATCH_SIZE, sizeof(Frame), BATCH_SIZE, fp);
        if (feof(fp)) {
            break;
        }


        for (int channel = start_channel; channel <= end_channel; channel++) {
            delay = channel_delay[channel];

            if (delay) {
                for (int i = 0; i < BATCH_SIZE; i++) {
                    samples[i].l = .5f * (input_data[i + BATCH_SIZE].l + input_data[i + BATCH_SIZE].r);
                    samples[i].r = .5f * (input_data[i + BATCH_SIZE - delay].l + input_data[i + BATCH_SIZE - delay].r);
                }
            }

            glBlendFunc(GL_ONE, GL_ONE);

            glBegin(GL_LINES);
            set_color(last_sample.l, last_sample.r, samples[0].l, samples[0].r, channel);
            glVertex2f(last_sample.l, last_sample.r);
            glVertex2f(samples[0].l, samples[0].r);

            for (int i = 0; i < BATCH_SIZE - 1; i++) {
                set_color(samples[i].l, samples[i].r, samples[i + 1].l, samples[i + 1].r, channel);
                glVertex2f(samples[i].l, samples[i].r);
                glVertex2f(samples[i + 1].l, samples[i + 1].r);
            }

            last_sample = samples[BATCH_SIZE - 1];
            glEnd();

            glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

            glBegin(GL_QUADS);
            glColor4f(0.f, 0.f, 0.f, .2f);
            glVertex2f(-2.f, -2.f);
            glVertex2f( 2.f, -2.f);
            glVertex2f( 2.f,  2.f);
            glVertex2f(-2.f,  2.f);
            glEnd();

            glFinish();
        }

        expected += 1000 * BATCH_SIZE / SAMPLE_RATE;
        int32_t now_ticks = SDL_GetTicks() - start_ticks;
        if (now_ticks < expected) {
            SDL_Delay(expected - now_ticks);
        }

        SDL_Event evt;
        while (SDL_PollEvent(&evt)) {
            if (evt.type == SDL_QUIT) {
                quit = true;
            } else if (evt.type == SDL_WINDOWEVENT) {
                if (evt.window.event == SDL_WINDOWEVENT_RESIZED) {
                    glViewport(0, 0, evt.window.data1, evt.window.data2);
                    glClear(GL_COLOR_BUFFER_BIT);
                }
            }
        }
    }

    unlink(raw_name);

    return 0;
}
