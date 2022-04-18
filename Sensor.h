// Class containing all relevant information per sensor.
class Sensor {
 public:
  Sensor(ADC* adc, uint8_t pin_value, SensorState* sensor_state = nullptr)
      : initialized_(false), adc_(adc), pin_value_(pin_value),
        user_threshold_(kDefaultThreshold),
        #if defined(CAN_AVERAGE)
          moving_average_(kWindowSize),
        #endif
        offset_(0), sensor_state_(sensor_state),
        should_delete_state_(false) {}
  
  ~Sensor() {
    if (should_delete_state_) {
      delete sensor_state_;
    }
  }

  void Init(uint8_t sensor_id) {
    // Sensor should only be initialized once.
    if (initialized_) {
      return;
    }
    // Sensor IDs should be 1-indexed thus they must be non-zero.
    if (sensor_id == 0) {
      return;
    }
    pinMode(pin_value_, INPUT);

    // There is no state for this sensor, create one.
    if (sensor_state_ == nullptr) {
      sensor_state_ = new SensorState();
      // If this sensor created the state, then it's in charge of deleting it.
      should_delete_state_ = true;
    }

    // Initialize the sensor state.
    // This sets the button number corresponding to the sensor state.
    // Trying to re-initialize a sensor_state_ is a no-op, so no harm in 
    sensor_state_->Init();

    // If this sensor hasn't been added to the state, then try adding it.
    if (sensor_state_->GetIndexForSensor(sensor_id) == SIZE_MAX) {
      sensor_state_->AddSensor(sensor_id);
    }
    sensor_id_ = sensor_id;
    initialized_ = true;
  }

  // Fetches the sensor value and maybe triggers the button press/release.
  void EvaluateSensor(bool willSend) {
    if (!initialized_) {
      return;
    }
    // If this sensor was never added to the state, then return early.
    if (sensor_state_->GetIndexForSensor(sensor_id_) == SIZE_MAX) {
      return;
    }

    int16_t sensor_value = adc_->analogRead(pin_value_);

    #if defined(CAN_AVERAGE)
      // Fetch the updated Weighted Moving Average.
      cur_value_ = moving_average_.GetAverage(sensor_value) - offset_;
      cur_value_ = constrain(cur_value_, 0, 1023);
    #else
      // Don't use averaging for Arduino Leonardo, Uno, Mega1280, and Mega2560
      // since averaging seems to be broken with it. This should also include
      // the Teensy 2.0 as it's the same board as the Leonardo.
      // TODO(teejusb): Figure out why and fix. Maybe due to different integer
      // widths?
      cur_value_ = sensor_value - offset_;
    #endif

    if (willSend) {
      sensor_state_->EvaluateSensor(
        sensor_id_, cur_value_, user_threshold_);
    }
  }

  void UpdateThreshold(int16_t new_threshold) {
    user_threshold_ = new_threshold;
  }

  int16_t UpdateOffset() {
    // Update the offset with the last read value. UpdateOffset should be
    // called with no applied pressure on the panels so that it will be
    // calibrated correctly.
    offset_ = cur_value_;
    return offset_;
  }

  int16_t GetCurValue() {
    return cur_value_;
  }

  int16_t GetThreshold() {
    return user_threshold_;
  }

  // Delete default constructor. Pin number MUST be explicitly specified.
  Sensor() = delete;
 
 private:
  // Ensures that Init() has been called at exactly once on this Sensor.
  bool initialized_;
  ADC* adc_;
  // The pin on the Teensy/Arduino corresponding to this sensor.
  uint8_t pin_value_;

  // The user defined threshold value to activate/deactivate this sensor at.
  int16_t user_threshold_;
  
  #if defined(CAN_AVERAGE)
  // The smoothed moving average calculated to reduce some of the noise. 
  HullMovingAverage moving_average_;
  #endif

  // The latest value obtained for this sensor.
  int16_t cur_value_;
  // How much to shift the value read by during each read.
  int16_t offset_;

  // Since many sensors may refer to the same input this may be shared among
  // other sensors.
  SensorState* sensor_state_;
  // Used to indicate if the state is owned by this instance, or if it was
  // passed in from outside
  bool should_delete_state_;

  // A unique number corresponding to this sensor. Set during Init().
  uint8_t sensor_id_;
};
