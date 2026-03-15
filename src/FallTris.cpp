// All comments in the code must always be in English.
#include <M5Cardputer.h>
#include <M5Unified.h>
#include <SPI.h>
#include <SD.h>
#include <FS.h>
#include <ctype.h>

#include "FallTris.h"
#include "logo.h" 
#include "splash.h" 
#include "AudioTask.h" // MP3 Check integration

namespace FT {

    int textOffsets[] = {43, 43, 45, 43, 43};

    const int BOARD_W = 10;
    const int BOARD_H = 18; 

    int blockSize = 6;
    int xOff = 95; 
    int yOff = 25; 
    bool portraitMode = false;
    bool menuMusicPlayed = false;
    bool isPaused = false; 

    #define COLOR_UI_BAR     0x3186 
    #define COLOR_UI_BORDER  0x6B4D 

    char keyL = 'a'; char keyR = 'd'; char keyDn = 'z'; char keyDrop = 'e';
    char keyCW = 'p'; char keyCCW = 'k';
    int remapStep = 0;
    char tempKeys[6];
    const char* remapNames[] = {"LEFT", "RIGHT", "DOWN", "HARD DROP", "ROTATE CW", "ROTATE CCW"};
    bool remapNeedsRedraw = true; 

    unsigned long leftHeldTime = 0;
    unsigned long rightHeldTime = 0;
    unsigned long downHeldTime = 0;
    const int DAS_DELAY = 200;  
    const int DAS_REPEAT = 40;  

    M5Canvas* canvas = nullptr;
    uint16_t colors_base[] = {0x0000, 0x07FF, 0x001F, 0xFBE0, 0xFFE0, 0x07E0, 0xA11F, 0xF800};

    enum GameState { MENU, LEADERBOARD, CONFIG, PLAYING, GAMEOVER, HIGHSCORE_INPUT, REMAP_INPUT };
    GameState currentState = MENU;

    unsigned long gameOverTimer = 0;
    unsigned long lastMenuSwitch = 0; 

    struct HighScoreEntry { char name[4]; int score; };
    HighScoreEntry highScores[5];
    bool sdAvailable = false;
    char newName[4] = "A__";
    int charIndex = 0;
    int tempScore = 0;

    int grid[BOARD_W][BOARD_H] = {0};
    int curX = 4, curY = 1, curT = 1, curR = 0;
    int nextT = 1; 
    uint32_t lastTick = 0, score = 0;

    bool prevL = false, prevR = false, prevD = false, prevU = false;
    bool prevCW = false, prevCCW = false, prevV = false, prevB = false;

    // --- Async Start Music ---
    int startMelody[] = {659, 494, 523, 587, 523, 494, 440, 440, 523, 659, 587, 523, 494, 494, 523, 587, 659, 523, 440, 440};
    int startDurations[] = {250, 125, 125, 250, 125, 125, 250, 125, 125, 250, 125, 125, 250, 125, 125, 250, 250, 250, 250, 250};
    int currentNote = 0;
    unsigned long nextNoteTime = 0;
    bool isPlayingMusic = false;

    const int8_t tetrominoes[8][4][4][2] = {
        {{{0,0},{0,0},{0,0},{0,0}}, {{0,0},{0,0},{0,0},{0,0}}, {{0,0},{0,0},{0,0},{0,0}}, {{0,0},{0,0},{0,0},{0,0}}},
        {{{0,-1},{0,0},{0,1},{0,2}}, {{-1,0},{0,0},{1,0},{2,0}}, {{0,-1},{0,0},{0,1},{0,2}}, {{-1,0},{0,0},{1,0},{2,0}}}, 
        {{{0,-1},{0,0},{0,1},{-1,1}}, {{-1,0},{0,0},{1,0},{1,1}}, {{0,-1},{0,0},{0,1},{1,-1}}, {{-1,-1},{-1,0},{0,0},{1,0}}}, 
        {{{0,-1},{0,0},{0,1},{1,1}}, {{-1,1},{-1,0},{0,0},{1,0}}, {{0,-1},{0,0},{0,1},{-1,-1}}, {{-1,0},{0,0},{1,0},{1,-1}}}, 
        {{{0,0},{1,0},{0,1},{1,1}}, {{0,0},{1,0},{0,1},{1,1}}, {{0,0},{1,0},{0,1},{1,1}}, {{0,0},{1,0},{0,1},{1,1}}}, 
        {{{0,0},{1,0},{0,1},{-1,1}}, {{0,-1},{0,0},{1,0},{1,1}}, {{0,0},{1,0},{0,1},{-1,1}}, {{0,-1},{0,0},{1,0},{1,1}}}, 
        {{{0,0},{-1,0},{1,0},{0,1}}, {{0,0},{0,-1},{0,1},{1,0}}, {{0,0},{-1,0},{1,0},{0,-1}}, {{0,0},{0,-1},{0,1},{-1,0}}}, 
        {{{0,0},{-1,0},{0,1},{1,1}}, {{0,1},{0,0},{1,0},{1,-1}}, {{0,0},{-1,0},{0,1},{1,1}}, {{0,1},{0,0},{1,0},{1,-1}}}  
    };

