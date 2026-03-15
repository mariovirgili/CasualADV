// All comments in the code must always be in English.
#include <M5Cardputer.h>
#include <SPI.h>
#include <SD.h>
#include <FS.h>
#include <ctype.h> 

#include "RockADV.h"
#include "rockadv_logo.h" 
#include "splash.h"       

namespace RA {

    // --- Game Constants ---
    const int MAP_WIDTH = 40;
    const int MAP_HEIGHT = 20;
    const int TILE_SIZE = 20;
    const int SCREEN_TILES_X = 12;
    const int SCREEN_TILES_Y = 6;
    const int MAX_ENEMIES = 15;

    const int PHYSICS_INTERVAL = 150;  
    const int MOVEMENT_INTERVAL = 150; 
    const int ENEMY_INTERVAL = 300;    
    const int ANIM_SPEED = 200;        

    #define COLOR_UI_BAR     0x3186 
    #define COLOR_UI_BORDER  0x6B4D 

    enum Tile { EMPTY = 0, DIRT, WALL, ROCK, DIAMOND, PLAYER, EXIT, ENEMY };

    // --- Sprite Objects ---
    M5Canvas* sprWall = nullptr;
    M5Canvas* sprDirt = nullptr;
    M5Canvas* sprRock = nullptr;
    M5Canvas* sprDiamond = nullptr;
    M5Canvas* sprExitClosed = nullptr;
    M5Canvas* sprExitOpen = nullptr;
    M5Canvas* sprEnemy = nullptr;
    M5Canvas* sprMouse[4][2]; 

    // --- Game State ---
    int currentLevel = 1;
    int targetDiamonds = 10;
    int currentMapWidth = 20;
    int currentMapHeight = 12;
    int playerX = 1, playerY = 1;
    int playerDir = 2; 
    int animFrame = 0; 
    int cameraX = 0, cameraY = 0;
    int diamondsCollected = 0;
    int timeLeft = 150;
    int totalScore = 0;

    // --- High Score Entry State Variables ---
    char newName[4] = "A__";
    int charIndex = 0;
    int tempScore = 0;

    // --- Custom Keys ---
    char keyUp = 'e'; char keyDn = 'z'; char keyL = 'a'; char keyR = 'd';
    int remapStep = 0;
    char tempKeys[4];
    const char* remapNames[] = {"UP", "DOWN", "LEFT", "RIGHT"};
    bool remapNeedsRedraw = true; 
    bool menuNeedsRedraw = true;

    struct HighScoreEntry { char name[4]; int score; };
    HighScoreEntry highScores[5];

    struct Enemy { int x, y, dir; bool alive; };
    Enemy enemies[MAX_ENEMIES];
    int enemyCount = 0;

    unsigned long lastTick = 0;
    unsigned long lastPhysicsTick = 0;
    unsigned long lastMoveTick = 0;
    unsigned long lastEnemyTick = 0;
    unsigned long lastAnimTick = 0;
    unsigned long volumeResetTick = 0;
    unsigned long lastToggleTime = 0;
    unsigned long doorOpenMsgTimer = 0;
    int doorMsgState = 0; 

    bool gameOver = false; bool gameWon = false; bool sdAvailable = false; 
    bool needsRedraw = true; bool hsNeedsRedraw = true; 

    uint8_t gameMap[MAP_HEIGHT][MAP_WIDTH];

    enum RAState { SPLASH, LEADERBOARD, PLAYING, HIGHSCORE_INPUT, REMAP_INPUT };
    RAState currentState = SPLASH;

    // --- Forward Declarations ---
    void initLevel();
    void initMap();
    void drawMap();
    void updateHUD();
    void showSplash();
    void resetGameSession();
    void generateSprites();
    void freeSprites();
    void checkAndSaveHighScore(int score);
    void handleHighScoreInput();
    void handleRemapInput();
    void startHighScoreEntry(int newScore);
    void transitionToSplash();
    bool updatePhysics();
    bool updateEnemies();
    void movePlayer(int dx, int dy);
    void drawLogoScreen();
    void drawLeaderboardScreen();

    // --- Persistence ---
    void loadKeys() {
        if (!sdAvailable) return;
        File f = SD.open("/CasualADV/rockadv.ini", FILE_READ);
        if (f) {
            String s = f.readStringUntil('\n');
            if (s.length() >= 4) { keyUp = s[0]; keyDn = s[1]; keyL = s[2]; keyR = s[3]; }
            f.close();
        }
    }

    void saveKeys() {
        if (!sdAvailable) return;
        if (!SD.exists("/CasualADV")) SD.mkdir("/CasualADV");
        File f = SD.open("/CasualADV/rockadv.ini", FILE_WRITE);
        if (f) { f.printf("%c%c%c%c\n", keyUp, keyDn, keyL, keyR); f.close(); }
    }

