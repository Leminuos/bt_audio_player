# I2S Audio Player with MAX98357 - "Xin chào" Greeting

## Description
ESP-IDF program to play Vietnamese "Xin chào" greeting through MAX98357 amplifier with 3W 8Ω speaker.

**Features:**
- Plays real voice recording "Xin chào" (Hello in Vietnamese)
- Generates test tones for hardware verification
- 16-bit stereo audio support
- Compatible with ESP-IDF v5.x

## Hardware

### Pin Connections:
```
ESP32           MAX98357
------          --------
GPIO 26    ->   BCLK (Bit Clock)
GPIO 25    ->   LRC/WS (Left/Right Clock)
GPIO 22    ->   DIN (Data In)
GND        ->   GND
5V         ->   VIN (or 3.3V depending on module)

MAX98357    ->  Speaker
+           ->  Speaker (+)
-           ->  Speaker (-)
```

## Quick Start

1. **Create ESP-IDF project:**
```bash
idf.py create-project i2s_audio_project
cd i2s_audio_project/main
```

2. **Copy files:** `i2s_audio_player.c`, `audio_data.h`, `CMakeLists.txt` → `main/`

3. **Build and flash:**
```bash
cd .. && idf.py set-target esp32 && idf.py build flash monitor
```

## Expected Output

You will hear:
1. "Xin chào" greeting in Vietnamese voice (~1.1 seconds)
2. Brief pause
3. 440Hz test tone (3 seconds)

## Converting Your Own Audio Files

### Method 1: FFmpeg (Recommended)

```bash
# Convert any audio to raw PCM
ffmpeg -i your_audio.mp3 -f s16le -ar 16000 -ac 2 output.raw

# Adjust volume (50%)
ffmpeg -i input.mp3 -af "volume=0.5" -f s16le -ar 16000 -ac 2 output.raw
```

### Method 2: Python Script

```python
#!/usr/bin/env python3
import numpy as np

with open('output.raw', 'rb') as f:
    data = np.frombuffer(f.read(), dtype=np.int16)

with open('audio_data.h', 'w') as f:
    f.write("#ifndef AUDIO_DATA_H\n#define AUDIO_DATA_H\n\n")
    f.write("#define AUDIO_SAMPLE_RATE 16000\n")
    f.write("#define AUDIO_CHANNELS 2\n\n")
    f.write("static const int16_t audio_xin_chao[] = {\n")
    for i in range(0, len(data), 12):
        row = data[i:i+12]
        f.write("    " + ", ".join(f"{v:6d}" for v in row) + ",\n")
    f.write("};\n\n")
    f.write("#define AUDIO_DATA_SIZE (sizeof(audio_xin_chao)/sizeof(int16_t))\n")
    f.write("#define AUDIO_DURATION_SEC ((float)AUDIO_DATA_SIZE/(AUDIO_SAMPLE_RATE*AUDIO_CHANNELS))\n")
    f.write("#endif\n")
```

## File Size Guidelines

```
Size = Sample_Rate × Channels × Duration × 2

Examples:
- 1 second @ 16kHz stereo: 64 KB
- 5 seconds @ 16kHz stereo: 320 KB
```

**Your "Xin chào" file:** 1.13s = ~72 KB

## Troubleshooting

### No sound
- Check wire connections
- Verify power supply (3.3V or 5V)
- Ensure audio_data.h is in main/ directory

### Too quiet/loud
```bash
# Adjust volume during conversion
ffmpeg -i input.mp3 -af "volume=1.5" -f s16le -ar 16000 -ac 2 output.raw
```

### File too large
```bash
# Use 8kHz or mono
ffmpeg -i input.mp3 -ar 8000 -ac 1 -f s16le output.raw
```

## References
- ESP-IDF I2S: https://docs.espressif.com/projects/esp-idf/en/latest/api-reference/peripherals/i2s.html
- MAX98357: https://www.analog.com/media/en/technical-documentation/data-sheets/MAX98357A-MAX98357B.pdf
