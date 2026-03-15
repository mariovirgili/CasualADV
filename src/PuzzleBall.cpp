// All comments in the code must always be in English.
#include <M5Cardputer.h>
#include <M5Unified.h>
#include <SPI.h>
#include <SD.h>
#include <FS.h>
#include <vector>
#include <cmath>
#include <ctype.h>

#include "PuzzleBall.h"
#include "splash.h" // Contains puzzleball_logo and highscores_logo

namespace PB {

    const float FRICTION = 0.985;
    const int BALL_RADIUS = 8; 
    const float MAX_POWER = 15.0;
    const int TRAIL_LENGTH = 12; 
    int textOffsets[] = {43, 43, 45, 43, 43}; // Baseline Granitica

    #define COLOR_BG_DARK    0x2945 
    #define COLOR_TILE_LINE  0x5289 
    #define COLOR_BALL_OUTLINE 0x1022 
    #define COLOR_HIGHLIGHT  TFT_WHITE 
    #define COLOR_UI_BAR     0x3186 
    #define COLOR_UI_BORDER  0x6B4D 
    #define COLOR_POWER_BAR  0xFD20 

    LGFX_Sprite* canvas;

    // --- Custom Keys Variables ---
    char keyAL = 'a';  // Aim Left
    char keyAR = 'd';  // Aim Right
    char keyPUp = 'z'; // Power Up
    char keyPDn = 'e'; // Power Down
    char keySh = 'p';  // Shoot
    int remapStep = 0;
    char tempKeys[5];
    const char* remapNames[] = {"AIM LEFT", "AIM RIGHT", "POWER UP", "POWER DOWN", "SHOOT"};
    bool remapNeedsRedraw = true; 

    struct TrailPoint { float x, y; };

    struct Ball {
        float x, y;
        float vx, vy;
        int level;
        uint16_t color;
        bool active;
        std::vector<TrailPoint> trail; 
        int flashTimer; 
    };

    struct Particle {
        float x, y;
        float vx, vy;
        int life;
        int maxLife;
        uint16_t color;
    };

    std::vector<Ball> balls;
    std::vector<Particle> particles; 
    Ball shooter; 

    float cueAngle = -M_PI / 2;
    float power = 0;
    bool gameOver = false;
    bool gameStarted = false;
    bool sdPresent = false;

    int score = 0;

    const unsigned long GAME_DURATION = 180000; 
    unsigned long gameStartTime = 0;
    unsigned long remainingTime = GAME_DURATION;
    unsigned long volumeResetTick = 0; 
    unsigned long lastPhysicsTime = 0;
    unsigned long gameOverTimer = 0; 

    struct HighScoreEntry { char name[4]; int score; };
    HighScoreEntry highScores[5];
    
    // --- New Standardized High Score Variables ---
    char newName[4] = "A__";
    int charIndex = 0;
    int tempScore = 0;
    bool hsNeedsRedraw = true;

    int keyCooldown = 0; 
    int shootCooldown = 0; 

    // Replaced boolean 'enteringName' with a dedicated menu state to match RockADV/FallTris flow
    enum MenuState { TITLE, LEADERBOARD, HELP, REMAP, HIGHSCORE_INPUT, PLAYING_STATE };
    MenuState currentMenu = TITLE;
    unsigned long lastMenuSwitch = 0;

    uint16_t levelColors[] = {0xAD55, 0xFD20, 0xF81F, 0xFFE0, 0x001F, 0xF800, 0x07E0, 0x7FFF};

    // --- Forward Declarations ---
    void updatePhysics();
    void draw();

    // --- SD Config Functions ---
    void loadKeys() {
        if (!sdPresent) return;
        File f = SD.open("/CasualADV/puzzleball.ini", FILE_READ);
        if (f) {
            String s = f.readStringUntil('\n');
            if (s.length() >= 5) {
                keyAL = s[0]; keyAR = s[1]; keyPUp = s[2]; keyPDn = s[3]; keySh = s[4];
            }
            f.close();
        }
    }

    void saveKeys() {
        if (!sdPresent) return;
        if (!SD.exists("/CasualADV")) SD.mkdir("/CasualADV");
        File f = SD.open("/CasualADV/puzzleball.ini", FILE_WRITE);
        if (f) {
            f.printf("%c%c%c%c%c\n", keyAL, keyAR, keyPUp, keyPDn, keySh);
            f.close();
        }
    }