    void initSD() {
        for (int i = 0; i < 5; i++) { strcpy(highScores[i].name, "---"); highScores[i].score = 0; }
        if (SD.cardType() != CARD_NONE) { 
            sdAvailable = true;
            if (!SD.exists("/CasualADV")) SD.mkdir("/CasualADV");
            loadKeys(); 
            File file = SD.open("/CasualADV/rockadv.high", FILE_READ);
            if (file) {
                for (int i = 0; i < 5; i++) {
                    if (file.available()) {
                        String line = file.readStringUntil('\n');
                        int commaIdx = line.indexOf(',');
                        if (commaIdx > 0) {
                            String n = line.substring(0, commaIdx); n.trim();
                            n.toCharArray(highScores[i].name, 4);
                            highScores[i].score = line.substring(commaIdx + 1).toInt();
                        }
                    }
                }
                file.close();
            }
        } else { sdAvailable = false; }
    }

    void saveScoreToSD() {
        if (!sdAvailable) return;
        File file = SD.open("/CasualADV/rockadv.high", FILE_WRITE);
        if (file) { for (int i = 0; i < 5; i++) file.printf("%s,%d\n", highScores[i].name, highScores[i].score); file.close(); }
    }

    // --- Core UI Logic ---
    void drawLogoScreen() {
        M5Cardputer.Display.fillScreen(TFT_BLACK);
        int imgW = 180; int imgH = 70;
        int imgX = (240 - imgW) / 2; int imgY = 15;
        M5Cardputer.Display.setSwapBytes(true);
        M5Cardputer.Display.pushImage(imgX, imgY, imgW, imgH, rockadv_logo);
        M5Cardputer.Display.setSwapBytes(false);
        M5Cardputer.Display.setTextColor(TFT_WHITE);
        M5Cardputer.Display.setTextSize(1);
        M5Cardputer.Display.drawCenterString("B:Play | ESC:Remap | V:Quit", 120, 100);
    }

    void drawLeaderboardScreen() {
        M5Cardputer.Display.fillScreen(TFT_BLACK);
        int imgW = 180; int imgH = 70;
        int imgX = (240 - imgW) / 2; int imgY = 2; 
        M5Cardputer.Display.setSwapBytes(true);
        M5Cardputer.Display.pushImage(imgX, imgY, imgW, imgH, highscores_logo);
        M5Cardputer.Display.setSwapBytes(false); 
        M5Cardputer.Display.fillRoundRect(30, 75, 180, 56, 6, COLOR_UI_BAR);
        M5Cardputer.Display.drawRoundRect(30, 75, 180, 56, 6, COLOR_UI_BORDER);
        M5Cardputer.Display.setTextColor(TFT_WHITE);
        M5Cardputer.Display.setTextSize(1);
        for (int i = 0; i < 5; i++) {
            char hsStr[30];
            sprintf(hsStr, "%d. %s : %06d", i + 1, highScores[i].name, highScores[i].score);
            M5Cardputer.Display.drawString(hsStr, 60, 80 + (i * 10)); 
        }
    }

    void showSplash() {
        M5Cardputer.Display.fillScreen(TFT_BLACK);
        menuNeedsRedraw = true;
        currentState = SPLASH;
        lastToggleTime = millis();
        while (M5Cardputer.Keyboard.isPressed()) { M5Cardputer.update(); delay(10); }
    }

    void transitionToSplash() {
        M5Cardputer.Display.fillScreen(TFT_BLACK);
        delay(150); 
        showSplash();
    }

    void startHighScoreEntry(int newScore) {
        tempScore = newScore;
        strcpy(newName, "A__");
        charIndex = 0;
        currentState = HIGHSCORE_INPUT;
        hsNeedsRedraw = true; 
        while (M5Cardputer.Keyboard.isPressed()) { M5Cardputer.update(); delay(10); }
    }

    void handleHighScoreInput() {
        if (hsNeedsRedraw) {
            M5Cardputer.Display.fillScreen(TFT_BLACK);
            M5Cardputer.Display.setTextColor(TFT_CYAN);
            M5Cardputer.Display.setTextSize(2);
            M5Cardputer.Display.setCursor(20, 30);
            M5Cardputer.Display.print("NEW HIGH SCORE!");
            M5Cardputer.Display.setTextColor(TFT_WHITE);
            M5Cardputer.Display.setTextSize(1);
            M5Cardputer.Display.setCursor(75, 60);
            M5Cardputer.Display.printf("SCORE: %06d", tempScore);
            M5Cardputer.Display.setTextSize(3);
            for (int i = 0; i < 3; i++) {
                if (i == charIndex) M5Cardputer.Display.setTextColor(TFT_GREEN); 
                else M5Cardputer.Display.setTextColor(TFT_YELLOW); 
                M5Cardputer.Display.setCursor(85 + (i * 24), 90);
                M5Cardputer.Display.printf("%c", newName[i]);
            }
            M5Cardputer.Display.setTextColor(TFT_GREEN);
            M5Cardputer.Display.setTextSize(1);
            M5Cardputer.Display.setCursor(15, 125);
            M5Cardputer.Display.print(String((char)toupper(keyUp)) + "/" + String((char)toupper(keyDn)) + ":Letter " + String((char)toupper(keyL)) + "/" + String((char)toupper(keyR)) + ":Move B:Save");
            hsNeedsRedraw = false; 
        }
        if (M5Cardputer.Keyboard.isChange() && M5Cardputer.Keyboard.isPressed()) {
            Keyboard_Class::KeysState s = M5Cardputer.Keyboard.keysState();
            for (auto c : s.word) {
                char lowerC = tolower(c);
                if (lowerC == keyUp) {
                    if (newName[charIndex] == '_' || newName[charIndex] == 'Z') newName[charIndex] = 'A';
                    else newName[charIndex]++;
                    hsNeedsRedraw = true;
                }
                else if (lowerC == keyDn) {
                    if (newName[charIndex] == '_' || newName[charIndex] == 'A') newName[charIndex] = 'Z';
                    else newName[charIndex]--;
                    hsNeedsRedraw = true;
                }
                else if (lowerC == keyL && charIndex > 0) { charIndex--; hsNeedsRedraw = true; }
                else if (lowerC == keyR && charIndex < 2) {
                    charIndex++; if (newName[charIndex] == '_') newName[charIndex] = 'A'; 
                    hsNeedsRedraw = true;
                }
                else if (lowerC == 'b') {
                    for (int i = 0; i < 3; i++) if (newName[i] == '_') newName[i] = ' ';
                    for (int i = 0; i < 5; i++) {
                        if (tempScore > highScores[i].score) {
                            for (int j = 4; j > i; j--) highScores[j] = highScores[j - 1];
                            strcpy(highScores[i].name, newName); highScores[i].score = tempScore; break;
                        }
                    }
                    saveScoreToSD(); M5Cardputer.Speaker.tone(1000, 200); delay(300); transitionToSplash(); 
                }
            }
        }
    }

