#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif

#include <math.h>
#include <fcntl.h>
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

static inline void set_color(float x1, float y1, float x2, float y2)
{
    float len = sqrtf(powf(x1 - x2, 2.f) + powf(y1 - y2, 2.f));
    glColor3f(min(.001f / len, 1.f), min(.01f / len, 1.f), min(.001f / len, 1.f));
}

int main(int argc, char **argv)
{
    if (argc < 2) {
        fprintf(stderr, "Usage: oszim [-r] <sound input>\n");
        return 1;
    }

    bool rotate = false;
    if (!strcmp(argv[1], "-r")) {
        rotate = true;
    }

    FILE *fp = fopen(argv[rotate + 1], "rb");
    if (!fp) {
        perror("Failed to open sound file");
        return 1;
    }


    pid_t child = fork();
    if (!child) {
        close(0);
        open("/dev/null", O_RDONLY);
        execlp("mpv", "--vo=null", "--demuxer=rawaudio", "--demuxer-rawaudio-format=float", "--demuxer-rawaudio-channels=2", "--demuxer-rawaudio-rate=" STRINGIFY(SAMPLE_RATE), argv[rotate + 1], NULL);
        kill(getppid(), SIGTERM);
    }

    SDL_Init(SDL_INIT_VIDEO | SDL_INIT_EVENTS | SDL_INIT_TIMER);
    atexit(SDL_Quit);

    int wsx = WINDOW_SIZE, wsy = WINDOW_SIZE;
    if (rotate) {
        wsx = (int)(WINDOW_SIZE * 1.25f);
        wsy = (int)(WINDOW_SIZE * 0.703125f);
    }

    SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 0);

    SDL_Window *wnd = SDL_CreateWindow("oszim", SDL_WINDOWPOS_UNDEFINED, SDL_WINDOWPOS_UNDEFINED, wsx, wsy, SDL_WINDOW_OPENGL);
    SDL_GL_CreateContext(wnd);

    struct {
        float l, r;
    } samples[BATCH_SIZE], last_sample = { 0.f };

    glViewport(0, 0, wsx, wsy);
    glClear(GL_COLOR_BUFFER_BIT);

    if (rotate) {
        glMatrixMode(GL_VIEWPORT);
        glRotatef(45.f, 0.f, 0.f, -1.f);
    }

    glLineWidth(3.f);

    glEnable(GL_BLEND);

    uint32_t expected = 0;
    bool quit = false;

    while (!quit) {
        fread(samples, sizeof(samples[0]), BATCH_SIZE, fp);
        if (feof(fp)) {
            break;
        }

        glBlendFunc(GL_ONE, GL_ONE);

        glBegin(GL_LINES);
        set_color(last_sample.l, last_sample.r, samples[0].l, samples[0].r);
        glVertex2f(last_sample.l, last_sample.r);
        glVertex2f(samples[0].l, samples[0].r);

        for (int i = 0; i < BATCH_SIZE - 1; i++) {
            set_color(samples[i].l, samples[i].r, samples[i + 1].l, samples[i + 1].r);
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

        expected += 1000 * BATCH_SIZE / SAMPLE_RATE;
        uint32_t now_ticks = SDL_GetTicks();
        if (now_ticks < expected) {
            SDL_Delay(expected - now_ticks);
        }

        SDL_Event evt;
        while (SDL_PollEvent(&evt)) {
            if (evt.type == SDL_QUIT) {
                quit = true;
            }
        }
    }

    kill(child, SIGKILL);

    return 0;
}