    void loadKeys() {
        if (!sdAvailable) return;
        File f = SD.open("/CasualADV/falltris.ini", FILE_READ);
        if (f) {
            String s = f.readStringUntil('\n');
            if (s.length() >= 6) { keyL = s[0]; keyR = s[1]; keyDn = s[2]; keyDrop = s[3]; keyCW = s[4]; keyCCW = s[5]; }
            f.close();
        }
    }

    void saveKeys() {
        if (!sdAvailable) return;
        if (!SD.exists("/CasualADV")) SD.mkdir("/CasualADV");
        File f = SD.open("/CasualADV/falltris.ini", FILE_WRITE);
        if (f) { f.printf("%c%c%c%c%c%c\n", keyL, keyR, keyDn, keyDrop, keyCW, keyCCW); f.close(); }
    }

    void initSD() {
        for (int i = 0; i < 5; i++) { strcpy(highScores[i].name, "---"); highScores[i].score = 0; }
        if (SD.cardType() != CARD_NONE) { 
            sdAvailable = true;
            if (!SD.exists("/CasualADV")) SD.mkdir("/CasualADV");
            loadKeys();
            File file = SD.open("/CasualADV/falltris.high", FILE_READ);
            if (file) {
                for (int i = 0; i < 5; i++) {
                    if (file.available()) {
                        String line = file.readStringUntil('\n');
                        int commaIdx = line.indexOf(',');
                        if (commaIdx > 0) {
                            String n = line.substring(0, commaIdx); n.trim(); n.toCharArray(highScores[i].name, 4);
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
        File file = SD.open("/CasualADV/falltris.high", FILE_WRITE);
        if (file) { for (int i = 0; i < 5; i++) file.printf("%s,%d\n", highScores[i].name, highScores[i].score); file.close(); }
    }

    void updateLayout() {
        if (canvas != nullptr) canvas->deleteSprite();
        if (portraitMode) {
            M5Cardputer.Display.setRotation(2); blockSize = 11;
            xOff = (135 - BOARD_W * blockSize) / 2; yOff = 35; 
            canvas->setColorDepth(16); canvas->createSprite(135, 240);
        } else {
            M5Cardputer.Display.setRotation(1); blockSize = 6;
            xOff = 90; yOff = 22; 
            canvas->setColorDepth(16); canvas->createSprite(240, 135);
        }
    }

    void transitionToMenu() {
        portraitMode = false; updateLayout();
        menuMusicPlayed = false; currentState = MENU;
        lastMenuSwitch = millis();
        isPlayingMusic = false; 
    }

    void startHighScoreEntry(int newScore) {
        tempScore = newScore; strcpy(newName, "A__"); charIndex = 0;
        currentState = HIGHSCORE_INPUT;
        while (M5Cardputer.Keyboard.isPressed()) { M5Cardputer.update(); delay(10); }
    }

    void checkAndSaveHighScore(int finalScore) {
        if (finalScore > highScores[4].score) {
            if (sdAvailable) startHighScoreEntry(finalScore);
            else {
                canvas->fillSprite(TFT_BLACK); canvas->setTextColor(TFT_RED); canvas->setTextSize(2);
                canvas->setCursor(35, 50); canvas->print("SD NOT PRESENT");
                canvas->setTextSize(1); canvas->setTextColor(TFT_WHITE);
                canvas->setCursor(85, 80); canvas->printf("Score: %06d", finalScore);
                canvas->pushSprite(0, 0); M5Cardputer.Speaker.tone(150, 600); delay(2500); transitionToMenu();
            }
        } else transitionToMenu();
    }

    void handleRemapInput() {
        if (remapNeedsRedraw) {
            canvas->fillSprite(TFT_BLACK);
            canvas->setTextColor(TFT_CYAN); canvas->setTextSize(2);
            canvas->setCursor(30, 20); canvas->print("REMAP CONTROLS");
            canvas->setTextColor(TFT_WHITE); canvas->setTextSize(1);
            canvas->setCursor(30, 60); canvas->print("Press key for:");
            canvas->setTextColor(TFT_YELLOW); canvas->setTextSize(2);
            canvas->setCursor(30, 80); canvas->print(remapNames[remapStep]);
            canvas->setTextColor(TFT_DARKGRAY); canvas->setTextSize(1);
            canvas->setCursor(30, 115); canvas->print("(V & B are reserved)");
            remapNeedsRedraw = false;
        }

        if (M5Cardputer.Keyboard.isChange() && M5Cardputer.Keyboard.isPressed()) {
            Keyboard_Class::KeysState s = M5Cardputer.Keyboard.keysState();
            for (auto c : s.word) {
                char lowerC = tolower(c);
                if (lowerC == 'v' || lowerC == 'b') { M5Cardputer.Speaker.tone(200, 100); return; }
                tempKeys[remapStep] = lowerC; M5Cardputer.Speaker.tone(1000, 50); remapStep++; remapNeedsRedraw = true;
                if (remapStep >= 6) {
                    keyL = tempKeys[0]; keyR = tempKeys[1]; keyDn = tempKeys[2];
                    keyDrop = tempKeys[3]; keyCW = tempKeys[4]; keyCCW = tempKeys[5];
                    saveKeys(); transitionToMenu();
                }
                delay(200); 
            }
        }
    }

    void handleHighScoreInput() {
        canvas->fillSprite(TFT_BLACK);
        canvas->setTextColor(TFT_CYAN); canvas->setTextSize(2); canvas->setCursor(30, 30); canvas->print("NEW HIGH SCORE!");
        canvas->setTextColor(TFT_WHITE); canvas->setTextSize(1); canvas->setCursor(81, 60); canvas->printf("SCORE: %06d", tempScore);
        canvas->setTextSize(3);
        for (int i = 0; i < 3; i++) {
            if (i == charIndex) canvas->setTextColor(TFT_GREEN); else canvas->setTextColor(TFT_YELLOW); 
            canvas->setCursor(84 + (i * 24), 90); canvas->printf("%c", newName[i]);
        }
        canvas->setTextColor(TFT_GREEN); canvas->setTextSize(1);
        canvas->setCursor(15, 125); canvas->print("Use current Up/Down/L/R & B to save");

        if (M5Cardputer.Keyboard.isChange() && M5Cardputer.Keyboard.isPressed()) {
            Keyboard_Class::KeysState s = M5Cardputer.Keyboard.keysState();
            for (auto c : s.word) {
                char lowerC = tolower(c);
                if (lowerC == keyDrop) { if (newName[charIndex] == '_' || newName[charIndex] == 'Z') newName[charIndex] = 'A'; else newName[charIndex]++; }
                else if (lowerC == keyDn) { if (newName[charIndex] == '_' || newName[charIndex] == 'A') newName[charIndex] = 'Z'; else newName[charIndex]--; }
                else if (lowerC == keyL && charIndex > 0) charIndex--;
                else if (lowerC == keyR && charIndex < 2) { charIndex++; if (newName[charIndex] == '_') newName[charIndex] = 'A'; }
                else if (lowerC == 'b') {
                    for (int i = 0; i < 3; i++) if (newName[i] == '_') newName[i] = ' ';
                    for (int i = 0; i < 5; i++) {
                        if (tempScore > highScores[i].score) {
                            for (int j = 4; j > i; j--) highScores[j] = highScores[j - 1];
                            strcpy(highScores[i].name, newName); highScores[i].score = tempScore; break;
                        }
                    }
                    saveScoreToSD(); M5Cardputer.Speaker.tone(1000, 200); transitionToMenu();
                }
            }
        }
    }

    void drawTile3D(int x, int y, int c) {
        int px = xOff + x * blockSize; int py = yOff + y * blockSize;
        if (c == 0) return; 
        uint16_t base = colors_base[c];
        canvas->fillRect(px, py, blockSize, blockSize, base);
        canvas->drawFastHLine(px, py, blockSize, TFT_WHITE);            
        canvas->drawFastVLine(px, py, blockSize, TFT_WHITE);            
        canvas->drawFastHLine(px, py + blockSize - 1, blockSize, 0x4208); 
        canvas->drawFastVLine(px + blockSize - 1, py, blockSize, 0x4208); 
    }

    void drawGhostTile(int x, int y, int c) {
        int px = xOff + x * blockSize; int py = yOff + y * blockSize;
        if (c == 0) return; 
        canvas->drawRect(px, py, blockSize, blockSize, colors_base[c]);
        canvas->drawRect(px + 1, py + 1, blockSize - 2, blockSize - 2, 0x4208); 
    }

    void drawAbsoluteTile3D(int px, int py, int c) {
        if (c == 0) return; 
        canvas->fillRect(px, py, blockSize, blockSize, colors_base[c]);
        canvas->drawFastHLine(px, py, blockSize, TFT_WHITE);            
        canvas->drawFastVLine(px, py, blockSize, TFT_WHITE);            
        canvas->drawFastHLine(px, py + blockSize - 1, blockSize, 0x4208); 
        canvas->drawFastVLine(px + blockSize - 1, py, blockSize, 0x4208); 
    }

    bool hit(int tx, int ty, int tr) {
        for(int i = 0; i < 4; i++) {
            int px = tx + tetrominoes[curT][tr][i][0]; int py = ty + tetrominoes[curT][tr][i][1];
            if(px < 0 || px >= BOARD_W || py >= BOARD_H) return true;
            if(py >= 0 && grid[px][py]) return true;
        }
        return false;
    }

    int clearLines() {
        int linesCleared = 0;
        for(int y = BOARD_H - 1; y >= 0; y--) {
            bool full = true;
            for(int x = 0; x < BOARD_W; x++) if(!grid[x][y]) full = false;
            if(full) {
                linesCleared++;
                for(int ty = y; ty > 0; ty--) for(int tx = 0; tx < BOARD_W; tx++) grid[tx][ty] = grid[tx][ty - 1];
                for(int tx = 0; tx < BOARD_W; tx++) grid[tx][0] = 0;
                y++; 
            }
        }
        if(linesCleared == 1) score += 100; else if(linesCleared == 2) score += 300; else if(linesCleared == 3) score += 500; else if(linesCleared == 4) score += 800;
        return linesCleared;
    }

    void playStartMusicAsync() {
        isPlayingMusic = true; currentNote = 0;
        M5Cardputer.Speaker.tone(startMelody[0], startDurations[0]);
        nextNoteTime = millis() + startDurations[0] + 30;
    }

    void updateStartMusic() {
        if (!isPlayingMusic) return;
        if (millis() >= nextNoteTime) {
            currentNote++;
            if (currentNote >= 20) isPlayingMusic = false;
            else {
                M5Cardputer.Speaker.tone(startMelody[currentNote], startDurations[currentNote]);
                nextNoteTime = millis() + startDurations[currentNote] + 30;
            }
        }
    }

    void playThud() { M5Cardputer.Speaker.tone(300, 30); delay(30); M5Cardputer.Speaker.tone(150, 30); }
    void playDing(int count) { for (int i = 0; i < count; i++) { M5Cardputer.Speaker.tone(1319, 100); delay(150); } }

    void playTetrisJingle() {
        delay(116); M5Cardputer.Speaker.tone(440, 123); delay(123); delay(34);
        M5Cardputer.Speaker.tone(87, 61); delay(61); M5Cardputer.Speaker.tone(175, 68); delay(68); delay(23);
        M5Cardputer.Speaker.tone(440, 77); delay(77); M5Cardputer.Speaker.tone(349, 52); delay(52); delay(207);
        M5Cardputer.Speaker.tone(175, 57); delay(57); M5Cardputer.Speaker.tone(349, 100); delay(100);
        M5Cardputer.Speaker.tone(523, 109); delay(109); M5Cardputer.Speaker.tone(440, 114); delay(114); delay(61);
        M5Cardputer.Speaker.tone(494, 125); delay(125); M5Cardputer.Speaker.tone(392, 125); delay(125); delay(139);
        M5Cardputer.Speaker.tone(392, 50); delay(50); M5Cardputer.Speaker.tone(494, 50); delay(50); delay(213);
        M5Cardputer.Speaker.tone(392, 66); delay(66); M5Cardputer.Speaker.tone(494, 102); delay(102); delay(367);
        M5Cardputer.Speaker.tone(392, 164); delay(164); delay(6); M5Cardputer.Speaker.tone(392, 139); delay(139);
        M5Cardputer.Speaker.tone(523, 14); delay(14);
    }

    void resetGame() {
        memset(grid, 0, sizeof(grid)); score = 0; curX = 4; curY = 1; curR = 0; 
        curT = random(1, 8); nextT = random(1, 8); 
        isPaused = false; leftHeldTime = 0; rightHeldTime = 0; downHeldTime = 0;
    }

} 

void setupFallTris() {
    randomSeed(millis());
    if (FT::canvas == nullptr) FT::canvas = new M5Canvas(&M5Cardputer.Display);
    FT::initSD(); 
    FT::currentState = FT::MENU;
    FT::portraitMode = false; FT::menuMusicPlayed = false; FT::isPaused = false;
    FT::isPlayingMusic = false; 
    FT::updateLayout();
    FT::lastMenuSwitch = millis();
    FT::prevL = false; FT::prevR = false; FT::prevD = false; FT::prevU = false;
    FT::prevCW = false; FT::prevCCW = false; FT::prevV = false; FT::prevB = false;
}

bool loopFallTris() {
    M5Cardputer.update();
    if (FT::currentState != FT::REMAP_INPUT) FT::canvas->fillSprite(TFT_BLACK); 

    bool currV = M5Cardputer.Keyboard.isKeyPressed('v');

    if (currV && !FT::prevV) {
        if (FT::currentState == FT::MENU || FT::currentState == FT::LEADERBOARD || FT::currentState == FT::REMAP_INPUT) {
            if (FT::canvas != nullptr) { FT::canvas->deleteSprite(); delete FT::canvas; FT::canvas = nullptr; }
            FT::isPlayingMusic = false; 
            M5Cardputer.Display.setRotation(1); 
            return false; 
        } else if (FT::currentState == FT::PLAYING) {
            FT::portraitMode = false; FT::updateLayout(); FT::checkAndSaveHighScore(FT::score); return true;
        } else FT::transitionToMenu();
    }

    if ((FT::currentState == FT::MENU || FT::currentState == FT::LEADERBOARD) && M5Cardputer.Keyboard.isChange() && M5Cardputer.Keyboard.isPressed()) {
        Keyboard_Class::KeysState s = M5Cardputer.Keyboard.keysState();
        for (auto c : s.word) {
            char lowerC = tolower(c);
            if (lowerC == 'b') { FT::isPlayingMusic = false; FT::portraitMode = false; FT::updateLayout(); FT::resetGame(); FT::currentState = FT::PLAYING; break; }
            if (lowerC == 'p') { FT::isPlayingMusic = false; FT::portraitMode = true; FT::updateLayout(); FT::resetGame(); FT::currentState = FT::PLAYING; break; }
            if (lowerC == 'k') { FT::isPlayingMusic = false; FT::currentState = FT::CONFIG; break; }
            if (lowerC == '`') { 
                FT::isPlayingMusic = false; FT::remapStep = 0; FT::remapNeedsRedraw = true; FT::currentState = FT::REMAP_INPUT; 
                while(M5Cardputer.Keyboard.isPressed()) { M5Cardputer.update(); delay(10); } break;
            }
        }
    }

    if (FT::currentState == FT::MENU) {
        FT::canvas->setSwapBytes(true); FT::canvas->pushImage(45, 10, 150, 71, (const uint16_t*)tetris_logo); FT::canvas->setSwapBytes(false);
        FT::canvas->setTextSize(1); FT::canvas->setTextColor(TFT_CYAN); FT::canvas->setCursor(185, 85); FT::canvas->print("v0.3");
        FT::canvas->setTextColor(TFT_WHITE);
        FT::canvas->setCursor(25, 100); FT::canvas->println("B:Play(Land) | P:Play(Port)");
        FT::canvas->setCursor(25, 115); FT::canvas->println("K:Help | ESC:Remap | V:Quit");
        
        if (!FT::menuMusicPlayed) { 
            FT::canvas->pushSprite(0, 0); 
            // HERE IS THE MAGIC: Only play retro music if MP3 is disabled globally
            if (!AudioTask::mp3Enabled) FT::playStartMusicAsync(); 
            FT::menuMusicPlayed = true; 
        }

        FT::updateStartMusic(); 
        if (millis() - FT::lastMenuSwitch > 3000) { FT::currentState = FT::LEADERBOARD; FT::lastMenuSwitch = millis(); }
    } 
    else if (FT::currentState == FT::LEADERBOARD) {
        FT::canvas->setSwapBytes(true); FT::canvas->pushImage(30, 2, 180, 70, highscores_logo); FT::canvas->setSwapBytes(false); 
        FT::canvas->fillRoundRect(30, 75, 180, 56, 6, COLOR_UI_BAR); FT::canvas->drawRoundRect(30, 75, 180, 56, 6, COLOR_UI_BORDER);
        FT::canvas->setTextColor(TFT_WHITE);
        for (int i = 0; i < 5; i++) {
            char hsStr[30]; sprintf(hsStr, "%d. %s : %06d", i + 1, FT::highScores[i].name, FT::highScores[i].score);
            FT::canvas->drawString(hsStr, 60, 80 + (i * 10)); 
        }
        FT::updateStartMusic(); 
        if (millis() - FT::lastMenuSwitch > 3000) { FT::currentState = FT::MENU; FT::lastMenuSwitch = millis(); }
    }
    else if (FT::currentState == FT::REMAP_INPUT) FT::handleRemapInput();
    else if (FT::currentState == FT::CONFIG) {
        FT::canvas->setTextColor(TFT_WHITE); FT::canvas->setTextSize(1.5); FT::canvas->setCursor(5, 10); FT::canvas->println("CONTROLS:");
        FT::canvas->setTextColor(TFT_YELLOW); FT::canvas->setTextSize(1); FT::canvas->setCursor(5, 40);
        char buf[60];
        sprintf(buf, "LAND: %c(L) %c(R) %c(D) %c(DROP)", toupper(FT::keyL), toupper(FT::keyR), toupper(FT::keyDn), toupper(FT::keyDrop)); FT::canvas->println(buf);
        sprintf(buf, "PORT: %c(L) %c(R) %c(D) %c(DROP)", toupper(FT::keyDrop), toupper(FT::keyDn), toupper(FT::keyL), toupper(FT::keyR)); FT::canvas->println(buf);
        sprintf(buf, "BOTH: %c(CW) %c(CCW) B(Pause)", toupper(FT::keyCW), toupper(FT::keyCCW)); FT::canvas->println(buf);
        FT::canvas->setTextColor(TFT_CYAN); FT::canvas->setCursor(5, FT::canvas->height() - 20); FT::canvas->println("Press B to return");
        if (M5Cardputer.Keyboard.isKeyPressed('b')) FT::currentState = FT::MENU;
    }
    else if (FT::currentState == FT::HIGHSCORE_INPUT) FT::handleHighScoreInput();
    else if (FT::currentState == FT::PLAYING) {
        bool currL = FT::portraitMode ? M5Cardputer.Keyboard.isKeyPressed(FT::keyDrop) : M5Cardputer.Keyboard.isKeyPressed(FT::keyL);
        bool currR = FT::portraitMode ? M5Cardputer.Keyboard.isKeyPressed(FT::keyDn) : M5Cardputer.Keyboard.isKeyPressed(FT::keyR);
        bool currD = FT::portraitMode ? M5Cardputer.Keyboard.isKeyPressed(FT::keyL) : M5Cardputer.Keyboard.isKeyPressed(FT::keyDn);
        bool currU = FT::portraitMode ? M5Cardputer.Keyboard.isKeyPressed(FT::keyR) : M5Cardputer.Keyboard.isKeyPressed(FT::keyDrop);
        bool currCW = M5Cardputer.Keyboard.isKeyPressed(FT::keyCW);
        bool currCCW = M5Cardputer.Keyboard.isKeyPressed(FT::keyCCW);
        bool currB = M5Cardputer.Keyboard.isKeyPressed('b');

        if (currB && !FT::prevB) { FT::isPaused = !FT::isPaused; if (!FT::isPaused) FT::lastTick = millis(); }

        if (!FT::isPaused) {
            unsigned long now = millis();
            if (currL) {
                if (!FT::prevL) { if(!FT::hit(FT::curX - 1, FT::curY, FT::curR)) FT::curX--; FT::leftHeldTime = now; } 
                else if (now - FT::leftHeldTime > FT::DAS_DELAY) { if(!FT::hit(FT::curX - 1, FT::curY, FT::curR)) FT::curX--; FT::leftHeldTime = now - FT::DAS_DELAY + FT::DAS_REPEAT; }
            } else FT::leftHeldTime = 0;
            if (currR) {
                if (!FT::prevR) { if(!FT::hit(FT::curX + 1, FT::curY, FT::curR)) FT::curX++; FT::rightHeldTime = now; } 
                else if (now - FT::rightHeldTime > FT::DAS_DELAY) { if(!FT::hit(FT::curX + 1, FT::curY, FT::curR)) FT::curX++; FT::rightHeldTime = now - FT::DAS_DELAY + FT::DAS_REPEAT; }
            } else FT::rightHeldTime = 0;
            if (currD) {
                if (!FT::prevD) { if(!FT::hit(FT::curX, FT::curY + 1, FT::curR)) FT::curY++; FT::downHeldTime = now; } 
                else if (now - FT::downHeldTime > FT::DAS_DELAY / 2) { if(!FT::hit(FT::curX, FT::curY + 1, FT::curR)) FT::curY++; FT::downHeldTime = now - (FT::DAS_DELAY / 2) + FT::DAS_REPEAT; }
            } else FT::downHeldTime = 0;
            if (currU && !FT::prevU) { while(!FT::hit(FT::curX, FT::curY + 1, FT::curR)) FT::curY++; }
            if (currCW && !FT::prevCW) { int nextR = (FT::curR + 1) % 4; if(!FT::hit(FT::curX, FT::curY, nextR)) FT::curR = nextR; }
            if (currCCW && !FT::prevCCW) { int nextR = (FT::curR + 3) % 4; if(!FT::hit(FT::curX, FT::curY, nextR)) FT::curR = nextR; }

            int currentSpeed = 600 - (FT::score / 100) * 8; if (currentSpeed < 80) currentSpeed = 80; 

            if (millis() - FT::lastTick > currentSpeed) {
                if (!FT::hit(FT::curX, FT::curY + 1, FT::curR)) FT::curY++;
                else {
                    for(int i = 0; i < 4; i++) FT::grid[FT::curX + FT::tetrominoes[FT::curT][FT::curR][i][0]][FT::curY + FT::tetrominoes[FT::curT][FT::curR][i][1]] = FT::curT;
                    int linesToClear = 0;
                    for(int y = 0; y < FT::BOARD_H; y++) {
                        bool full = true; for(int x = 0; x < FT::BOARD_W; x++) if(!FT::grid[x][y]) full = false;
                        if(full) linesToClear++;
                    }
                    if (linesToClear == 4) {
                        FT::canvas->fillSprite(TFT_BLACK); FT::canvas->setTextSize(1); FT::canvas->setTextColor(TFT_WHITE);
                        FT::canvas->setCursor(FT::xOff, FT::yOff - 20); FT::canvas->printf("Score: %d", FT::score);
                        int previewRot = (FT::nextT == 1 || FT::nextT == 2 || FT::nextT == 3) ? 1 : 0;
                        int pXBase = FT::xOff + FT::BOARD_W * FT::blockSize - 3 * FT::blockSize; int pYBase = FT::yOff - 2 * FT::blockSize - 2; 
                        for(int i = 0; i < 4; i++) { int bx = pXBase + FT::tetrominoes[FT::nextT][previewRot][i][0] * FT::blockSize; int by = pYBase + FT::tetrominoes[FT::nextT][previewRot][i][1] * FT::blockSize; FT::drawAbsoluteTile3D(bx, by, FT::nextT); }
                        FT::canvas->drawRect(FT::xOff - 1, FT::yOff - 1, FT::BOARD_W * FT::blockSize + 2, FT::BOARD_H * FT::blockSize + 2, TFT_WHITE);
                        for(int x = 0; x < FT::BOARD_W; x++) for(int y = 0; y < FT::BOARD_H; y++) FT::drawTile3D(x, y, FT::grid[x][y]);
                        FT::canvas->pushSprite(0, 0);
                        FT::playTetrisJingle(); FT::clearLines();
                    } else {
                        int cleared = FT::clearLines();
                        if (cleared > 0) FT::playDing(cleared); else FT::playThud();         
                    }
                    FT::curX = 4; FT::curY = 1; FT::curR = 0; FT::curT = FT::nextT; FT::nextT = random(1, 8); 
                    if(FT::hit(FT::curX, FT::curY, FT::curR)) { FT::currentState = FT::GAMEOVER; FT::gameOverTimer = millis(); }
                }
                FT::lastTick = millis();
            }
        }

        FT::canvas->setTextSize(1); FT::canvas->setTextColor(TFT_WHITE);
        FT::canvas->setCursor(FT::xOff, FT::yOff - 20); FT::canvas->printf("Score: %d", FT::score);
        int previewRot = (FT::nextT == 1 || FT::nextT == 2 || FT::nextT == 3) ? 1 : 0;
        int pXBase = FT::xOff + FT::BOARD_W * FT::blockSize - 3 * FT::blockSize; int pYBase = FT::yOff - 2 * FT::blockSize - 2; 
        for(int i = 0; i < 4; i++) { int bx = pXBase + FT::tetrominoes[FT::nextT][previewRot][i][0] * FT::blockSize; int by = pYBase + FT::tetrominoes[FT::nextT][previewRot][i][1] * FT::blockSize; FT::drawAbsoluteTile3D(bx, by, FT::nextT); }
        FT::canvas->drawRect(FT::xOff - 1, FT::yOff - 1, FT::BOARD_W * FT::blockSize + 2, FT::BOARD_H * FT::blockSize + 2, TFT_WHITE);
        for(int x = 0; x < FT::BOARD_W; x++) for(int y = 0; y < FT::BOARD_H; y++) FT::drawTile3D(x, y, FT::grid[x][y]);
        int ghostY = FT::curY; while (!FT::hit(FT::curX, ghostY + 1, FT::curR)) ghostY++;
        for(int i = 0; i < 4; i++) FT::drawGhostTile(FT::curX + FT::tetrominoes[FT::curT][FT::curR][i][0], ghostY + FT::tetrominoes[FT::curT][FT::curR][i][1], FT::curT);
        for(int i = 0; i < 4; i++) FT::drawTile3D(FT::curX + FT::tetrominoes[FT::curT][FT::curR][i][0], FT::curY + FT::tetrominoes[FT::curT][FT::curR][i][1], FT::curT);

        if (FT::isPaused) {
            int centerX = FT::xOff + (FT::BOARD_W * FT::blockSize) / 2; int centerY = FT::yOff + (FT::BOARD_H * FT::blockSize) / 2;
            FT::canvas->fillRect(centerX - 24, centerY - 10, 48, 20, TFT_BLACK); FT::canvas->drawRect(centerX - 24, centerY - 10, 48, 20, TFT_WHITE);
            FT::canvas->setTextColor(TFT_YELLOW); FT::canvas->setCursor(centerX - 18, centerY - 3); FT::canvas->print("PAUSED");
        }
        FT::prevL = currL; FT::prevR = currR; FT::prevD = currD; FT::prevU = currU; FT::prevCW = currCW; FT::prevCCW = currCCW; FT::prevB = currB;
    }
    else if (FT::currentState == FT::GAMEOVER) {
        FT::canvas->setTextColor(TFT_RED); FT::canvas->setTextSize(2); FT::canvas->setCursor(FT::portraitMode ? 15 : 60, FT::portraitMode ? 100 : 50); FT::canvas->print("GAME OVER");
        FT::canvas->setTextSize(1); FT::canvas->setCursor(FT::portraitMode ? 15 : 60, FT::portraitMode ? 140 : 80); FT::canvas->setTextColor(TFT_WHITE); FT::canvas->printf("Final Score: %d", FT::score);
        if (millis() - FT::gameOverTimer > 3000) { FT::portraitMode = false; FT::updateLayout(); FT::checkAndSaveHighScore(FT::score); return true; }
    }
    FT::prevV = currV; 
    FT::canvas->pushSprite(0, 0); delay(1); 
    return true; 
}