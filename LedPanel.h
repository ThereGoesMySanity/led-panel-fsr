#include <MatrixHardware_Teensy4_ShieldV5.h>
#include <SmartMatrix.h>
#include <GifDecoder.h>
#include "ldur.h"

const uint16_t kPanelWidth = 64;
const uint16_t kNumPanels = 4;
//L, D, U, R
const uint16_t kPanelPositions[] = {128, 64, 192, 0};
const bool kPanelFlipped[] = {false, true, false, true};
const int8_t kPanelRotation[] = {
  0, -1, 1, 0,
  -1, 0, 0, -1,
  1, 0, 0, 1,
  0, 1, -1, 0
};
const uint16_t kPanelOffset[] {
  0, 1, 0, 0,
  1, 0, 0, 1,
  0, 0, 0, 0,
  0, 0, 1, 0
}

const int defaultBrightness = 255;
const rgb24 COLOR_BLACK = {
    0, 0, 0 };

#define COLOR_DEPTH 24                  // Choose the color depth used for storing pixels in the layers: 24 or 48 (24 is good for most sketches - If the sketch uses type `rgb24` directly, COLOR_DEPTH must be 24)

const uint16_t kMatrixWidth = kPanelWidth * kNumPanels;       // Set to the width of your display, must be a multiple of 8
const uint16_t kMatrixHeight = 64;      // Set to the height of your display
const uint8_t kRefreshDepth = 36;       // Tradeoff of color quality vs refresh rate, max brightness, and RAM usage.  36 is typically good, drop down to 24 if you need to.  On Teensy, multiples of 3, up to 48: 3, 6, 9, 12, 15, 18, 21, 24, 27, 30, 33, 36, 39, 42, 45, 48.  On ESP32: 24, 36, 48
const uint8_t kDmaBufferRows = 4;       // known working: 2-4, use 2 to save RAM, more to keep from dropping frames and automatically lowering refresh rate.  (This isn't used on ESP32, leave as default)
const uint8_t kPanelType = SM_PANELTYPE_HUB75_64ROW_MOD32SCAN;  // Choose the configuration that matches your panels.  See more details in MatrixCommonHUB75.h and the docs: https://github.com/pixelmatix/SmartMatrix/wiki
const uint32_t kMatrixOptions = (SM_HUB75_OPTIONS_NONE);        // see docs for options: https://github.com/pixelmatix/SmartMatrix/wiki
const uint8_t kBackgroundLayerOptions = (SM_BACKGROUND_OPTIONS_NONE);
const uint8_t kScrollingLayerOptions = (SM_SCROLLING_OPTIONS_NONE);

SMARTMATRIX_ALLOCATE_BUFFERS(matrix, kMatrixWidth, kMatrixHeight, kRefreshDepth, kDmaBufferRows, kPanelType, kMatrixOptions);
SMARTMATRIX_ALLOCATE_BACKGROUND_LAYER(backgroundLayer, kMatrixWidth, kMatrixHeight, COLOR_DEPTH, kBackgroundLayerOptions);


SensorState::State currentStates[kNumPanels];

const size_t maxFrames = 8;
size_t current_frame = 0;
size_t frames = maxFrames;
int8_t tile = 1;
rgb24 framesBuffer[kMatrixWidth * kMatrixHeight * maxFrames] DMAMEM;
size_t frame_times[maxFrames];

void screenClearCallback(void) {
  //backgroundLayer.fillScreen({0,0,0});
}
void updateScreenCallback(void) {
  //backgroundLayer.swapBuffers();
}