    void initSD() {
        for (int i = 0; i < 5; i++) { strcpy(highScores[i].name, "---"); highScores[i].score = 0; }
        if (SD.cardType() != CARD_NONE) { 
            sdPresent = true;
            if (!SD.exists("/CasualADV")) SD.mkdir("/CasualADV");
            loadKeys();
            File file = SD.open("/CasualADV/puzzleball.high", FILE_READ);
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
        } else { sdPresent = false; }
    }

    void saveScoreToSD() {
        if (!sdPresent) return;
        if (!SD.exists("/CasualADV")) SD.mkdir("/CasualADV");
        File file = SD.open("/CasualADV/puzzleball.high", FILE_WRITE);
        if (file) { 
            for (int i = 0; i < 5; i++) file.printf("%s,%d\n", highScores[i].name, highScores[i].score);
            file.close(); 
        }
    }

    // --- Standardized High Score Input ---
    void startHighScoreEntry(int newScore) {
        tempScore = newScore;
        strcpy(newName, "A__");
        charIndex = 0;
        currentMenu = HIGHSCORE_INPUT;
        hsNeedsRedraw = true; 
        while (M5Cardputer.Keyboard.isPressed()) { M5Cardputer.update(); delay(10); }
    }

    void transitionToMenu() {
        canvas->fillScreen(TFT_BLACK);
        canvas->pushSprite(0, 0);
        delay(150); 
        currentMenu = TITLE;
        lastMenuSwitch = millis();
    }

    void handleHighScoreInput() {
        if (hsNeedsRedraw) {
            canvas->fillSprite(TFT_BLACK);
            canvas->setTextColor(TFT_CYAN);
            canvas->setTextSize(2);
            canvas->setCursor(20, 30);
            canvas->print("NEW HIGH SCORE!");
            
            canvas->setTextColor(TFT_WHITE);
            canvas->setTextSize(1);
            canvas->setCursor(75, 60);
            canvas->printf("SCORE: %06d", tempScore);
            
            canvas->setTextSize(3);
            for (int i = 0; i < 3; i++) {
                if (i == charIndex) canvas->setTextColor(TFT_GREEN); 
                else canvas->setTextColor(TFT_YELLOW); 
                canvas->setCursor(85 + (i * 24), 90);
                canvas->printf("%c", newName[i]);
            }
            
            canvas->setTextColor(TFT_GREEN);
            canvas->setTextSize(1);
            canvas->setCursor(15, 125);
            // Uses custom PB keys for UI navigation instructions
            canvas->print(String((char)toupper(keyPUp)) + "/" + String((char)toupper(keyPDn)) + ":Letter " + String((char)toupper(keyAL)) + "/" + String((char)toupper(keyAR)) + ":Move B:Save");
            
            canvas->pushSprite(0, 0);
            hsNeedsRedraw = false; 
        }
        
        if (M5Cardputer.Keyboard.isChange() && M5Cardputer.Keyboard.isPressed()) {
            Keyboard_Class::KeysState s = M5Cardputer.Keyboard.keysState();
            for (auto c : s.word) {
                char lowerC = tolower(c);
                if (lowerC == keyPUp) { // Use Power Up for next letter
                    if (newName[charIndex] == '_' || newName[charIndex] == 'Z') newName[charIndex] = 'A';
                    else newName[charIndex]++;
                    hsNeedsRedraw = true;
                }
                else if (lowerC == keyPDn) { // Use Power Down for previous letter
                    if (newName[charIndex] == '_' || newName[charIndex] == 'A') newName[charIndex] = 'Z';
                    else newName[charIndex]--;
                    hsNeedsRedraw = true;
                }
                else if (lowerC == keyAL && charIndex > 0) { charIndex--; hsNeedsRedraw = true; } // Left
                else if (lowerC == keyAR && charIndex < 2) { // Right
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
                    saveScoreToSD(); 
                    M5Cardputer.Speaker.tone(1000, 200); delay(300); 
                    transitionToMenu(); 
                }
            }
        }
    }

    void checkAndSaveHighScore(int score) {
        if (score > highScores[4].score) {
            if (sdPresent) startHighScoreEntry(score);
            else {
                canvas->fillSprite(TFT_BLACK);
                canvas->setTextColor(TFT_RED);
                canvas->setTextSize(2);
                canvas->setCursor(35, 50);
                canvas->print("SD NOT PRESENT");
                canvas->setTextSize(1);
                canvas->setTextColor(TFT_WHITE);
                canvas->setCursor(60, 80);
                canvas->printf("Score: %06d", score);
                canvas->pushSprite(0, 0);
                M5Cardputer.Speaker.tone(150, 600);
                delay(2500); 
                transitionToMenu(); 
            }
        } else { transitionToMenu(); }
    }


