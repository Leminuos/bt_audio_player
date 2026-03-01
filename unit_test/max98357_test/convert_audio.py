#!/usr/bin/env python3
"""
Convert Xin_chào.mp3 to C header file
"""

import numpy as np

def convert_raw_to_header(raw_file, output_file='audio_data.h', var_name='audio_xin_chao'):
    """Convert raw PCM to C header file"""
    
    # Read raw PCM data
    with open(raw_file, 'rb') as f:
        audio_data = np.frombuffer(f.read(), dtype=np.int16)
    
    # Calculate parameters
    SAMPLE_RATE = 16000
    CHANNELS = 2  # Stereo
    total_samples = len(audio_data)
    duration = total_samples / (SAMPLE_RATE * CHANNELS)
    
    print(f"Converting {raw_file}...")
    print(f"  Total samples: {total_samples}")
    print(f"  Duration: {duration:.2f} seconds")
    print(f"  Channels: {CHANNELS}")
    print(f"  Sample rate: {SAMPLE_RATE} Hz")
    
    # Write C header file
    with open(output_file, 'w') as f:
        f.write("/**\n")
        f.write(" * @file audio_data.h\n")
        f.write(' * @brief Raw audio data - "Xin chào" greeting\n')
        f.write(" * \n")
        f.write(f" * Original: Xin_chào.mp3\n")
        f.write(f" * Converted: 16-bit PCM, Stereo, 16kHz\n")
        f.write(f" * Duration: {duration:.2f} seconds\n")
        f.write(f" * Total samples: {total_samples}\n")
        f.write(" */\n\n")
        
        f.write("#ifndef AUDIO_DATA_H\n")
        f.write("#define AUDIO_DATA_H\n\n")
        
        f.write("#include <stdint.h>\n")
        f.write("#include <stddef.h>\n\n")
        
        f.write("// Audio configuration\n")
        f.write(f"#define AUDIO_SAMPLE_RATE       {SAMPLE_RATE}    // 16kHz sample rate\n")
        f.write(f"#define AUDIO_BITS_PER_SAMPLE   16       // 16-bit\n")
        f.write(f"#define AUDIO_CHANNELS          {CHANNELS}        // Stereo\n\n")
        
        f.write(f'// Raw PCM audio data - "Xin chào" greeting\n')
        f.write(f"static const int16_t {var_name}[] = {{\n")
        
        # Write data in rows of 12 values
        for i in range(0, len(audio_data), 12):
            row = audio_data[i:i+12]
            f.write("    ")
            f.write(", ".join(f"{val:6d}" for val in row))
            if i + 12 < len(audio_data):
                f.write(",\n")
            else:
                f.write("\n")
        
        f.write("};\n\n")
        
        f.write("// Calculate array size\n")
        f.write(f"#define AUDIO_DATA_SIZE (sizeof({var_name}) / sizeof(int16_t))\n\n")
        
        f.write("// Duration in seconds\n")
        f.write("#define AUDIO_DURATION_SEC ((float)AUDIO_DATA_SIZE / (AUDIO_SAMPLE_RATE * AUDIO_CHANNELS))\n\n")
        
        f.write("#endif // AUDIO_DATA_H\n")
    
    print(f"\n✓ Generated {output_file}")
    print(f"  File size: {len(audio_data) * 2} bytes")
    print(f"  Variable name: {var_name}")

if __name__ == "__main__":
    convert_raw_to_header('xin_chao.raw', 'audio_data.h', 'audio_xin_chao')
    print("\nDone!")
