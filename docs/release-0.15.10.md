# HyperDRM 0.15.10

- Added a **Black bar detection** switch in the TV app and in Home Assistant via MQTT. Fresh installations start with detection off. Updates keep the existing behavior or saved preference.
- Added guided LED-position measurement with independent validation for TVs that expose spatial histograms. The wizard keeps the saved profile active until a new measurement passes validation and is saved.
- Added an optional experimental LG G4 edge-correction profile for HyperHDR, based on measurements from an OLED65G42LW.
