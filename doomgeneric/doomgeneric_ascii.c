//
// DoomGeneric ASCII Renderer (Linux Only)
//
// Copyright(C) 2022-2024 Wojciech Graj
// Licensed under GPL v2 or later
//
#include <fcntl.h>
#include <pthread.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/select.h>
#include <termios.h>
#include <time.h>
#include <unistd.h>

#include "doomgeneric.h"
#include "doomkeys.h"
#include "i_system.h"

#define GRADIENT \
    " .'`^\",:;Il!i><~+_-?][}{1)(|\\/tfjrxnuvczXYUJCLQ0OZmwqpdbkhao*#MW&8%B@$"
#define GRADIENT_LEN (sizeof(GRADIENT) - 1)

struct color_t {
    uint32_t b : 8;
    uint32_t g : 8;
    uint32_t r : 8;
    uint32_t a : 8;
};

static struct termios oldt;
static struct timespec ts_init;
static char *output_buffer;
static size_t output_buffer_size;

#define WIDTH DOOMGENERIC_RESX
#define HEIGHT DOOMGENERIC_RESY

// Convert pixel to ASCII
char pixel_to_ascii(uint32_t pixel) {
    uint8_t r = (pixel >> 16) & 0xFF;
    uint8_t g = (pixel >> 8) & 0xFF;
    uint8_t b = pixel & 0xFF;

    uint8_t brightness = (r + g + b) / 3;
    return GRADIENT[(brightness * (GRADIENT_LEN - 1)) / 255];
}

void render_ascii(uint32_t *buffer) {
    printf("\033[H");  // Move cursor to top-left
    char *buf = output_buffer;

    for (int y = 0; y < HEIGHT; y += 2) {
        for (int x = 0; x < WIDTH; x++) {
            int index = y * WIDTH + x;

            if (index >= WIDTH * HEIGHT) {
                printf("ERROR: Buffer overflow at index %d\n", index);
                return;
            }

            *buf++ = pixel_to_ascii(buffer[index]);
        }
        *buf++ = '\n';
    }
    *buf = '\0';

    fputs(output_buffer, stdout);
    fflush(stdout);
}

// Original input reading function.
void DG_ReadInput() {
    unsigned char input_buffer[16];
    struct timeval timeout = {0, 0};  // Zero timeout: non-blocking
    fd_set read_fds;

    FD_ZERO(&read_fds);
    FD_SET(STDIN_FILENO, &read_fds);

    // Check if input is available.
    if (select(STDIN_FILENO + 1, &read_fds, NULL, NULL, &timeout) > 0) {
        int bytes_read =
            read(STDIN_FILENO, input_buffer, sizeof(input_buffer) - 1);
        if (bytes_read > 0) {
            for (int i = 0; i < bytes_read; i++) {
                char c = input_buffer[i];

                // If an escape character is found, check if enough bytes are
                // available.
                if (c == '\033') {
                    if (i + 2 < bytes_read && input_buffer[i + 1] == '[') {
                        char arrow = input_buffer[i + 2];
                        i += 2;  // Consume the escape sequence.
                        switch (arrow) {
                            case 'A':
                                // printf("ARROW UP PRESSED\n");
                                break;
                            case 'B':
                                // printf("ARROW DOWN PRESSED\n");
                                break;
                            case 'C':
                                // printf("ARROW RIGHT PRESSED\n");
                                break;
                            case 'D':
                                // printf("ARROW LEFT PRESSED\n");
                                break;
                            default:
                                // printf("UNKNOWN ESC SEQUENCE: \\033[%c\n",
                                // arrow);
                                break;
                        }
                    } else {
                        // printf("ESC PRESSED\n");
                    }
                } else {
                    // Process regular keys.
                    switch (c) {
                        case 'w':
                        case 'W':
                            // printf("UP PRESSED\n");
                            break;
                        case 's':
                        case 'S':
                            // printf("DOWN PRESSED\n");
                            break;
                        case 'a':
                        case 'A':
                            // printf("LEFT PRESSED\n");
                            break;
                        case 'd':
                        case 'D':
                            // printf("RIGHT PRESSED\n");
                            break;
                        case ' ':
                            // printf("SHOOT (SPACE) PRESSED\n");
                            break;
                        case 'q':
                            // printf("QUIT GAME\n");
                            exit(0);
                        default:
                            // Handle additional keys if needed.
                            break;
                    }
                }
            }
        }
    }
}

