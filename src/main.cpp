// All comments in the code must always be in English.
#include <M5Cardputer.h>
#include <M5Unified.h>
#include <SPI.h>
#include <SD.h>
#include <math.h> 
#include <vector>

#include "FallTris.h"
#include "RockADV.h"
#include "PuzzleBall.h"
#include "CasualADV_logo.h"
#include "AudioTask.h" 

enum GlobalState { MAIN_MENU, APP_FALLTRIS, APP_ROCKADV, APP_PUZZLEBALL, REMAP_INPUT, MP3_OPTIONS, FILE_MANAGER };
GlobalState currentState = MAIN_MENU;

const int NUM_GAMES = 3;
int selectedGame = 0;
String gameNames[] = {"FallTris", "RockADV", "PuzzleBall"};

char keyPrev = 'e';
char keyNext = 'z';
char keySel = 'p';
int remapStep = 0;
char tempKeys[3];
const char* remapNames[] = {"PREV GAME", "NEXT GAME", "SELECT"};
bool remapNeedsRedraw = true;

LGFX_Sprite* menuCanvas = nullptr; 

unsigned long lastInputTime = 0;
unsigned long attractStartTime = 0;
const unsigned long IDLE_TIMEOUT = 6000;      
const unsigned long ATTRACT_DURATION = 8000;  
bool inAttractMode = false;
float time_counter = 0;
const float speed_step = 0.15f; 

const int FIRE_WIDTH = 240;
const int FIRE_HEIGHT = 90; 
uint8_t fireScreen[(FIRE_WIDTH * (FIRE_HEIGHT + 2)) + 2] = {0};
uint16_t firePalette[256];

bool sdPresent = false;
unsigned long menuCooldownTime = 0; 
unsigned long lastFireUpdate = 0;

// MP3 Menus variables
int mp3OptCursor = 0;
int fmCursor = 0;
int fmScroll = 0;
std::vector<String> availableFiles;
std::vector<String> newPlaylist;

void loadKeys() {
    if (!sdPresent) return;
    if (xSemaphoreTake(sdMutex, portMAX_DELAY)) {
        File f = SD.open("/CasualADV/main.ini", FILE_READ);
        if (f) {
            String s = f.readStringUntil('\n');
            if (s.length() >= 3) {
                keyPrev = s[0]; keyNext = s[1]; keySel = s[2];
                if (s.length() > 3) {
                    int commaIdx = s.indexOf(',', 3);
                    if (commaIdx > 3) {
                        AudioTask::mp3Enabled = (s.substring(3, commaIdx).toInt() == 1);
                        AudioTask::mp3Volume = s.substring(commaIdx + 1).toInt();
                    }
                }
            }
            f.close();
        }
        xSemaphoreGive(sdMutex);
    }
}

void saveKeys() {
    if (!sdPresent) return;
    
    // NOTE: AudioTask::stop() is removed here! 
    // The Mutex handles SD concurrency gracefully, so music won't stop while saving!
    
    if (xSemaphoreTake(sdMutex, portMAX_DELAY)) {
        if (!SD.exists("/CasualADV")) SD.mkdir("/CasualADV");
        File f = SD.open("/CasualADV/main.ini", FILE_WRITE);
        if (f) {
            f.printf("%c%c%c%d,%d\n", keyPrev, keyNext, keySel, AudioTask::mp3Enabled ? 1 : 0, AudioTask::mp3Volume);
            f.close();
        }
        xSemaphoreGive(sdMutex);
    }
}

void readMusicDir() {
    availableFiles.clear();
    if (xSemaphoreTake(sdMutex, portMAX_DELAY)) {
        File dir = SD.open("/CasualADV/music");
        if (dir && dir.isDirectory()) {
            File file = dir.openNextFile();
            while (file) {
                if (!file.isDirectory()) {
                    String name = file.name();
                    name.toLowerCase();
                    if (name.endsWith(".mp3")) availableFiles.push_back(String(file.name()));
                }
                file = dir.openNextFile();
            }
        }
        xSemaphoreGive(sdMutex);
    }
}

