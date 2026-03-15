// All comments in the code must always be in English.
#include "AudioTask.h"
#include <M5Cardputer.h>
#include <SD.h>
#include "AudioFileSourceSD.h"
#include "AudioGeneratorMP3.h"
#include "AudioOutput.h"

SemaphoreHandle_t sdMutex = NULL;

namespace AudioTask {
    bool mp3Enabled = false;
    uint8_t mp3Volume = 128; // Default to ~50%
}

float mp3Gain = 0.5f; // Internal gain multiplier

class AudioOutputM5Speaker : public AudioOutput {
private:
    m5::Speaker_Class* _m5sound;
    int16_t _tri_buffer[3][1536]; 
    size_t _tri_buffer_index = 0;
    size_t _tri_index = 0;
    int current_sample_rate = 44100; 
    
public:
    AudioOutputM5Speaker(m5::Speaker_Class* m5sound) { _m5sound = m5sound; }
    virtual ~AudioOutputM5Speaker() {}
    virtual bool begin() override { return true; }
    
    virtual bool SetRate(int hz) override { 
        current_sample_rate = hz; 
        return true; 
    }
    
    virtual bool ConsumeSample(int16_t sample[2]) override {
        if (_tri_buffer_index < 1536) {
            // Mix to mono and apply independent gain
            int32_t mixed = ((int32_t)sample[0] + sample[1]) / 2;
            mixed = (int32_t)(mixed * mp3Gain);
            
            // Hard clipping protection
            if (mixed > 32767) mixed = 32767;
            if (mixed < -32768) mixed = -32768;

            _tri_buffer[_tri_index][_tri_buffer_index++] = (int16_t)mixed;
            return true;
        }
        flush(); 
        return false;
    }
    
    void flush() {
        if (_tri_buffer_index > 0) {
            while (_m5sound->isPlaying()) { vTaskDelay(1); }
            _m5sound->playRaw(_tri_buffer[_tri_index], _tri_buffer_index, current_sample_rate, false, 1, 0);
            _tri_index = (_tri_index + 1) % 3;
            _tri_buffer_index = 0;
        }
    }
    
    virtual bool stop() override { 
        flush(); 
        while (_m5sound->isPlaying()) { vTaskDelay(1); }
        _m5sound->stop(); 
        return true; 
    }
};

namespace AudioTask {
    AudioGeneratorMP3 *mp3 = nullptr;
    AudioFileSourceSD *file = nullptr;
    AudioOutputM5Speaker *out = nullptr;
    
    TaskHandle_t audioTaskHandle = nullptr;
    String currentFile = "";
    
    volatile bool playRequested = false;
    volatile bool stopRequested = false;

    std::vector<String> playlist;
    int currentTrackIdx = -1;

    void audioLoop(void *pvParameters) {
        while (true) {
            if (stopRequested) {
                if (mp3 && mp3->isRunning()) mp3->stop();
                if (file && file->isOpen()) file->close();
                stopRequested = false;
            }

            if (playRequested) {
                if (mp3 && mp3->isRunning()) mp3->stop();
                if (file && file->isOpen()) file->close();
                
                if (xSemaphoreTake(sdMutex, portMAX_DELAY)) {
                    file->open(currentFile.c_str());
                    xSemaphoreGive(sdMutex);
                    
                    if (file->isOpen()) mp3->begin(file, out);
                }
                playRequested = false;
            }

            if (mp3 && mp3->isRunning()) {
                bool loopResult = false;
                if (xSemaphoreTake(sdMutex, pdMS_TO_TICKS(10))) {
                    loopResult = mp3->loop();
                    xSemaphoreGive(sdMutex);
                }
                
                if (!loopResult) {
                    mp3->stop();
                    if (file && file->isOpen()) file->close();

                    // --- Auto Advance Playlist ---
                    if (mp3Enabled && playlist.size() > 0 && !stopRequested) {
                        currentTrackIdx++;
                        if (currentTrackIdx >= playlist.size()) currentTrackIdx = 0;
                        currentFile = "/CasualADV/music/" + playlist[currentTrackIdx];
                        playRequested = true;
                    }
                } else {
                    static int yieldCounter = 0;
                    if (++yieldCounter > 3) { vTaskDelay(1); yieldCounter = 0; }
                }
            } else {
                vTaskDelay(pdMS_TO_TICKS(50)); 
            }
        }
    }

    void begin() {
        if (sdMutex == NULL) sdMutex = xSemaphoreCreateMutex();
        if (!out) out = new AudioOutputM5Speaker(&M5Cardputer.Speaker);
        if (!mp3) mp3 = new AudioGeneratorMP3();
        if (!file) file = new AudioFileSourceSD(); 
        xTaskCreatePinnedToCore(audioLoop, "AudioTask", 8192, NULL, 1, &audioTaskHandle, 0);
    }

    void playMP3(const char* path) {
        stopRequested = true; delay(10); 
        currentFile = String(path);
        playRequested = true;
    }

    void stop() {
        playRequested = false; stopRequested = true;
        delay(20); M5Cardputer.Speaker.stop();
    }

    bool isPlaying() { return (mp3 && mp3->isRunning()); }
    
    void setMP3Volume(uint8_t vol) {
        mp3Gain = (float)vol / 255.0f;
    }

    void loadPlaylist() {
        playlist.clear();
        if (xSemaphoreTake(sdMutex, portMAX_DELAY)) {
            File f = SD.open("/CasualADV/playlist.txt", FILE_READ);
            if (f) {
                while (f.available()) {
                    String line = f.readStringUntil('\n');
                    line.trim();
                    if (line.length() > 0) playlist.push_back(line);
                }
                f.close();
            }
            xSemaphoreGive(sdMutex);
        }
    }

    void startPlaylist() {
        if (playlist.empty()) return;
        currentTrackIdx = 0;
        playMP3(("/CasualADV/music/" + playlist[currentTrackIdx]).c_str());
    }

    void savePlaylist(const std::vector<String>& newPlaylist) {
        if (xSemaphoreTake(sdMutex, portMAX_DELAY)) {
            File f = SD.open("/CasualADV/playlist.txt", FILE_WRITE);
            if (f) {
                for (const String& s : newPlaylist) f.println(s);
                f.close();
            }
            xSemaphoreGive(sdMutex);
        }
        loadPlaylist(); // Reload into memory
    }

    void nextTrack() {
        if (playlist.empty()) return;
        currentTrackIdx++;
        if (currentTrackIdx >= playlist.size()) currentTrackIdx = 0;
        playMP3(("/CasualADV/music/" + playlist[currentTrackIdx]).c_str());
    }

    void prevTrack() {
        if (playlist.empty()) return;
        currentTrackIdx--;
        if (currentTrackIdx < 0) currentTrackIdx = playlist.size() - 1;
        playMP3(("/CasualADV/music/" + playlist[currentTrackIdx]).c_str());
    }

    String getCurrentTrackName() {
        if (playlist.empty() || currentTrackIdx < 0 || currentTrackIdx >= playlist.size()) return "None";
        return playlist[currentTrackIdx];
    }
}