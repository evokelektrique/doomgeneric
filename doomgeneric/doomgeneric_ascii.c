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
#include <errno.h>

#include "doomgeneric.h"
#include "doomkeys.h"
#include "i_system.h"

#include <ctype.h>
#include <string.h>

#define INPUT_BUFFER_LEN 16u
#define EVENT_BUFFER_LEN ((INPUT_BUFFER_LEN) * 2u - 1u)
#define GRADIENT " .:-=!*#%@&$"

// #define GRADIENT \
//     " .'`^\",:;Il!i><~+_-?][}{1)(|\\/tfjrxnuvczXYUJCLQ0OZmwqpdbkhao*#MW&8%B@$"
#define GRADIENT_LEN (sizeof(GRADIENT) - 1)
#define CLK CLOCK_REALTIME

#define UNLIKELY(x) __builtin_expect((x), 0)
#define CALL(stmt, format)                          \
    do {                                            \
        if (UNLIKELY(stmt)) I_Error(format, errno); \
    } while (0)
#define CALL_STDOUT(stmt, format) CALL((stmt) == EOF, format)

#define BYTE_TO_TEXT(buf, byte)              \
    do {                                     \
        *(buf)++ = '0' + (byte) / 100u;      \
        *(buf)++ = '0' + (byte) / 10u % 10u; \
        *(buf)++ = '0' + (byte) % 10u;       \
    } while (0)
struct color_t {
    uint32_t b : 8;
    uint32_t g : 8;
    uint32_t r : 8;
    uint32_t a : 8;
};

static char *output_buffer;
static size_t output_buffer_size;
static struct timespec ts_init;

static unsigned char input_buffer[INPUT_BUFFER_LEN];
static uint16_t event_buffer[EVENT_BUFFER_LEN];
static uint16_t *event_buf_loc;

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

static unsigned char doomKeyIfTilda(const char **const buf, const unsigned char key)
{
	if (*((*buf) + 1) != '~')
		return '\0';
	(*buf)++;
	return key;
}

static inline unsigned char convertCsiToDoomKey(const char **const buf)
{
	switch (**buf) {
	case 'A':
		return KEY_UPARROW;
	case 'B':
		return KEY_DOWNARROW;
	case 'C':
		return KEY_RIGHTARROW;
	case 'D':
		return KEY_LEFTARROW;
	case 'H':
		return KEY_HOME;
	case 'F':
		return KEY_END;
	case '1':
		switch (*((*buf) + 1)) {
		case '5':
			(*buf)++;
			return doomKeyIfTilda(buf, KEY_F5);
		case '7':
			(*buf)++;
			return doomKeyIfTilda(buf, KEY_F6);
		case '8':
			(*buf)++;
			return doomKeyIfTilda(buf, KEY_F7);
		case '9':
			(*buf)++;
			return doomKeyIfTilda(buf, KEY_F8);
		default:
			return '\0';
		}
	case '2':
		switch (*((*buf) + 1)) {
		case '0':
			(*buf)++;
			return doomKeyIfTilda(buf, KEY_F9);
		case '1':
			(*buf)++;
			return doomKeyIfTilda(buf, KEY_F10);
		case '3':
			(*buf)++;
			return doomKeyIfTilda(buf, KEY_F11);
		case '4':
			(*buf)++;
			return doomKeyIfTilda(buf, KEY_F12);
		case '~':
			(*buf)++;
			return KEY_INS;
		default:
			return '\0';
		}
	case '3':
		return doomKeyIfTilda(buf, KEY_DEL);
	case '5':
		return doomKeyIfTilda(buf, KEY_PGUP);
	case '6':
		return doomKeyIfTilda(buf, KEY_PGDN);
	default:
		return '\0';
	}
}

static inline unsigned char convertSs3ToDoomKey(const char **const buf)
{
	switch (**buf) {
	case 'P':
		return KEY_F1;
	case 'Q':
		return KEY_F2;
	case 'R':
		return KEY_F3;
	case 'S':
		return KEY_F4;
	default:
		return '\0';
	}
}

static inline unsigned char convertToDoomKey(const char **const buf)
{
	switch (**buf) {
	case '\012':
		return KEY_ENTER;
	case '\033':
		switch (*((*buf) + 1)) {
		case '[':
			*buf += 2;
			return convertCsiToDoomKey(buf);
		case 'O':
			*buf += 2;
			return convertSs3ToDoomKey(buf);
		default:
			return KEY_ESCAPE;
		}
	default:
		return tolower(**buf);
	}
}