void initFire() {
    for (int v = 0; v < 256; v++) {
        uint8_t r = (v * 2 > 255) ? 255 : (v * 2);       
        uint8_t g = (v > 128) ? (v - 128) * 2 : 0;       
        uint8_t b = (v > 192) ? (v - 192) * 4 : 0;       
        firePalette[v] = menuCanvas->color565(r, g, b);
    }
    memset(fireScreen, 0, sizeof(fireScreen));
}

void updateFire() {
    int pA = 2; 
    for (int y = 1; y < FIRE_HEIGHT; y++) {
        int row = y * FIRE_WIDTH;
        int next_row = (y + 1) * FIRE_WIDTH;
        for (int x = 1; x < FIRE_WIDTH - 1; x++) {
            int val = (fireScreen[next_row + x] + fireScreen[next_row + x - 1] + fireScreen[next_row + x + 1] + fireScreen[next_row + FIRE_WIDTH + x]) >> 2;
            if (val > pA) val -= pA; else val = 0;
            fireScreen[row + x] = val;
        }
    }
    int last_row = (FIRE_HEIGHT - 1) * FIRE_WIDTH;
    for (int x = 0; x < FIRE_WIDTH; x++) {
        fireScreen[last_row + x] = (random(100) > 50) ? 255 : 0;
    }
}

void setup() {
    auto cfg = M5.config();
    M5Cardputer.begin(cfg, true);
    M5Cardputer.Display.setRotation(1);
    
    // Master volume controls SFX. MP3 uses independent gain.
    M5Cardputer.Speaker.setVolume(200); 

    AudioTask::begin();

    SPI.begin(40, 39, 14, 12); 
    if (SD.begin(12, SPI, 15000000)) {
        sdPresent = true;
        if (xSemaphoreTake(sdMutex, portMAX_DELAY)) {
            if (!SD.exists("/CasualADV")) SD.mkdir("/CasualADV"); 
            if (!SD.exists("/CasualADV/music")) SD.mkdir("/CasualADV/music"); 
            xSemaphoreGive(sdMutex);
        }
        loadKeys();
    }

    AudioTask::loadPlaylist();
    AudioTask::setMP3Volume(AudioTask::mp3Volume);
    if (AudioTask::mp3Enabled) AudioTask::startPlaylist();

    menuCanvas = new LGFX_Sprite(&M5Cardputer.Display);
    menuCanvas->setColorDepth(16);
    menuCanvas->createSprite(240, 135);

    initFire(); 
    lastInputTime = millis();
}

void drawRotozoom() {
    if (menuCanvas == nullptr) return;
    float angle = time_counter * 1.5f; 
    float scale = 1.0f + 0.5f * sinf(time_counter);
    float s = sinf(angle) * scale;
    float c = cosf(angle) * scale;
    int cx = 120 + (int)(50.0f * sinf(time_counter * 0.7f));
    
    for (int y = 0; y < 135; y++) {
        for (int x = 0; x < 240; x++) {
            int dx = x - cx; int dy = y - 67;
            int u = (int)(dx * c - dy * s); int v = (int)(dx * s + dy * c);
            int imgX = (u + 90) % 180; if (imgX < 0) imgX += 180; 
            int imgY = (v + 35) % 70; if (imgY < 0) imgY += 70; 
            menuCanvas->drawPixel(x, y, casualadv_logo[imgY * 180 + imgX]);
        }
    }
    menuCanvas->pushSprite(0, 0); 
}