    void handleRemapInput() {
        if (remapNeedsRedraw) {
            M5Cardputer.Display.fillScreen(TFT_BLACK);
            M5Cardputer.Display.setTextColor(TFT_CYAN);
            M5Cardputer.Display.setTextSize(2);
            M5Cardputer.Display.setCursor(20, 20);
            M5Cardputer.Display.print("REMAP CONTROLS");
            M5Cardputer.Display.setTextColor(TFT_WHITE);
            M5Cardputer.Display.setTextSize(1);
            M5Cardputer.Display.setCursor(30, 60);
            M5Cardputer.Display.print("Press key for:");
            M5Cardputer.Display.setTextColor(TFT_YELLOW);
            M5Cardputer.Display.setTextSize(2);
            M5Cardputer.Display.setCursor(30, 80);
            M5Cardputer.Display.print(remapNames[remapStep]);
            M5Cardputer.Display.setTextColor(TFT_DARKGRAY);
            M5Cardputer.Display.setCursor(20, 120);
            M5Cardputer.Display.print("(Keys V and B are reserved)");
            remapNeedsRedraw = false;
        }
        if (M5Cardputer.Keyboard.isChange() && M5Cardputer.Keyboard.isPressed()) {
            Keyboard_Class::KeysState s = M5Cardputer.Keyboard.keysState();
            for (auto c : s.word) {
                char lowerC = tolower(c);
                if (lowerC == 'v' || lowerC == 'b') { M5Cardputer.Speaker.tone(200, 100); return; }
                tempKeys[remapStep] = lowerC;
                M5Cardputer.Speaker.tone(1000, 50);
                remapStep++;
                remapNeedsRedraw = true;
                if (remapStep >= 4) {
                    keyUp = tempKeys[0]; keyDn = tempKeys[1]; keyL = tempKeys[2]; keyR = tempKeys[3];
                    saveKeys(); transitionToSplash();
                }
                delay(200); 
            }
        }
    }

    void checkAndSaveHighScore(int score) {
        if (score > highScores[4].score) {
            if (sdAvailable) startHighScoreEntry(score);
            else {
                M5Cardputer.Display.fillScreen(TFT_BLACK);
                M5Cardputer.Display.setTextColor(TFT_RED);
                M5Cardputer.Display.setTextSize(2);
                M5Cardputer.Display.setCursor(35, 50);
                M5Cardputer.Display.print("SD NOT PRESENT");
                M5Cardputer.Display.setTextSize(1);
                M5Cardputer.Display.setTextColor(TFT_WHITE);
                M5Cardputer.Display.setCursor(60, 80);
                M5Cardputer.Display.printf("Score: %06d", score);
                M5Cardputer.Speaker.tone(150, 600);
                delay(2500); transitionToSplash(); 
            }
        } else { transitionToSplash(); }
    }

    // --- Helper Math/Graphics ---
    void drawDitherRect(M5Canvas* spr, int x, int y, int w, int h, uint16_t color1, uint16_t color2) {
        for (int i = 0; i < w; i++) {
            for (int j = 0; j < h; j++) {
                if ((i + j) % 2 == 0) spr->drawPixel(x + i, y + j, color1);
                else spr->drawPixel(x + i, y + j, color2);
            }
        }
    }

    void drawDitherCircle(M5Canvas* spr, int cx, int cy, int r, uint16_t color1, uint16_t color2) {
        for (int y = -r; y <= r; y++) {
            for (int x = -r; x <= r; x++) {
                if (x * x + y * y <= r * r) {
                    if ((x + y + cx + cy) % 2 == 0) spr->drawPixel(cx + x, cy + y, color1);
                    else spr->drawPixel(cx + x, cy + y, color2);
                }
            }
        }
    }

