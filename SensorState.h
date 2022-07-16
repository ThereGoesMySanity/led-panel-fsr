// The class that actually evaluates a sensor and actually triggers the button
// press or release event. If there are multiple sensors added to a
// SensorState, they will all be evaluated first before triggering the event.
class SensorState {
 public:
  SensorState(uint8_t _buttonNum)
      : num_sensors_(0),
        #if defined(ENABLE_LIGHTS)
        kLightsPin(curLightPin++),
        #endif
        buttonNum(_buttonNum) {
    for (size_t i = 0; i < kMaxSharedSensors; ++i) {
      sensor_ids_[i] = 0;
      individual_states_[i] = SensorState::OFF;
    }
  }

  // Adds a new sensor to share this state with. If we try adding a sensor that
  // we don't have space for, it's essentially dropped.
  void AddSensor(uint8_t sensor_id) {
    if (num_sensors_ < kMaxSharedSensors) {
      sensor_ids_[num_sensors_++] = sensor_id;
    }
  }

  // Evaluates a single sensor as part of the shared state.
  void EvaluateSensor(uint8_t sensor_id,
                      int16_t cur_value,
                      int16_t user_threshold) {
    if (!initialized_) {
      return;
    }
    size_t sensor_index = GetIndexForSensor(sensor_id);

    // The sensor we're evaluating is not part of this shared state.
    // This should not happen.
    if (sensor_index == SIZE_MAX) {
      return;
    }

    // If we're above the threshold, turn the individual sensor on.
    if (cur_value >= user_threshold + kPaddingWidth) {
      individual_states_[sensor_index] = SensorState::ON;
    }

    // If we're below the threshold, turn the individual sensor off.
    if (cur_value < user_threshold - kPaddingWidth) {
      individual_states_[sensor_index] = SensorState::OFF;
    }
    
    // If we evaluated all the sensors this state applies to, only then
    // should we determine if we want to send a press/release event.
    bool all_evaluated = (sensor_index == num_sensors_ - 1);

    if (all_evaluated) {
      switch (combined_state_) {
        case SensorState::OFF:
          {
            // If ANY of the sensors triggered, then we trigger a button press.
            bool turn_on = false;
            for (size_t i = 0; i < num_sensors_; ++i) {
              if (individual_states_[i] == SensorState::ON) {
                turn_on = true;
                break;
              }
            }
            if (turn_on) {
              ButtonPress(buttonNum);
              combined_state_ = SensorState::ON;
            }
          }
          break;
        case SensorState::ON:
          {
            // ALL of the sensors must be off to trigger a release.
            // i.e. If any of them are ON we do not release.
            bool turn_off = true;
            for (size_t i = 0; i < num_sensors_; ++i) {
              if (individual_states_[i] == SensorState::ON) {
                turn_off = false;
              }
            }
            if (turn_off) {
              ButtonRelease(buttonNum);
              combined_state_ = SensorState::OFF;
            }
          }
          break;
      }
    }
  }

  // Given a sensor_id, returns the index in the sensor_ids_ array.
  // Returns SIZE_MAX if not found.
  size_t GetIndexForSensor(uint8_t sensor_id) {
    for (size_t i = 0; i < num_sensors_; ++i) {
      if (sensor_ids_[i] == sensor_id) {
        return i;
      }
    }
    return SIZE_MAX;
  }

  // Used to determine the state of each individual sensor, as well as
  // the aggregated state.
  enum State { OFF, ON };

  inline State GetCurrentState() const { return combined_state_; }

 private:
  // Ensures that Init() has been called at exactly once on this SensorState.
  bool initialized_;

  // The collection of sensors shared with this state.
  uint8_t sensor_ids_[kMaxSharedSensors];
  // The number of sensors this state combines with.
  size_t num_sensors_;

  // The evaluated state for each individual sensor.
  State individual_states_[kMaxSharedSensors];
  // The aggregated state.
  State combined_state_ = SensorState::OFF;

  // One-tailed width size to create a window around user_threshold to
  // mitigate fluctuations by noise. 
  // TODO(teejusb): Make this a user controllable variable.
  const int16_t kPaddingWidth = 1;

  // The button number this state corresponds to.
  // Set once in Init().
  uint8_t buttonNum;
};