void drawMainMenu() {
    menuCanvas->fillScreen(TFT_BLACK); 
    int yStart = menuCanvas->height() - FIRE_HEIGHT;
    for(int y = 1; y < FIRE_HEIGHT; y++) {
        int row = y * FIRE_WIDTH;
        for(int x = 1; x < FIRE_WIDTH - 1; x++) {
            uint8_t val = fireScreen[row + x];
            if (val > 0) menuCanvas->drawPixel(x, yStart + y, firePalette[val]);
        }
    }
    
    menuCanvas->setSwapBytes(true);
    menuCanvas->pushImage(30, 5, 180, 70, casualadv_logo);
    menuCanvas->setSwapBytes(false);

    menuCanvas->setFont(&fonts::Font2);
    menuCanvas->setTextSize(1); 

    for(int i = 0; i < NUM_GAMES; i++) {
        int yPos = 85 + (i * 15);
        if (i == selectedGame) {
            menuCanvas->setTextColor(TFT_YELLOW, TFT_BLACK); 
            menuCanvas->drawCenterString("> " + gameNames[i] + " <", 120, yPos);
        } else {
            menuCanvas->setTextColor(TFT_WHITE, TFT_BLACK);  
            menuCanvas->drawCenterString(gameNames[i], 120, yPos);
        }
    }
    menuCanvas->pushSprite(0, 0); 
}

void drawMP3Options() {
    menuCanvas->fillSprite(TFT_BLACK);
    menuCanvas->setTextColor(TFT_CYAN);
    menuCanvas->setTextSize(1.5);
    menuCanvas->drawCenterString("MP3 SETTINGS", 120, 10);
    
    menuCanvas->setTextSize(1);
    for (int i = 0; i < 4; i++) {
        int yPos = 40 + i*16; // Shifted up to make room
        if (i == mp3OptCursor) menuCanvas->setTextColor(TFT_YELLOW);
        else menuCanvas->setTextColor(TFT_WHITE);
        
        String text = "";
        if (i == 0) text = String("Playback: ") + (AudioTask::mp3Enabled ? "ON" : "OFF");
        else if (i == 1) text = String("MP3 Volume: ") + String((int)((AudioTask::mp3Volume/255.0f)*100)) + "%";
        else if (i == 2) text = "Edit Playlist";
        else if (i == 3) text = "Back & Save";
        
        menuCanvas->drawCenterString(i == mp3OptCursor ? "> " + text + " <" : text, 120, yPos);
    }

    // Draw the "Now Playing" footer
    menuCanvas->drawFastHLine(0, 110, 240, TFT_DARKGRAY);
    menuCanvas->setTextColor(TFT_GREEN);
    String currentTrack = (AudioTask::mp3Enabled && AudioTask::isPlaying()) ? AudioTask::getCurrentTrackName() : "Stopped";
    if (currentTrack.length() > 30) currentTrack = currentTrack.substring(0, 28) + "..";
    menuCanvas->drawCenterString("Playing: " + currentTrack, 120, 118);

    menuCanvas->pushSprite(0, 0);
}

void drawFileManager() {
    menuCanvas->fillScreen(TFT_BLACK);
    menuCanvas->setTextColor(TFT_CYAN);
    menuCanvas->setTextSize(1);
    menuCanvas->drawString("SELECT MP3s (Press V to Save)", 5, 5);
    
    menuCanvas->setTextColor(TFT_WHITE);
    int yOff = 25;
    for (int i = 0; i < 6; i++) {
        int idx = fmScroll + i;
        if (idx >= availableFiles.size()) break;
        
        if (idx == fmCursor) {
            menuCanvas->setTextColor(TFT_YELLOW);
            menuCanvas->drawString("> " + availableFiles[idx].substring(0,25), 5, yOff + i*15);
        } else {
            menuCanvas->setTextColor(TFT_WHITE);
            menuCanvas->drawString("  " + availableFiles[idx].substring(0,25), 5, yOff + i*15);
        }
    }
    menuCanvas->drawFastHLine(0, 115, 240, TFT_DARKGRAY);
    menuCanvas->setTextColor(TFT_GREEN);
    menuCanvas->drawString(String(newPlaylist.size()) + " tracks selected in playlist.", 5, 120);
    
    menuCanvas->pushSprite(0,0);
}