    // --- Sprite Generation ---
    void generateSprites() {
        if (sprWall != nullptr) return; 
        uint16_t colDirt = 0x4A69; uint16_t colDirtLight = 0x73AE; 
        uint16_t colWall = 0x52AA; uint16_t colWallDark = 0x2945; 
        uint16_t colRock = 0x7BEF; uint16_t colRockDark = 0x4228; 
        uint16_t colMouse = 0x9CD3; uint16_t colEar = 0xFACB;
        uint16_t diaBorder = TFT_BLUE; uint16_t diaTopLeft = 0x11DF; 
        uint16_t diaTopRight = TFT_WHITE; uint16_t diaBotLeft = 0x5D3F; 
        uint16_t diaBotRight = TFT_WHITE;

        sprWall = new M5Canvas(&M5Cardputer.Display);
        sprDirt = new M5Canvas(&M5Cardputer.Display);
        sprRock = new M5Canvas(&M5Cardputer.Display);
        sprDiamond = new M5Canvas(&M5Cardputer.Display);
        sprExitClosed = new M5Canvas(&M5Cardputer.Display);
        sprExitOpen = new M5Canvas(&M5Cardputer.Display);
        sprEnemy = new M5Canvas(&M5Cardputer.Display);
        
        sprWall->createSprite(TILE_SIZE, TILE_SIZE);
        sprDirt->createSprite(TILE_SIZE, TILE_SIZE);
        sprRock->createSprite(TILE_SIZE, TILE_SIZE);
        sprDiamond->createSprite(TILE_SIZE, TILE_SIZE);
        sprExitClosed->createSprite(TILE_SIZE, TILE_SIZE);
        sprExitOpen->createSprite(TILE_SIZE, TILE_SIZE);
        sprEnemy->createSprite(TILE_SIZE, TILE_SIZE);

        drawDitherRect(sprWall, 0, 0, TILE_SIZE, TILE_SIZE, colWall, colWallDark);
        sprWall->drawRect(0, 0, 20, 10, TFT_BLACK); 
        sprWall->drawRect(0, 10, 20, 10, TFT_BLACK);
        sprWall->drawLine(10, 0, 10, 9, TFT_BLACK); 
        sprWall->drawLine(5, 10, 5, 19, TFT_BLACK); 
        sprWall->drawLine(15, 10, 15, 19, TFT_BLACK);

        drawDitherRect(sprDirt, 0, 0, TILE_SIZE, TILE_SIZE, colDirt, colDirtLight);
        for(int i=0; i<8; i++) sprDirt->fillRect(random(18), random(18), 2, 2, 0x2945); 

        sprRock->fillSprite(TFT_BLACK); 
        sprRock->fillCircle(10, 10, 9, colRock);
        drawDitherCircle(sprRock, 12, 12, 6, colRock, colRockDark);
        sprRock->fillCircle(14, 14, 4, colRockDark); 
        sprRock->fillCircle(6, 6, 2, TFT_WHITE); 

        sprDiamond->fillSprite(TFT_BLACK);
        sprDiamond->fillTriangle(2, 9, 10, 3, 10, 9, diaTopLeft);
        sprDiamond->fillTriangle(10, 3, 18, 9, 10, 9, diaTopRight);
        sprDiamond->fillTriangle(2, 9, 10, 17, 10, 9, diaBotLeft);
        sprDiamond->fillTriangle(10, 9, 18, 9, 10, 17, diaBotRight);
        sprDiamond->drawLine(2, 9, 10, 3, diaBorder);
        sprDiamond->drawLine(10, 3, 18, 9, diaBorder);
        sprDiamond->drawLine(2, 9, 10, 17, diaBorder);
        sprDiamond->drawLine(18, 9, 10, 17, diaBorder);
        sprDiamond->drawLine(2, 9, 18, 9, diaBorder);  
        sprDiamond->drawLine(10, 3, 10, 17, diaBorder); 

        sprExitClosed->fillSprite(TFT_DARKGRAY);
        sprExitClosed->drawRect(0, 0, 20, 20, TFT_BLACK);
        sprExitClosed->drawLine(10, 0, 10, 20, TFT_BLACK);
        sprExitClosed->fillCircle(7, 10, 2, TFT_BLACK);
        sprExitClosed->fillCircle(13, 10, 2, TFT_BLACK);

        sprExitOpen->fillSprite(TFT_BLACK);
        sprExitOpen->fillRect(2, 2, 16, 16, TFT_WHITE);
        drawDitherRect(sprExitOpen, 5, 5, 10, 10, TFT_CYAN, TFT_BLUE);

        sprEnemy->fillSprite(TFT_BLACK);
        sprEnemy->fillCircle(10, 10, 8, TFT_RED);
        drawDitherCircle(sprEnemy, 10, 10, 5, TFT_RED, TFT_ORANGE);
        sprEnemy->fillCircle(10, 10, 2, TFT_YELLOW);

        for(int d=0; d<4; d++) { 
            for(int f=0; f<2; f++) { 
                sprMouse[d][f] = new M5Canvas(&M5Cardputer.Display);
                sprMouse[d][f]->createSprite(TILE_SIZE, TILE_SIZE);
                M5Canvas* s = sprMouse[d][f];
                s->fillSprite(TFT_BLACK);
                s->fillCircle(10, 10, 7, colMouse);
                if (d==0 || d==2) { s->fillCircle(5, 5, 4, colEar); s->fillCircle(15, 5, 4, colEar); } 
                else { s->fillCircle(10, 5, 4, colEar); }
                int footOffset = (f==0) ? -2 : 2;
                if (d==0) { s->fillCircle(10, 8, 2, TFT_DARKGRAY); } 
                else if (d==2) { 
                     s->fillCircle(8, 9, 1, TFT_BLACK); s->fillCircle(12, 9, 1, TFT_BLACK); 
                     s->fillCircle(10, 12, 2, TFT_BLACK); 
                     s->fillRect(7, 17, 2, 2+footOffset, colMouse); s->fillRect(11, 17, 2, 2-footOffset, colMouse); 
                } else if (d==1) { 
                    s->fillCircle(14, 9, 1, TFT_BLACK); s->fillCircle(16, 11, 2, TFT_BLACK); 
                    s->fillRect(10+footOffset, 17, 3, 2, colMouse);
                } else { 
                    s->fillCircle(6, 9, 1, TFT_BLACK); s->fillCircle(4, 11, 2, TFT_BLACK);
                    s->fillRect(7+footOffset, 17, 3, 2, colMouse);
                }
            }
        }
    }

