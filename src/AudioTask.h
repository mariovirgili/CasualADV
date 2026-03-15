// All comments in the code must always be in English.
#pragma once
#include <Arduino.h>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>
#include <vector>

extern SemaphoreHandle_t sdMutex;

namespace AudioTask {
    extern bool mp3Enabled;
    extern uint8_t mp3Volume; // 0-255 (affects only MP3 gain)

    // Core functionality
    void begin();
    void playMP3(const char* path);
    void stop();
    bool isPlaying();
    void setMP3Volume(uint8_t vol); 
    
    // Playlist management
    void loadPlaylist();
    void startPlaylist();
    void savePlaylist(const std::vector<String>& newPlaylist);
    
    // Track control
    void nextTrack();
    void prevTrack();
    String getCurrentTrackName();
}