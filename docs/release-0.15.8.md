# HyperDRM 0.15.8

- Test connection now verifies the selected transport. In FlatBuffer mode, HyperDRM checks the receiver's LED layout and waits for a registration acknowledgement on the configured FlatBuffer port. The test uses a free priority and sends no frames.
- Home Assistant discovers a new Displayed source sensor for HyperHDR. Its instances attribute lists each LED instance's visible source and priority. Sending source still reports HyperDRM's own output or the PicCap session it manages.