    void freeSprites() {
        if (sprWall != nullptr) {
            sprWall->deleteSprite(); delete sprWall; sprWall = nullptr;
            sprDirt->deleteSprite(); delete sprDirt; sprDirt = nullptr;
            sprRock->deleteSprite(); delete sprRock; sprRock = nullptr;
            sprDiamond->deleteSprite(); delete sprDiamond; sprDiamond = nullptr;
            sprExitClosed->deleteSprite(); delete sprExitClosed; sprExitClosed = nullptr;
            sprExitOpen->deleteSprite(); delete sprExitOpen; sprExitOpen = nullptr;
            sprEnemy->deleteSprite(); delete sprEnemy; sprEnemy = nullptr;
            for(int d=0; d<4; d++) for(int f=0; f<2; f++) { sprMouse[d][f]->deleteSprite(); delete sprMouse[d][f]; sprMouse[d][f] = nullptr; }
        }
    }

    // --- Main Game Logic ---
    void initMap() {
        int baseRockProb = min(12 + (currentLevel * 2), 35);
        int diamondProb = 18;
        int enemyProb = min(1 + currentLevel, 5); 
        enemyCount = 0;
        for(int y = 0; y < MAP_HEIGHT; y++) for(int x = 0; x < MAP_WIDTH; x++) gameMap[y][x] = WALL;
        for(int y = 0; y < currentMapHeight; y++) {
            for(int x = 0; x < currentMapWidth; x++) {
                if (x == 0 || x == currentMapWidth - 1 || y == 0 || y == currentMapHeight - 1) gameMap[y][x] = WALL;
                else if (x == 1 && y == 1) { gameMap[y][x] = PLAYER; playerX = 1; playerY = 1; }
                else if (x == currentMapWidth - 2 && y == currentMapHeight - 2) gameMap[y][x] = EXIT;
                else {
                    int wallProb = 5; if (x > 1 && gameMap[y][x-1] == WALL) wallProb = 45; 
                    int r = random(100);
                    if (r < wallProb) gameMap[y][x] = WALL;
                    else {
                        int r2 = random(100);
                        if (r2 < baseRockProb) gameMap[y][x] = ROCK;
                        else if (r2 < baseRockProb + diamondProb) gameMap[y][x] = DIAMOND;
                        else if (r2 < baseRockProb + diamondProb + enemyProb && enemyCount < MAX_ENEMIES && (x > 15 || y > 8)) {
                            gameMap[y][x] = ENEMY; enemies[enemyCount] = {x, y, (int)random(4), true}; enemyCount++;
                        } else if (r2 < 85) gameMap[y][x] = DIRT;
                        else gameMap[y][x] = EMPTY;
                    }
                }
            }
        }
        for(int y = 1; y <= 3; y++) for(int x = 1; x <= 3; x++) {
            if(gameMap[y][x] == ENEMY) for(int i=0; i<enemyCount; i++) if(enemies[i].x==x && enemies[i].y==y) enemies[i].alive=false;
            if(gameMap[y][x] != WALL && gameMap[y][x] != PLAYER) gameMap[y][x] = EMPTY;
        }
        gameMap[1][2] = EMPTY; gameMap[2][1] = EMPTY; gameMap[2][2] = EMPTY;
        playerDir = 2; animFrame = 0; 
    }

    void showLevelIntro() {
        M5Cardputer.Display.fillScreen(TFT_BLACK);
        M5Cardputer.Display.setTextColor(TFT_WHITE);
        M5Cardputer.Display.setTextSize(3);
        M5Cardputer.Display.setCursor(50, 40);
        M5Cardputer.Display.printf("LEVEL %d", currentLevel);
        M5Cardputer.Display.setTextSize(1);
        M5Cardputer.Display.setTextColor(TFT_CYAN);
        M5Cardputer.Display.setCursor(50, 90);
        M5Cardputer.Display.printf("Target: %d Diamonds", targetDiamonds);
        M5Cardputer.Speaker.tone(600, 150); delay(200);
        M5Cardputer.Speaker.tone(800, 300); delay(2000); 
        M5Cardputer.Display.fillScreen(TFT_BLACK);
    }

