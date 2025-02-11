#include <stdio.h>
#include "doomgeneric.h"
#include "m_argv.h"
#include "d_main.h"

// Function prototypes (as declared in your headers)
void dg_Create(void);
void DG_ReadInput(void);
void doomgeneric_Tick(void);
void DG_DrawFrame(void);
void DG_SleepMs(uint32_t ms);

void M_FindResponseFile(void);

int main(int argc, char **argv)
{
    myargc = argc;
    myargv = argv;

    M_FindResponseFile();

    printf("Starting DoomGeneric ASCII Renderer...\n");
    dg_Create();  // Initializes Doom Generic (calls DG_Init, etc.)

    D_DoomMain();

    // Main loop: poll input, update game logic, draw frame, then sleep (~60 FPS)
    while (1)
    {
        DG_ReadInput();     // Read key presses (nonblocking)
        doomgeneric_Tick(); // Update game logic
        DG_DrawFrame();     // Render the ASCII output (with colors)
        DG_SleepMs(16);     // Limit frame rate (about 60 FPS)
    }

    return 0;
}
