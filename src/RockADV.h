#pragma once

// Initializes RockADV variables, allocates canvas, and sets Landscape mode
void setupRockADV();

// Runs the RockADV game loop. 
// Returns true to keep running, false to exit to CasualADV Main Menu
bool loopRockADV();