void handleFileManager() {
    if (M5Cardputer.Keyboard.isChange() && M5Cardputer.Keyboard.isPressed()) {
        Keyboard_Class::KeysState s = M5Cardputer.Keyboard.keysState();
        for (auto c : s.word) {
            char l = tolower(c);
            if (l == 'v') {
                AudioTask::savePlaylist(newPlaylist);
                if (AudioTask::mp3Enabled) AudioTask::startPlaylist(); // Instantly apply new playlist
                currentState = MP3_OPTIONS;
                M5Cardputer.Speaker.tone(1000, 50); delay(100);
                return;
            }
            else if (l == keyPrev) {
                fmCursor--; if (fmCursor < 0) fmCursor = max(0, (int)availableFiles.size() - 1);
                if (fmCursor < fmScroll) fmScroll = fmCursor;
                if (fmCursor >= fmScroll + 6) fmScroll = fmCursor - 5;
                M5Cardputer.Speaker.tone(1000, 20);
            }
            else if (l == keyNext) {
                fmCursor++; if (fmCursor >= availableFiles.size()) fmCursor = 0;
                if (fmCursor >= fmScroll + 6) fmScroll = fmCursor - 5;
                if (fmCursor < fmScroll) fmScroll = fmCursor;
                M5Cardputer.Speaker.tone(1000, 20);
            }
            else if (l == keySel) {
                if (availableFiles.size() > 0) {
                    newPlaylist.push_back(availableFiles[fmCursor]);
                    M5Cardputer.Speaker.tone(1500, 50);
                }
            }
        }
    }
    drawFileManager();
}

void launchGame(int gameIndex) {
    if (menuCanvas != nullptr) {
        menuCanvas->deleteSprite();
        delete menuCanvas;
        menuCanvas = nullptr;
    }
    
    // MP3 Task handles itself. We DO NOT stop it here if enabled!
    if (!AudioTask::mp3Enabled) AudioTask::stop(); 

    M5Cardputer.Display.fillScreen(TFT_BLACK);
    delay(150);

    if (gameIndex == 0) { currentState = APP_FALLTRIS; setupFallTris(); } 
    else if (gameIndex == 1) { currentState = APP_ROCKADV; setupRockADV(); } 
    else if (gameIndex == 2) { currentState = APP_PUZZLEBALL; setupPuzzleBall(); }
}