// Original input reading function.
void DG_ReadInput(void) {
    static unsigned char prev_input_buffer[INPUT_BUFFER_LEN];

    memcpy(prev_input_buffer, input_buffer, INPUT_BUFFER_LEN);
    memset(input_buffer, '\0', INPUT_BUFFER_LEN);
    memset(event_buffer, '\0', 2u * (size_t)EVENT_BUFFER_LEN);
    event_buf_loc = event_buffer;

    static char raw_input_buffer[INPUT_BUFFER_LEN];
    struct termios oldt, newt;

    memset(raw_input_buffer, '\0', INPUT_BUFFER_LEN);

    /* Disable canonical mode */
    CALL(tcgetattr(STDIN_FILENO, &oldt), "DG_DrawFrame: tcgetattr error %d");
    newt = oldt;
    newt.c_lflag &= ~(ICANON);
    newt.c_cc[VMIN] = 0;
    newt.c_cc[VTIME] = 0;
    CALL(tcsetattr(STDIN_FILENO, TCSANOW, &newt),
         "DG_DrawFrame: tcsetattr error %d");

    CALL(read(2, raw_input_buffer, INPUT_BUFFER_LEN - 1u) < 0,
         "DG_DrawFrame: read error %d");

    CALL(tcsetattr(STDIN_FILENO, TCSANOW, &oldt),
         "DG_DrawFrame: tcsetattr error %d");

    /* Flush input buffer to prevent read of previous unread input */
    CALL(tcflush(STDIN_FILENO, TCIFLUSH), "DG_DrawFrame: tcflush error %d");

    /* create input buffer */
    const char *raw_input_buf_loc = raw_input_buffer;
    unsigned char *input_buf_loc = input_buffer;
    while (*raw_input_buf_loc) {
        const unsigned char inp = convertToDoomKey(&raw_input_buf_loc);
        if (!inp) break;
        *input_buf_loc++ = inp;
        raw_input_buf_loc++;
    }
    /* construct event array */
    int i, j;
    for (i = 0; input_buffer[i]; i++) {
        /* skip duplicates */
        for (j = i + 1; input_buffer[j]; j++) {
            if (input_buffer[i] == input_buffer[j]) goto LBL_CONTINUE_1;
        }

        /* pressed events */
        for (j = 0; prev_input_buffer[j]; j++) {
            if (input_buffer[i] == prev_input_buffer[j]) goto LBL_CONTINUE_1;
        }
        *event_buf_loc++ = 0x0100 | input_buffer[i];

    LBL_CONTINUE_1:;
    }

    /* depressed events */
    for (i = 0; prev_input_buffer[i]; i++) {
        for (j = 0; input_buffer[j]; j++) {
            if (prev_input_buffer[i] == input_buffer[j]) goto LBL_CONTINUE_2;
        }
        *event_buf_loc++ = 0xFF & prev_input_buffer[i];

    LBL_CONTINUE_2:;
    }

    event_buf_loc = event_buffer;
}

int DG_GetKey(int *const pressed, unsigned char *const doomKey) {
    if (!*event_buf_loc) return 0;

    *pressed = *event_buf_loc >> 8;
    *doomKey = *event_buf_loc & 0xFF;
    event_buf_loc++;
    return 1;
}

// Terminal cleanup: disable alternate screen and restore terminal settings.
void DG_AtExit(void) {
    // Disable alternate screen buffer
    printf("\033[?1049l");

    tcsetattr(STDIN_FILENO, TCSANOW, &oldt);
    printf("\033[0m\033[?25h\n");  // Reset colors and show cursor
}

void DG_Init() {
    struct termios t;
    CALL(tcgetattr(STDIN_FILENO, &t), "DG_Init: tcgetattr error %d");
    t.c_lflag &= ~(ECHO);
    CALL(tcsetattr(STDIN_FILENO, TCSANOW, &t), "DG_Init: tcsetattr error %d");
    CALL(atexit(&DG_AtExit), "DG_Init: atexit error %d");

    /* Longest SGR code: \033[38;2;RRR;GGG;BBBm (length 19)
     * Maximum 21 bytes per pixel: SGR + 2 x char
     * 1 Newline character per line
     * SGR clear code: \033[0m (length 4)
     */
    output_buffer_size =
        21u * DOOMGENERIC_RESX * DOOMGENERIC_RESY + DOOMGENERIC_RESY + 4u;
    output_buffer = malloc(output_buffer_size);

    clock_gettime(CLK, &ts_init);

    memset(input_buffer, '\0', INPUT_BUFFER_LEN);
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

// Set terminal title
void DG_SetWindowTitle(const char *title) {
    printf("\033]2;%s\007", title);
    fflush(stdout);
}