    // --- Graphics Utilities ---
    uint16_t darkenColor(uint16_t color, float factor) {
        uint16_t r = ((color >> 11) & 0x1F) * factor;
        uint16_t g = ((color >> 5) & 0x3F) * factor;
        uint16_t b = (color & 0x1F) * factor;
        return (r << 11) | (g << 5) | b;
    }

    void spawnExplosion(float x, float y, uint16_t color) {
        for (int i = 0; i < 15; i++) {
            Particle p;
            p.x = x; p.y = y;
            float angle = random(0, 360) * M_PI / 180.0;
            float speed = random(10, 35) / 10.0;
            p.vx = cos(angle) * speed; p.vy = sin(angle) * speed;
            p.maxLife = random(10, 25); p.life = p.maxLife; p.color = color;
            particles.push_back(p);
        }
    }

    void updateParticles() {
        for (int i = particles.size() - 1; i >= 0; i--) {
            particles[i].x += particles[i].vx;
            particles[i].y += particles[i].vy;
            particles[i].life--;
            if (particles[i].life <= 0) particles.erase(particles.begin() + i);
        }
    }

    void drawPixelBall(int x, int y, uint16_t color, int flashTimer) {
        int r = BALL_RADIUS;
        canvas->fillCircle(x, y, r - 1, color);
        uint16_t outlineColor = (flashTimer > 0) ? TFT_YELLOW : COLOR_BALL_OUTLINE;
        canvas->drawCircle(x, y, r, outlineColor);
        canvas->drawCircle(x, y, r - 1, outlineColor);
        canvas->fillRect(x - r/2, y - r/2, 2, 2, COLOR_HIGHLIGHT);
    }

    void drawPixelBackground() {
        int tileSize = 20;
        int w = canvas->width();
        int h = canvas->height();
        canvas->fillScreen(COLOR_BG_DARK);
        for (int i = 0; i < w; i += tileSize) canvas->drawFastVLine(i, 0, h, COLOR_TILE_LINE);
        for (int j = 0; j < h; j += tileSize) canvas->drawFastHLine(0, j, w, COLOR_TILE_LINE);
    }

    void spawnShooter() {
        shooter.x = canvas->width() / 2;
        shooter.y = canvas->height() - 25; 
        shooter.vx = 0; shooter.vy = 0;
        shooter.level = 0; shooter.color = levelColors[0];
        shooter.active = true; shooter.trail.clear(); shooter.flashTimer = 0;
    }