void drawPixelCallback(int16_t x, int16_t y, uint8_t red, uint8_t green, uint8_t blue) {
  if (tile <= 2) {
    int16_t index = x / kPanelWidth;
    int16_t xpos = kPanelPositions[index] + (kPanelFlipped[index]? kPanelWidth - x % kPanelWidth : x % kPanelWidth);
    int16_t ypos = (kPanelFlipped[index]? kMatrixHeight - y : y);
    framesBuffer[kMatrixWidth * kMatrixHeight * current_frame + kMatrixWidth * ypos + xpos] = (rgb24){red, green, blue};
    if (tile == 2) {
      int16_t xpos1 = kPanelPositions[index + 2] + (kPanelFlipped[index + 2]? x % kPanelWidth : kPanelWidth - x % kPanelWidth);
      int16_t ypos1 = (kPanelFlipped[index + 2]? y : kMatrixHeight - y);
      framesBuffer[kMatrixWidth * kMatrixHeight * current_frame + kMatrixWidth * ypos1 + xpos1] = (rgb24){red, green, blue};
    }
  }
  else if (tile == 4) {
    for (int i = 0; i < 4; i++) {
      int16_t xpos = kPanelPositions[i] + x * kPanelRotation[4*i] + y * kPanelRotation[4*i + 1] 
          + kPanelWidth * kPanelOffset[4*i] + kMatrixHeight * kPanelOffset[4*i + 1];
      int16_t ypos = kPanelPositions[i] + x * kPanelRotation[4*i + 2] + y * kPanelRotation[4*i + 3] 
          + kPanelWidth * kPanelOffset[4*i + 2] + kMatrixHeight * kPanelOffset[4*i + 3];
      framesBuffer[kMatrixWidth * kMatrixHeight * current_frame + kMatrixWidth * ypos + xpos] = (rgb24){red, green, blue};
    }
  }
}

class LedPanel {
  public:
    LedPanel(const SensorState* states) : _states(states) {
    }
    

    void Init() {
      decoder.setScreenClearCallback(screenClearCallback);
      decoder.setUpdateScreenCallback(updateScreenCallback);
      decoder.setDrawPixelCallback(drawPixelCallback);

      matrix.addLayer(&backgroundLayer);
      matrix.setBrightness(defaultBrightness);
      matrix.setRefreshRate(60);

      matrix.begin();

      SetGif((uint8_t*)ldur_gif, ldur_gif_len);
    }

    void Update() {
      bool any_on = false, any_changed = false;
      for (size_t i = 0; i < kNumPanels; i++) {
        if (currentStates[i] != _states[i].GetCurrentState()) any_changed = true;
        currentStates[i] = _states[i].GetCurrentState();
        if (currentStates[i] == SensorState::ON) any_on = true;
      }
      
      if (any_on) {
        unsigned long now = millis();
        if (now >= nextUpdateTime || any_changed) {
          rgb24* dest = backgroundLayer.backBuffer();
          backgroundLayer.fillScreen(COLOR_BLACK);
          for (size_t i = 0; i < kNumPanels; i++) {
            size_t pos_dst = kPanelPositions[i], pos_src = current_frame * kMatrixWidth * kMatrixHeight + pos_dst;
            for (; pos_dst < kMatrixWidth * kMatrixHeight; pos_dst += kMatrixWidth, pos_src += kMatrixWidth) {
              if (currentStates[i] == SensorState::ON) {
                memcpy(&dest[pos_dst], &framesBuffer[pos_src], kPanelWidth * sizeof(rgb24));
              }
            }
          }
          backgroundLayer.swapBuffers();
          nextUpdateTime = now + frame_times[current_frame];
          current_frame = (current_frame + 1) % frames;
        }
      } else if (nextUpdateTime > 0) {
        Clear();
      }
    }

    void SetGif(uint8_t* _buffer, size_t len) {
      uint16_t w, h;
      decoder.startDecoding(_buffer, len);
      decoder.getSize(&w, &h);
      tile = w / kPanelWidth;
      if ((tile != 1 && tile != 2 && tile != 4) || h != kMatrixHeight) {
        Serial.println("GIF incorrect size, skipping");
        return;
      }
      for(int i = 0; i < maxFrames && decoder.getCycleNumber() == 0; i++) {
        current_frame = i;
        decoder.decodeFrame(false);
        frame_times[i] = decoder.getFrameDelay_ms();
      }
      frames = (decoder.getCycleNumber() == 0)? maxFrames : decoder.getFrameCount();
      Clear();
    }

    void Clear() {
      backgroundLayer.fillScreen(COLOR_BLACK);
      backgroundLayer.swapBuffers(false);
      nextUpdateTime = 0;
    }
  private:
    GifDecoder<kMatrixWidth, kMatrixHeight, 12> decoder;
    const SensorState* _states;
    
    unsigned long nextUpdateTime;
};
