#pragma once

// Initializes FallTris variables, allocates canvas, and sets Portrait mode
void setupFallTris();

// Runs the FallTris game loop. 
// Returns true to keep running, false to exit to CasualADV (restoring Landscape mode)
bool loopFallTris();