    void updatePhysics() {
        if (!gameStarted || gameOver || currentMenu == HIGHSCORE_INPUT) {
            updateParticles(); 
            lastPhysicsTime = millis();
            return;
        }

        unsigned long now = millis();
        int dt = now - lastPhysicsTime;
        lastPhysicsTime = now;
        updateParticles();

        unsigned long elapsed = now - gameStartTime;
        bool timeUp = false;
        if (elapsed >= GAME_DURATION) { remainingTime = 0; timeUp = true; } 
        else { remainingTime = GAME_DURATION - elapsed; }

        if (shooter.flashTimer > 0) {
            shooter.flashTimer -= dt;
            if (shooter.flashTimer < 0) shooter.flashTimer = 0;
        }

        for (size_t i = 0; i < balls.size(); i++) {
            if (!balls[i].active) continue;

            if (balls[i].flashTimer > 0) {
                balls[i].flashTimer -= dt;
                if (balls[i].flashTimer < 0) balls[i].flashTimer = 0;
            }

            if (abs(balls[i].vx) > 0.2 || abs(balls[i].vy) > 0.2) {
                balls[i].trail.push_back({balls[i].x, balls[i].y});
                if (balls[i].trail.size() > TRAIL_LENGTH) balls[i].trail.erase(balls[i].trail.begin());
            } else if (!balls[i].trail.empty()) {
                balls[i].trail.erase(balls[i].trail.begin());
            }

            balls[i].x += balls[i].vx; balls[i].y += balls[i].vy;
            balls[i].vx *= FRICTION; balls[i].vy *= FRICTION;

            bool bounced = false;
            if (balls[i].x < BALL_RADIUS) { balls[i].x = BALL_RADIUS; balls[i].vx *= -1; bounced = true; }
            if (balls[i].x > canvas->width() - BALL_RADIUS) { balls[i].x = canvas->width() - BALL_RADIUS; balls[i].vx *= -1; bounced = true; }
            if (balls[i].y < BALL_RADIUS) { balls[i].y = BALL_RADIUS; balls[i].vy *= -1; bounced = true; }
            if (balls[i].y > canvas->height() - BALL_RADIUS - 20) { balls[i].y = canvas->height() - BALL_RADIUS - 20; balls[i].vy *= -1; bounced = true; }
            
            if (bounced && (abs(balls[i].vx) > 0.5 || abs(balls[i].vy) > 0.5)) {
                M5Cardputer.Speaker.tone(600, 20);
                balls[i].flashTimer = 100; 
            }

            if (abs(balls[i].vx) < 0.15 && abs(balls[i].vy) < 0.15) { balls[i].vx = 0; balls[i].vy = 0; }
        }

        for (size_t i = 0; i < balls.size(); i++) {
            for (size_t j = i + 1; j < balls.size(); ) {
                float dx = balls[j].x - balls[i].x;
                float dy = balls[j].y - balls[i].y;
                float dist = sqrt(dx*dx + dy*dy);

                if (dist < BALL_RADIUS * 2) {
                    float overlap = (BALL_RADIUS * 2) - dist;
                    float angle = atan2(dy, dx);
                    balls[i].x -= cos(angle) * (overlap / 2.1); balls[i].y -= sin(angle) * (overlap / 2.1);
                    balls[j].x += cos(angle) * (overlap / 2.1); balls[j].y += sin(angle) * (overlap / 2.1);

                    balls[i].flashTimer = 100; balls[j].flashTimer = 100;

                    if (balls[i].level == balls[j].level) {
                        balls[i].level++;
                        balls[i].color = levelColors[balls[i].level % 8];
                        spawnExplosion(balls[i].x, balls[i].y, balls[i].color);

                        balls[i].vx = (balls[i].vx + balls[j].vx) / 2; balls[i].vy = (balls[i].vy + balls[j].vy) / 2;
                        balls[i].trail.clear(); 
                        balls.erase(balls.begin() + j);
                        
                        score += (balls[i].level * 10);
                        M5Cardputer.Speaker.setVolume(100); 
                        M5Cardputer.Speaker.tone(1500, 80);
                        volumeResetTick = millis() + 80; 
                    } else {
                        float nx = dx / dist; float ny = dy / dist;
                        float k = balls[i].vx * nx + balls[i].vy * ny - balls[j].vx * nx - balls[j].vy * ny;
                        balls[i].vx -= k * nx; balls[i].vy -= k * ny;
                        balls[j].vx += k * nx; balls[j].vy += k * ny;
                        M5Cardputer.Speaker.tone(800, 30);
                        j++;
                    }
                } else j++;
            }
        }
        
        if ((balls.size() > 18 || timeUp) && !gameOver) {
            gameOver = true;
            gameOverTimer = millis(); 
        }
    }