    void initLevel() {
        diamondsCollected = 0;
        currentMapWidth = min(MAP_WIDTH, 16 + (currentLevel * 4));
        currentMapHeight = min(MAP_HEIGHT, 10 + (currentLevel * 2));
        timeLeft = max(150 - ((currentLevel - 1) * 10), 60); 
        targetDiamonds = min(10 + ((currentLevel - 1) * 2), 30); 
        showLevelIntro(); initMap();
        gameWon = false; gameOver = false; needsRedraw = true;
        doorOpenMsgTimer = 0; doorMsgState = 0;
        lastTick = millis(); lastPhysicsTick = millis(); lastEnemyTick = millis(); lastAnimTick = millis();
        updateHUD(); drawMap();
    }
    
    void resetGameSession() { currentLevel = 1; totalScore = 0; initLevel(); }

    void explodeEnemy(int ex, int ey) {
        M5Cardputer.Speaker.tone(100, 300);
        for (int dy=-1; dy<=1; dy++) {
            for (int dx=-1; dx<=1; dx++) {
                int ny=ey+dy; int nx=ex+dx;
                if(nx>0 && nx<currentMapWidth-1 && ny>0 && ny<currentMapHeight-1) {
                    if(gameMap[ny][nx]==PLAYER) gameOver=true;
                    if(gameMap[ny][nx]==ENEMY) for (int i=0; i<enemyCount; i++) if(enemies[i].x==nx && enemies[i].y==ny) enemies[i].alive=false;
                    if(gameMap[ny][nx]!=WALL && gameMap[ny][nx]!=EXIT) gameMap[ny][nx]=DIAMOND;
                }
            }
        }
    }

    bool updateEnemies() {
        bool changed = false;
        for (int i = 0; i < enemyCount; i++) {
            if (!enemies[i].alive) continue;
            int ex = enemies[i].x; int ey = enemies[i].y;
            int dx[4] = {0, 1, 0, -1}; int dy[4] = {-1, 0, 1, 0};
            for (int attempts = 0; attempts < 4; attempts++) {
                int tryDir = (enemies[i].dir + attempts) % 4;
                int nx = ex + dx[tryDir]; int ny = ey + dy[tryDir];
                if (gameMap[ny][nx] == PLAYER) { gameOver = true; break; }
                if (gameMap[ny][nx] == EMPTY) {
                    gameMap[ey][ex] = EMPTY; gameMap[ny][nx] = ENEMY;
                    enemies[i].x = nx; enemies[i].y = ny; enemies[i].dir = tryDir;
                    changed = true; break;
                }
            }
        }
        return changed;
    }

    bool updatePhysics() {
        bool changed = false; bool rockFell = false; bool diamondFell = false;
        for (int y = currentMapHeight - 2; y >= 1; y--) {
            for (int x = 1; x < currentMapWidth - 1; x++) {
                uint8_t current = gameMap[y][x];
                if (current == ROCK || current == DIAMOND) {
                    if (gameMap[y+1][x] == EMPTY) {
                        gameMap[y+1][x] = current; gameMap[y][x] = EMPTY; changed = true;
                        if (current == ROCK) rockFell = true; else diamondFell = true;
                        if(y+2 < currentMapHeight) {
                            if (gameMap[y+2][x] == PLAYER) gameOver = true;
                            else if (gameMap[y+2][x] == ENEMY) { gameMap[y+1][x] = EMPTY; explodeEnemy(x, y+2); }
                        }
                    } else if (gameMap[y+1][x] == ROCK || gameMap[y+1][x] == DIAMOND || gameMap[y+1][x] == WALL) {
                        if (gameMap[y][x-1] == EMPTY && gameMap[y+1][x-1] == EMPTY) {
                            gameMap[y+1][x-1] = current; gameMap[y][x] = EMPTY; changed = true;
                        } else if (gameMap[y][x+1] == EMPTY && gameMap[y+1][x+1] == EMPTY) {
                            gameMap[y+1][x+1] = current; gameMap[y][x] = EMPTY; changed = true;
                        }
                    }
                }
            }
        }
        if (diamondFell) M5Cardputer.Speaker.tone(1500, 40); else if (rockFell) M5Cardputer.Speaker.tone(120, 60);  
        return changed;
    }

