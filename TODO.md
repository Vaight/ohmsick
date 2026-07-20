# TO-DO / TIMELINE

- <i>v0.2.2</i> : <b>On-The-Fly Data Modification</b>
    - <s>Change I/O ownership to the backend instead of the plugin.</s>
    - Analog Noise Correction (Dynamic remap based on average values).
    - Inversion (Map range `0-127` to `127-0` and vice versa).
    - Arbutrary Remap (Map range `0-127` to `A-B` where `0 < A,B < 127` and `A < B`).
    - Data Toggle (Toggle between `0` and `127` from a digital input; button to switch).
    - Apply these algorithms to each GUI card config.
    - Card GUI line chart representation of processed data.
    - Attempt to lower serial & processing latency.
- <i>v0.2.3</i> : <b>Advanced MIDI Control</b>
    - MIDI Note Assignment (ability to assign digital inputs to MIDI note on/off signals)
    - Arbutrary MIDI CC Assignment (ability to choose MIDI CC channels per input).
    - Include a new display for viewing a log of MIDI actions departing the plugin
- <i>v0.2.4</i> : <b>Graphics Overhaul</b>
    - Create a true visual identity for the application (icon, logo, color theme, etc...)
    - Overhaul & polish JUCE plugin GUI
<br><br>
- <i>v0.3.0</i> : <b>Broaden Device Support</b>
    - Port firmware to other dedicated microcontrollers (rPi, ESP32, etc...)
    - Port firmware to other chips (AVR family, ARM family, ESP family, etc...)
    - Add specific backend support for different boards / serial interfaces
    - Overhaul plugin frontend serial device selection
        - Auto-detection of connected devices
        - Detect device type and use specific backend support
    - Add support for basic USB MIDI Serial devices??
- <i>v0.3.1</i> : <b>TBD</b>