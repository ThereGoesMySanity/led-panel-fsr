class SerialProcessor {
 public:
   void Init(long baud_rate) {
    Serial.begin(baud_rate);
  }

  void CheckAndMaybeProcessData() {
    while (Serial.available() > 0) {
      size_t bytes_read = Serial.readBytesUntil(
          '\n', buffer_, kBufferSize - 1);
      buffer_[bytes_read] = '\0';

      if (bytes_read == 0) { return; }
 
      switch(buffer_[0]) {
        case 'o':
        case 'O':
          UpdateOffsets();
          break;
        case 'v':
        case 'V':
          PrintValues();
          break;
        case 't':
        case 'T':
          PrintThresholds();
          break;
        case 'g':
        case 'G':
          UpdateGif(bytes_read);
          break;
        case '0' ... '9': // Case ranges are non-standard but work in gcc
          UpdateAndPrintThreshold(bytes_read);
        default:
          break;
      }
    }
  }

  void UpdateGif(size_t bytes_read) {
    if (bytes_read < 3) return;
    size_t filesize = strtoul(buffer_ + 2, nullptr, 10);
    if (filesize == 0) return;

    char* fbuffer = (char*)malloc(filesize);
    bytes_read = 0;
    while (bytes_read < filesize) {
      int result = Serial.readBytes(fbuffer + bytes_read, filesize - bytes_read);
      if (result <= 0) return;
      bytes_read += result;
    }
    panel.SetGif(fbuffer, filesize);
    free(fbuffer);
  }

  void UpdateAndPrintThreshold(size_t bytes_read) {
    // Need to specify:
    // Sensor number + Threshold value, separated by a space.
    // {0, 1, 2, 3,...} + "0"-"1023"
    // e.g. 3 180 (fourth FSR, change threshold to 180)
    
    if (bytes_read < 3 || bytes_read > 7) { return; }

    char* next = nullptr;
    size_t sensor_index = strtoul(buffer_, &next, 10);
    if (sensor_index >= kNumSensors) { return; }

    int16_t sensor_threshold = strtol(next, nullptr, 10);
    if (sensor_threshold < 0 || sensor_threshold > 1023) { return; }

    kSensors[sensor_index].UpdateThreshold(sensor_threshold);
    PrintThresholds();
  }

  void UpdateOffsets() {
    for (size_t i = 0; i < kNumSensors; ++i) {
      kSensors[i].UpdateOffset();
    }
  }

  void PrintValues() {
    Serial.print("v");
    for (size_t i = 0; i < kNumSensors; ++i) {
      Serial.print(" ");
      Serial.print(kSensors[i].GetCurValue());
    }
    Serial.print("\n");
  }

  void PrintThresholds() {
    Serial.print("t");
    for (size_t i = 0; i < kNumSensors; ++i) {
      Serial.print(" ");
      Serial.print(kSensors[i].GetThreshold());
    }
    Serial.print("\n");
  }

 private:
   static const size_t kBufferSize = 64;
   char buffer_[kBufferSize];
};