void loop() {
    M5Cardputer.update();

    if (currentState == MAIN_MENU || currentState == REMAP_INPUT || currentState == MP3_OPTIONS || currentState == FILE_MANAGER) {
        if (menuCanvas == nullptr) {
            M5Cardputer.Display.setRotation(1);
            menuCanvas = new LGFX_Sprite(&M5Cardputer.Display);
            menuCanvas->setColorDepth(16);
            menuCanvas->createSprite(240, 135);
            initFire();
        }

        if (currentState == FILE_MANAGER) { handleFileManager(); return; }
        
        if (currentState == MP3_OPTIONS) {
            if (M5Cardputer.Keyboard.isChange() && M5Cardputer.Keyboard.isPressed()) {
                Keyboard_Class::KeysState s = M5Cardputer.Keyboard.keysState();
                for (auto c : s.word) {
                    char l = tolower(c);
                    if (l == keyPrev) { mp3OptCursor--; if (mp3OptCursor < 0) mp3OptCursor = 3; M5Cardputer.Speaker.tone(1000, 20); } 
                    else if (l == keyNext) { mp3OptCursor++; if (mp3OptCursor > 3) mp3OptCursor = 0; M5Cardputer.Speaker.tone(1000, 20); } 
                    
                    // Allow skipping tracks directly in the menu using 'A', 'D' or the physical left/right arrows
                    else if (l == 'a' || l == ',') {
                        if (AudioTask::mp3Enabled) { AudioTask::prevTrack(); M5Cardputer.Speaker.tone(1000, 20); }
                    }
                    else if (l == 'd' || l == '/') {
                        if (AudioTask::mp3Enabled) { AudioTask::nextTrack(); M5Cardputer.Speaker.tone(1000, 20); }
                    }

                    else if (l == keySel) {
                        M5Cardputer.Speaker.tone(1500, 30);
                        if (mp3OptCursor == 0) {
                            AudioTask::mp3Enabled = !AudioTask::mp3Enabled;
                            if (AudioTask::mp3Enabled) {
                                if (!AudioTask::isPlaying()) AudioTask::startPlaylist(); 
                            } else {
                                AudioTask::stop();
                            }
                        } 
                        else if (mp3OptCursor == 1) {
                            int v = AudioTask::mp3Volume + 25; if (v > 255) v = 0;
                            AudioTask::mp3Volume = v;
                            AudioTask::setMP3Volume(AudioTask::mp3Volume);
                        } 
                        else if (mp3OptCursor == 2) {
                            currentState = FILE_MANAGER; readMusicDir(); newPlaylist.clear(); fmCursor = 0; fmScroll = 0;
                        } 
                        else if (mp3OptCursor == 3) {
                            saveKeys(); 
                            // Only force start if enabled but somehow not playing
                            if(AudioTask::mp3Enabled && !AudioTask::isPlaying()) AudioTask::startPlaylist(); 
                            currentState = MAIN_MENU; menuCooldownTime = millis() + 300;
                        }
                    }
                }
            }
            drawMP3Options(); return;
        }

        // --- Standard Main Menu Logic ---
        bool selectionChanged = false;

        if (inAttractMode) {
            if (M5Cardputer.Keyboard.isPressed()) { inAttractMode = false; lastInputTime = millis(); menuCooldownTime = millis() + 200; return; }
            if (millis() - attractStartTime > ATTRACT_DURATION) { inAttractMode = false; lastInputTime = millis(); drawMainMenu(); } 
            else { if (millis() - lastFireUpdate > 30) { time_counter += speed_step; drawRotozoom(); lastFireUpdate = millis(); } }
        } else {
            if (millis() - lastInputTime > IDLE_TIMEOUT) { inAttractMode = true; attractStartTime = millis(); time_counter = 0; }
            if (millis() > menuCooldownTime) {
                if (M5Cardputer.Keyboard.isKeyPressed('v')) {
                    currentState = MP3_OPTIONS; mp3OptCursor = 0; menuCooldownTime = millis() + 300; return;
                }
                if (M5Cardputer.Keyboard.isChange() && M5Cardputer.Keyboard.isPressed()) {
                    Keyboard_Class::KeysState s = M5Cardputer.Keyboard.keysState();
                    for (auto c : s.word) {
                        char lowerC = tolower(c);
                        if (lowerC == '`') { 
                            remapStep = 0; remapNeedsRedraw = true; currentState = REMAP_INPUT;
                            while(M5Cardputer.Keyboard.isPressed()) { M5Cardputer.update(); delay(10); } return;
                        } 
                        else if (lowerC == keyPrev) {
                            selectedGame--; if (selectedGame < 0) selectedGame = NUM_GAMES - 1;
                            M5Cardputer.Speaker.tone(1000, 20); selectionChanged = true; lastInputTime = millis();
                        } 
                        else if (lowerC == keyNext) {
                            selectedGame++; if (selectedGame >= NUM_GAMES) selectedGame = 0;
                            M5Cardputer.Speaker.tone(1000, 20); selectionChanged = true; lastInputTime = millis();
                        } 
                        else if (lowerC == keySel) {
                            M5Cardputer.Speaker.tone(1500, 50); delay(60); launchGame(selectedGame); return; 
                        }
                    }
                }
            }

            if (selectionChanged || millis() - lastFireUpdate > 15) {
                if (millis() - lastFireUpdate > 15) { updateFire(); lastFireUpdate = millis(); }
                drawMainMenu();
            }
        }
    } 
    else if (currentState == APP_FALLTRIS) { if (!loopFallTris()) { currentState = MAIN_MENU; menuCooldownTime = millis() + 300; lastInputTime = millis(); } }
    else if (currentState == APP_ROCKADV) { if (!loopRockADV()) { currentState = MAIN_MENU; menuCooldownTime = millis() + 300; lastInputTime = millis(); } }
    else if (currentState == APP_PUZZLEBALL) { if (!loopPuzzleBall()) { currentState = MAIN_MENU; menuCooldownTime = millis() + 300; lastInputTime = millis(); } }
}