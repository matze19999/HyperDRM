# HyperDRM 0.15.11

- Pause capture when webOS reports standby or turns the screen off. HyperDRM releases its output priority so the receiver can turn off the LEDs. This applies to FlatBuffer and JSON output. [Issue #4](https://github.com/matze19999/HyperDRM/issues/4)
- Resume capture when the display wakes if it was running or starting before standby. A manual **Stop** persists across standby and wake.