    void draw() {
        if (currentMenu == REMAP && !gameStarted && currentMenu != HIGHSCORE_INPUT) {
            if (remapNeedsRedraw) {
                drawPixelBackground();
                canvas->fillRoundRect(20, 20, 200, 95, 8, COLOR_UI_BAR);
                canvas->drawRoundRect(20, 20, 200, 95, 8, COLOR_UI_BORDER);
                
                canvas->setTextColor(TFT_CYAN);
                canvas->drawCenterString("REMAP CONTROLS", canvas->width()/2, 30);
                
                canvas->setTextColor(TFT_WHITE);
                canvas->drawCenterString("Press key for:", canvas->width()/2, 50);
                
                canvas->setTextColor(TFT_YELLOW);
                canvas->setTextSize(1.5);
                canvas->drawCenterString(remapNames[remapStep], canvas->width()/2, 70);
                canvas->setTextSize(1);
                
                canvas->setTextColor(COLOR_TILE_LINE);
                canvas->drawCenterString("(V and B are reserved)", canvas->width()/2, 95);

                canvas->pushSprite(0, 0); 
                remapNeedsRedraw = false;
            }
            return;
        }

        if (currentMenu == HIGHSCORE_INPUT) {
            // Handled within handleHighScoreInput directly onto canvas
            return; 
        }

        drawPixelBackground();

        if (!gameStarted) {
            if (currentMenu == TITLE) {
                int imgW = 180; int imgH = 71;
                int imgX = (canvas->width() - imgW) / 2; int imgY = 15; 
                
                canvas->setSwapBytes(true);
                canvas->pushImage(imgX, imgY, imgW, imgH, puzzleball_logo);
                canvas->setSwapBytes(false); 

                canvas->setTextColor(TFT_WHITE);
                canvas->drawCenterString("B:Play | K:Help | ESC:Remap", canvas->width()/2, 100);
                
                if (!sdPresent) {
                    canvas->setTextColor(TFT_RED);
                    canvas->drawCenterString("SD Warning: Scores won't be saved", canvas->width()/2, 115);
                }
                
                if (millis() - lastMenuSwitch > 3000) {
                    currentMenu = LEADERBOARD; lastMenuSwitch = millis();
                }

            } else if (currentMenu == LEADERBOARD) {
                int imgW = 180; int imgH = 70;
                int imgX = (canvas->width() - imgW) / 2; int imgY = 2; 
                
                canvas->setSwapBytes(true);
                canvas->pushImage(imgX, imgY, imgW, imgH, highscores_logo);
                canvas->setSwapBytes(false); 
                
                canvas->fillRoundRect(30, 75, 180, 56, 6, COLOR_UI_BAR);
                canvas->drawRoundRect(30, 75, 180, 56, 6, COLOR_UI_BORDER);

                canvas->setTextColor(TFT_WHITE);
                for (int i = 0; i < 5; i++) {
                    char hsStr[30];
                    sprintf(hsStr, "%d. %s : %06d", i + 1, highScores[i].name, highScores[i].score);
                    canvas->drawString(hsStr, 60, 80 + (i * 10)); 
                }
                
                if (millis() - lastMenuSwitch > 3000) {
                    currentMenu = TITLE; lastMenuSwitch = millis();
                }

            } else if (currentMenu == HELP) {
                canvas->fillRoundRect(10, 10, 220, 115, 8, COLOR_UI_BAR);
                canvas->drawRoundRect(10, 10, 220, 115, 8, COLOR_UI_BORDER);
                
                canvas->setTextColor(TFT_YELLOW);
                canvas->drawCenterString("HOW TO PLAY", canvas->width()/2, 15);
                
                canvas->setTextColor(TFT_WHITE);
                char buf[40];
                sprintf(buf, "%c / %c : Aim the cue", toupper(keyAL), toupper(keyAR)); canvas->drawString(buf, 20, 35);
                sprintf(buf, "%c : Charge Power", toupper(keyPUp)); canvas->drawString(buf, 20, 50);
                sprintf(buf, "%c : Decrease Power", toupper(keyPDn)); canvas->drawString(buf, 20, 65);
                sprintf(buf, "%c : Shoot the ball", toupper(keySh)); canvas->drawString(buf, 20, 80);
                canvas->drawString("Match colors to merge! Limit: 18", 20, 95);
                
                canvas->setTextColor(COLOR_TILE_LINE);
                canvas->drawCenterString("Press K to Return", canvas->width()/2, 110);
            }
            
            canvas->pushSprite(0, 0); 
            return;
        }

        if (!gameOver) {
            float lx = shooter.x; float ly = shooter.y;
            float dx = cos(cueAngle) * 4; float dy = sin(cueAngle) * 4;
            for(int i=0; i<10; i++) { 
                canvas->fillRect(lx-1, ly-1, 2, 2, TFT_YELLOW); 
                lx += dx; ly += dy;
            }
        }

        for (const auto& b : balls) {
            if (b.trail.size() < 2) continue;
            for (size_t t = 0; t < b.trail.size(); t++) {
                float fadeFactor = 0.1 + ((float)t / b.trail.size()) * 0.6;
                uint16_t fadedColor = darkenColor(b.color, fadeFactor);
                int trailRadius = (BALL_RADIUS - 2) * ((float)t / b.trail.size());
                if (trailRadius < 1) trailRadius = 1;
                canvas->fillCircle(b.trail[t].x, b.trail[t].y, trailRadius, fadedColor);
            }
        }

        for (const auto& b : balls) drawPixelBall(b.x, b.y, b.color, b.flashTimer);
        if (!gameOver) drawPixelBall(shooter.x, shooter.y, shooter.color, shooter.flashTimer);

        for (const auto& p : particles) {
            int pSize = (p.life > p.maxLife / 2) ? 2 : 1;
            canvas->fillRect(p.x, p.y, pSize, pSize, p.color);
        }

        int mins = remainingTime / 60000; int secs = (remainingTime % 60000) / 1000;
        char timeStr[10]; sprintf(timeStr, "%02d:%02d", mins, secs);
        
        canvas->fillRoundRect(canvas->width() - 48, 4, 44, 16, 3, COLOR_UI_BAR);
        canvas->drawRoundRect(canvas->width() - 48, 4, 44, 16, 3, COLOR_UI_BORDER);
        canvas->setTextColor(TFT_WHITE); canvas->drawString(timeStr, canvas->width() - 44, 8);

        int uiHeight = 20; int uiY = canvas->height() - uiHeight;
        canvas->fillRect(0, uiY, canvas->width(), uiHeight, COLOR_UI_BAR);
        canvas->drawFastHLine(0, uiY, canvas->width(), COLOR_UI_BORDER);

        int powerBarX = canvas->width() / 2 - (MAX_POWER * 2); int powerBarY = uiY + 5;
        int powerBarW = MAX_POWER * 4; int powerBarH = 8;
        canvas->drawRect(powerBarX - 1, powerBarY - 1, powerBarW + 2, powerBarH + 2, COLOR_UI_BORDER);
        canvas->fillRect(powerBarX, powerBarY, powerBarW, powerBarH, COLOR_BG_DARK); 
        if (power > 0) canvas->fillRect(powerBarX, powerBarY, power * 4, powerBarH, COLOR_POWER_BAR);
        
        canvas->setTextColor(TFT_BLACK); canvas->setTextSize(1.5); 
        canvas->setCursor(5, uiY + 4); canvas->printf("S:%05d", score); 
        canvas->setCursor(canvas->width() - 85, uiY + 4); canvas->printf("H:%05d", highScores[0].score); 
        canvas->setTextSize(1); 

        if (gameOver) {
            canvas->fillRoundRect(30, 40, 180, 60, 8, COLOR_UI_BAR);
            canvas->drawRoundRect(30, 40, 180, 60, 8, COLOR_UI_BORDER);
            canvas->setTextColor(TFT_WHITE);
            if (remainingTime == 0) canvas->drawCenterString("TIME'S UP!", canvas->width()/2, 55);
            else canvas->drawCenterString("GAME OVER", canvas->width()/2, 55);
        }
        canvas->pushSprite(0, 0); 
    }
} 

