# HyperDRM 0.15.4

- Add optional HyperHDR FlatBuffer output for all LED hardware instances. JSON remains the default.
- Add optional region rectangles and linear-light mixing for custom calibration. Rim geometry keeps the previous color response.
- Recover from a receiver that never replies to registration and restart cleanly after Clear when changing the receiver or output transport.
- Keep MQTT source switching and expose output transport and calibration geometry in Home Assistant.