    void movePlayer(int dx, int dy) {
        int nextX = playerX + dx; int nextY = playerY + dy;
        if (nextX<0 || nextX>=currentMapWidth || nextY<0 || nextY>=currentMapHeight) return;
        if(dy < 0) playerDir = 0; else if(dx > 0) playerDir = 1; else if(dy > 0) playerDir = 2; else if(dx < 0) playerDir = 3;
        uint8_t target = gameMap[nextY][nextX];
        if (target == WALL) { needsRedraw = true; return; } 
        if (target == ENEMY) { gameOver = true; return; }
        if (target == ROCK && dy == 0) {
            if (gameMap[nextY][nextX + dx] == EMPTY) {
                gameMap[nextY][nextX + dx] = ROCK; M5Cardputer.Speaker.tone(150, 50); target = EMPTY; needsRedraw = true;
            } else return;
        }
        if (target == DIRT || target == EMPTY || target == DIAMOND || target == EXIT) {
            if (target == EXIT && diamondsCollected < targetDiamonds) return;
            if (target == DIAMOND) { 
                diamondsCollected++; 
                if (diamondsCollected == targetDiamonds) {
                    doorOpenMsgTimer = millis(); M5Cardputer.Speaker.setVolume(180);
                    M5Cardputer.Speaker.tone(523, 100); delay(100); M5Cardputer.Speaker.tone(659, 100); delay(100);
                    M5Cardputer.Speaker.tone(784, 100); delay(100); M5Cardputer.Speaker.tone(1046, 300); delay(300);
                } else { M5Cardputer.Speaker.setVolume(90); M5Cardputer.Speaker.tone(2000, 50); volumeResetTick = millis() + 50; }
            } else if (target == DIRT || target == EMPTY) M5Cardputer.Speaker.tone(100, 10);
            if (target == EXIT && diamondsCollected >= targetDiamonds) gameWon = true;
            gameMap[playerY][playerX] = EMPTY; playerX = nextX; playerY = nextY;
            gameMap[playerY][playerX] = PLAYER; needsRedraw = true;
            animFrame = (animFrame + 1) % 2; lastAnimTick = millis();
        }
    }

    void drawMap() {
        cameraX = playerX - SCREEN_TILES_X / 2; cameraY = playerY - SCREEN_TILES_Y / 2;
        if (cameraX < 0) cameraX = 0; if (cameraY < 0) cameraY = 0;
        if (cameraX > currentMapWidth - SCREEN_TILES_X) cameraX = currentMapWidth - SCREEN_TILES_X;
        if (cameraY > currentMapHeight - SCREEN_TILES_Y) cameraY = currentMapHeight - SCREEN_TILES_Y;
        M5Cardputer.Display.startWrite();
        for (int y = 0; y < SCREEN_TILES_Y; y++) {
            for (int x = 0; x < SCREEN_TILES_X; x++) {
                int mapX = cameraX + x; int mapY = cameraY + y;
                int posX = x * TILE_SIZE; int posY = y * TILE_SIZE + 15;
                if (mapX >= currentMapWidth || mapY >= currentMapHeight) { M5Cardputer.Display.fillRect(posX, posY, TILE_SIZE, TILE_SIZE, TFT_BLACK); continue; }
                uint8_t tile = gameMap[mapY][mapX];
                if(tile != WALL && tile != DIRT) M5Cardputer.Display.fillRect(posX, posY, TILE_SIZE, TILE_SIZE, TFT_BLACK);
                switch (tile) {
                    case WALL:    sprWall->pushSprite(posX, posY); break;
                    case DIRT:    sprDirt->pushSprite(posX, posY); break;
                    case ROCK:    sprRock->pushSprite(posX, posY, TFT_BLACK); break;
                    case DIAMOND: sprDiamond->pushSprite(posX, posY, TFT_BLACK); break;
                    case EXIT:    if (diamondsCollected >= targetDiamonds) sprExitOpen->pushSprite(posX, posY, TFT_BLACK); else sprExitClosed->pushSprite(posX, posY, TFT_BLACK); break;
                    case ENEMY:   sprEnemy->pushSprite(posX, posY, TFT_BLACK); break;
                    case PLAYER:  sprMouse[playerDir][animFrame]->pushSprite(posX, posY, TFT_BLACK); break;
                }
            }
        }
        M5Cardputer.Display.endWrite();
    }

    void updateHUD() {
        M5Cardputer.Display.fillRect(0, 0, 240, 15, TFT_BLACK); M5Cardputer.Display.setCursor(2, 5);
        if (doorMsgState == 1) { M5Cardputer.Display.setTextColor(TFT_YELLOW); M5Cardputer.Display.print(" DOOR OPEN!!"); } 
        else if (doorMsgState == 2) { M5Cardputer.Display.setTextColor(TFT_GREEN); M5Cardputer.Display.print(" GO TO EXIT!"); } 
        else {
            M5Cardputer.Display.setTextColor(TFT_WHITE);
            M5Cardputer.Display.printf("v0.3 | L:%d S:%d D:%d/%d T:%d", currentLevel, totalScore, diamondsCollected, targetDiamonds, timeLeft);
        }
    }

} // namespace RA

void setupRockADV() {
    M5Cardputer.Display.setRotation(1); M5Cardputer.Display.fillScreen(TFT_BLACK); 
    RA::initSD(); RA::generateSprites(); 
    RA::showSplash();
}