// Input thread function: continuously poll input.
void *DG_InputThread(void *arg) {
    (void)arg;  // Unused parameter
    while (1) {
        DG_ReadInput();
        // Sleep a short time to prevent busy-waiting.
        usleep(1000);  // 1ms sleep
    }
    return NULL;
}

// Terminal cleanup: disable alternate screen and restore terminal settings.
void DG_AtExit(void) {
    // Disable alternate screen buffer
    printf("\033[?1049l");

    tcsetattr(STDIN_FILENO, TCSANOW, &oldt);
    printf("\033[0m\033[?25h\n");  // Reset colors and show cursor
}

// Terminal initialization: set non-canonical mode, allocate buffer, etc.
void DG_Init() {
    struct termios t;

    // Save original terminal settings
    tcgetattr(STDIN_FILENO, &oldt);
    t = oldt;
    t.c_lflag &= ~(ECHO | ICANON);
    // Set VMIN and VTIME so that read() returns immediately.
    t.c_cc[VMIN] = 0;
    t.c_cc[VTIME] = 0;
    tcsetattr(STDIN_FILENO, TCSANOW, &t);

    // Set file descriptor to non-blocking mode.
    fcntl(STDIN_FILENO, F_SETFL, O_NONBLOCK);

    // Register exit cleanup function.
    atexit(DG_AtExit);

    // Allocate output buffer.
    output_buffer_size = (WIDTH + 1) * (HEIGHT / 2) + 1;
    output_buffer = malloc(output_buffer_size);

    // Initialize time for tick calculations.
    clock_gettime(CLOCK_MONOTONIC, &ts_init);

    memset(output_buffer, 0, output_buffer_size);

    // Hide cursor and enable alternate screen buffer to reduce flickering.
    printf("\033[?25l");
    printf("\033[?1049h");

    // --- Create the input thread ---
    pthread_t input_tid;
    if (pthread_create(&input_tid, NULL, DG_InputThread, NULL) != 0) {
        perror("pthread_create");
        exit(EXIT_FAILURE);
    }
    // Optionally, detach the thread so it cleans up automatically:
    pthread_detach(input_tid);
}

void DG_DrawFrame() {
    printf("\033[H");  // Move cursor to top-left without clearing
    render_ascii(DG_ScreenBuffer);
}

void DG_SleepMs(uint32_t ms) {
    struct timespec ts = {.tv_sec = ms / 1000,
                          .tv_nsec = (ms % 1000) * 1000000};
    nanosleep(&ts, NULL);
}

uint32_t DG_GetTicksMs() {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (ts.tv_sec - ts_init.tv_sec) * 1000 +
           (ts.tv_nsec - ts_init.tv_nsec) / 1000000;
}

// Read input (Non-blocking)
// (This function remains for compatibility, though in threaded mode you may not
// need it.)
int DG_GetKey(int *pressed, unsigned char *doomKey) {
    static char input_buffer[16];
    static int input_len = 0, index = 0;

    // Read new input only if old buffer is empty.
    if (index >= input_len) {
        memset(input_buffer, 0, sizeof(input_buffer));
        input_len = read(STDIN_FILENO, input_buffer, sizeof(input_buffer) - 1);
        index = 0;
        if (input_len <= 0) return 0;
    }

    char c = input_buffer[index++];
    *pressed = 1;

    switch (c) {
        case '\033':  // Escape sequences.
            if (index < input_len && input_buffer[index] == '[') {
                index++;
                switch (input_buffer[index++]) {
                    case 'A':
                        return *doomKey = KEY_UPARROW, 1;
                    case 'B':
                        return *doomKey = KEY_DOWNARROW, 1;
                    case 'C':
                        return *doomKey = KEY_RIGHTARROW, 1;
                    case 'D':
                        return *doomKey = KEY_LEFTARROW, 1;
                    case 'H':
                        return *doomKey = KEY_HOME, 1;
                    case 'F':
                        return *doomKey = KEY_END, 1;
                }
            }
            return *doomKey = KEY_ESCAPE, 1;
        case '\n':
            return *doomKey = KEY_ENTER, 1;
        case '\t':
            return *doomKey = KEY_TAB, 1;
        case 127:
            return *doomKey = KEY_BACKSPACE, 1;
        default:
            return *doomKey = c, 1;
    }
}

// Set terminal title
void DG_SetWindowTitle(const char *title) {
    printf("\033]2;%s\007", title);
    fflush(stdout);
}
