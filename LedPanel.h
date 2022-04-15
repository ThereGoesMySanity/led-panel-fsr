#include <MatrixHardware_Teensy4_ShieldV5.h>
#include <SmartMatrix.h>
#include <GifDecoder.h>
#include "ldur.h"

const uint16_t kPanelWidth = 64;
const uint16_t kNumPanels = 4;
//L, D, U, R
const uint16_t kPanelPositions[] = {128, 64, 192, 0};
const bool kPanelFlipped[] = {false, true, false, true};

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

void screenClearCallback(void) {
  backgroundLayer.fillScreen({0,0,0});
}
void updateScreenCallback(void) {
  backgroundLayer.swapBuffers();
}

void drawPixelCallback(int16_t x, int16_t y, uint8_t red, uint8_t green, uint8_t blue) {
  int16_t index = x / kPanelWidth;
  int16_t xpos = kPanelPositions[index] + (kPanelFlipped[index]? kPanelWidth - x % kPanelWidth : x % kPanelWidth);
  int16_t ypos = (kPanelFlipped[index]? kMatrixHeight - y : y);
  rgb24 color = {red, green, blue};
  backgroundLayer.drawPixel(xpos, ypos, (currentStates[index] == SensorState::ON)? color : COLOR_BLACK);
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
      backgroundLayer.fillScreen(COLOR_BLACK);
      backgroundLayer.swapBuffers();

      SetGif((uint8_t*)ldur_gif, ldur_gif_len);
    }

    void Update() {
      bool any_on = false;
      for (size_t i = 0; i < kNumPanels; i++) {
        currentStates[i] = _states[i].GetCurrentState();
        if (currentStates[i] == SensorState::ON) any_on = true;
      }
      
      if (any_on) {
        unsigned long now = millis();
        if (now >= nextUpdateTime) {
          decoder.decodeFrame(false);
          nextUpdateTime = now + decoder.getFrameDelay_ms();
        }
      } else if (nextUpdateTime > 0) {
        Clear();
      }
    }

    void SetGif(uint8_t* _buffer, size_t len) {
      decoder.startDecoding(_buffer, len);
      Clear();
    }

    void Clear() {
      backgroundLayer.fillScreen(COLOR_BLACK);
      backgroundLayer.swapBuffers();
      nextUpdateTime = 0;
    }
  private:
    GifDecoder<kMatrixWidth, kMatrixHeight, 12> decoder;
    const SensorState* _states;
    
    unsigned long nextUpdateTime;
};