bool loopRockADV() {
    M5Cardputer.update(); unsigned long now = millis();

    if (M5Cardputer.Keyboard.isKeyPressed('v')) {
        if (RA::currentState == RA::PLAYING) { RA::totalScore += (RA::diamondsCollected * 10); RA::checkAndSaveHighScore(RA::totalScore); } 
        else if (RA::currentState == RA::SPLASH || RA::currentState == RA::LEADERBOARD || RA::currentState == RA::REMAP_INPUT) { 
            RA::freeSprites(); return false; 
        }
        delay(200); 
    }

    if (RA::currentState == RA::SPLASH) {
        if (RA::menuNeedsRedraw) { RA::drawLogoScreen(); RA::menuNeedsRedraw = false; }
        if (now - RA::lastToggleTime > 6000) { RA::currentState = RA::LEADERBOARD; RA::lastToggleTime = now; RA::menuNeedsRedraw = true; }
    } 
    else if (RA::currentState == RA::LEADERBOARD) {
        if (RA::menuNeedsRedraw) { RA::drawLeaderboardScreen(); RA::menuNeedsRedraw = false; }
        if (now - RA::lastToggleTime > 6000) { RA::currentState = RA::SPLASH; RA::lastToggleTime = now; RA::menuNeedsRedraw = true; }
    }

    if (RA::currentState == RA::SPLASH || RA::currentState == RA::LEADERBOARD) {
        if (M5Cardputer.Keyboard.isPressed()) {
            Keyboard_Class::KeysState s = M5Cardputer.Keyboard.keysState();
            for (auto c : s.word) {
                if (tolower(c) == 'b') {
                    M5Cardputer.Display.fillScreen(TFT_BLACK); delay(150); RA::resetGameSession(); RA::currentState = RA::PLAYING; return true;
                }
                if (c == '`') { RA::remapStep = 0; RA::remapNeedsRedraw = true; RA::currentState = RA::REMAP_INPUT; return true; }
            }
        }
    }
    else if (RA::currentState == RA::REMAP_INPUT) RA::handleRemapInput();
    else if (RA::currentState == RA::HIGHSCORE_INPUT) RA::handleHighScoreInput();
    else if (RA::currentState == RA::PLAYING) {
        if (RA::volumeResetTick > 0 && now >= RA::volumeResetTick) { M5Cardputer.Speaker.setVolume(180); RA::volumeResetTick = 0; }
        if (RA::doorOpenMsgTimer > 0) {
            unsigned long el = now - RA::doorOpenMsgTimer;
            if (el < 2000 && RA::doorMsgState != 1) { RA::doorMsgState = 1; RA::updateHUD(); } 
            else if (el >= 2000 && el < 4000 && RA::doorMsgState != 2) { RA::doorMsgState = 2; RA::updateHUD(); } 
            else if (el >= 4000) { RA::doorOpenMsgTimer = 0; RA::doorMsgState = 0; RA::updateHUD(); }
        }
        if (now - RA::lastTick >= 1000) { 
            RA::timeLeft--; RA::lastTick = now; if(RA::doorMsgState == 0) RA::updateHUD(); if (RA::timeLeft <= 0) RA::gameOver = true; 
        }
        if (now - RA::lastAnimTick >= RA::ANIM_SPEED) { RA::animFrame = (RA::animFrame + 1) % 2; RA::lastAnimTick = now; RA::needsRedraw = true; }
        if (now - RA::lastPhysicsTick >= RA::PHYSICS_INTERVAL) { if(RA::updatePhysics()) RA::needsRedraw = true; RA::lastPhysicsTick = now; }
        if (now - RA::lastEnemyTick >= RA::ENEMY_INTERVAL) { if(RA::updateEnemies()) RA::needsRedraw = true; RA::lastEnemyTick = now; }
        if (RA::gameOver) {
            RA::totalScore += (RA::diamondsCollected * 10); M5Cardputer.Display.fillScreen(TFT_BLACK); M5Cardputer.Display.setCursor(60, 60);
            M5Cardputer.Display.setTextColor(TFT_RED); M5Cardputer.Display.setTextSize(2); M5Cardputer.Display.print("GAME OVER"); M5Cardputer.Speaker.tone(100, 800);
            delay(3000); RA::gameOver = false; RA::checkAndSaveHighScore(RA::totalScore); return true; 
        }
        if (RA::gameWon) {
            RA::totalScore += (RA::diamondsCollected * 10) + RA::timeLeft; M5Cardputer.Display.fillScreen(TFT_BLACK); M5Cardputer.Display.setCursor(45, 60);
            M5Cardputer.Display.setTextColor(TFT_GREEN); M5Cardputer.Display.setTextSize(2); M5Cardputer.Display.print("LEVEL CLEAR!"); M5Cardputer.Speaker.tone(1000, 600);
            delay(2500); RA::gameWon = false; RA::currentLevel++; RA::initLevel(); return true; 
        }
        if (M5Cardputer.Keyboard.isPressed() && (now - RA::lastMoveTick >= RA::MOVEMENT_INTERVAL)) {
            Keyboard_Class::KeysState s = M5Cardputer.Keyboard.keysState();
            bool ip = false;
            for (auto c : s.word) {
                char l = tolower(c);
                if (l == RA::keyUp) { RA::movePlayer(0, -1); ip = true; break; }
                else if (l == RA::keyDn) { RA::movePlayer(0, 1); ip = true; break; }
                else if (l == RA::keyL) { RA::movePlayer(-1, 0); ip = true; break; }
                else if (l == RA::keyR) { RA::movePlayer(1, 0); ip = true; break; }
            }
            if (ip) { if(RA::doorMsgState == 0) RA::updateHUD(); RA::lastMoveTick = now; }
        }
        if (RA::needsRedraw) { RA::drawMap(); RA::needsRedraw = false; }
    }
    delay(5); return true; 
}