void setupPuzzleBall() {
    PB::gameStarted = false;
    PB::gameOver = false;
    PB::currentMenu = PB::TITLE;
    PB::score = 0;
    PB::power = 0;
    PB::cueAngle = -M_PI / 2;
    PB::balls.clear();
    PB::particles.clear();
    PB::lastPhysicsTime = millis();
    PB::volumeResetTick = 0; 

    if (PB::canvas == nullptr) {
        PB::canvas = new LGFX_Sprite(&M5Cardputer.Display);
        PB::canvas->setColorDepth(16);
        PB::canvas->createSprite(M5Cardputer.Display.width(), M5Cardputer.Display.height());
    }

    PB::initSD();
    PB::lastMenuSwitch = millis();
}

bool loopPuzzleBall() {
    M5Cardputer.update();
    unsigned long now = millis();

    if (PB::volumeResetTick > 0 && now >= PB::volumeResetTick) {
        M5Cardputer.Speaker.setVolume(230); 
        PB::volumeResetTick = 0;
    }
    
    if (PB::keyCooldown > 0) PB::keyCooldown--;
    if (PB::shootCooldown > 0) PB::shootCooldown--;

    if (PB::currentMenu == PB::HIGHSCORE_INPUT) {
        PB::handleHighScoreInput();
        return true; 
    }

    if (!PB::gameStarted) {
        if (PB::currentMenu == PB::REMAP) {
            if (M5Cardputer.Keyboard.isChange() && M5Cardputer.Keyboard.isPressed() && PB::keyCooldown == 0) {
                Keyboard_Class::KeysState s = M5Cardputer.Keyboard.keysState();
                for (auto c : s.word) {
                    char lowerC = tolower(c);
                    if (lowerC == 'v' || lowerC == 'b') {
                        M5Cardputer.Speaker.tone(200, 100);
                        PB::keyCooldown = 15;
                        return true;
                    }
                    PB::tempKeys[PB::remapStep] = lowerC;
                    M5Cardputer.Speaker.tone(1000, 50);
                    PB::remapStep++;
                    PB::remapNeedsRedraw = true;
                    
                    if (PB::remapStep >= 5) {
                        PB::keyAL = PB::tempKeys[0]; PB::keyAR = PB::tempKeys[1];
                        PB::keyPUp = PB::tempKeys[2]; PB::keyPDn = PB::tempKeys[3];
                        PB::keySh = PB::tempKeys[4];
                        PB::saveKeys();
                        PB::currentMenu = PB::TITLE;
                        PB::lastMenuSwitch = millis();
                    }
                    PB::keyCooldown = 10;
                }
            }
        } else {
            if (M5Cardputer.Keyboard.isKeyPressed('k') && PB::keyCooldown == 0) {
                if (PB::currentMenu == PB::HELP) { PB::currentMenu = PB::TITLE; PB::lastMenuSwitch = millis(); } 
                else { PB::currentMenu = PB::HELP; }
                PB::keyCooldown = 15;
            }
            if (M5Cardputer.Keyboard.isKeyPressed('`') && PB::keyCooldown == 0) {
                PB::remapStep = 0;
                PB::remapNeedsRedraw = true;
                PB::currentMenu = PB::REMAP;
                PB::keyCooldown = 15;
                while(M5Cardputer.Keyboard.isPressed()) { M5Cardputer.update(); delay(10); }
            }
        }
    }

    if (PB::gameOver) {
        if (millis() - PB::gameOverTimer > 3000) {
            PB::checkAndSaveHighScore(PB::score);
            PB::balls.clear(); PB::particles.clear(); PB::score = 0; PB::power = 0;
            PB::gameStarted = false; PB::gameOver = false;
            return true;
        }
    }

    if (PB::currentMenu != PB::REMAP && PB::currentMenu != PB::HIGHSCORE_INPUT) {
        if (M5Cardputer.Keyboard.isKeyPressed('b') && (!PB::gameStarted || PB::gameOver)) {
            PB::balls.clear(); PB::particles.clear(); PB::spawnShooter(); 
            PB::score = 0; PB::power = 0; PB::cueAngle = -M_PI / 2;
            PB::gameOver = false; PB::gameStarted = true;
            PB::currentMenu = PB::PLAYING_STATE;
            PB::gameStartTime = millis(); PB::remainingTime = PB::GAME_DURATION;
        }

        if (M5Cardputer.Keyboard.isKeyPressed('v') && PB::keyCooldown == 0) {
            if (PB::gameStarted) {
                PB::checkAndSaveHighScore(PB::score);
                PB::balls.clear(); PB::particles.clear(); PB::score = 0; PB::power = 0;
                PB::gameStarted = false; PB::gameOver = false; 
                PB::keyCooldown = 20; 
            } else {
                PB::canvas->deleteSprite(); delete PB::canvas; PB::canvas = nullptr;
                M5Cardputer.Speaker.setVolume(230);
                return false; 
            }
        }

        if (PB::gameStarted && !PB::gameOver) {
            if (M5Cardputer.Keyboard.isKeyPressed(PB::keyAL)) PB::cueAngle -= 0.05;
            if (M5Cardputer.Keyboard.isKeyPressed(PB::keyAR)) PB::cueAngle += 0.05;
            
            if (M5Cardputer.Keyboard.isKeyPressed(PB::keyPUp) && PB::shootCooldown == 0) {
                PB::power += 0.4; if (PB::power > PB::MAX_POWER) PB::power = PB::MAX_POWER;
            }
            if (M5Cardputer.Keyboard.isKeyPressed(PB::keyPDn) && PB::shootCooldown == 0) {
                PB::power -= 0.4; if (PB::power < 0) PB::power = 0;
            }
            if (M5Cardputer.Keyboard.isKeyPressed(PB::keySh) && PB::power > 0 && PB::shootCooldown == 0) {
                PB::shooter.vx = cos(PB::cueAngle) * PB::power; PB::shooter.vy = sin(PB::cueAngle) * PB::power;
                PB::balls.push_back(PB::shooter); 
                M5Cardputer.Speaker.tone(400, 50); PB::power = 0; PB::spawnShooter(); PB::shootCooldown = 25;
            }
        }
    }

    PB::updatePhysics();
    PB::draw();
    delay(20);
    return true; 
}