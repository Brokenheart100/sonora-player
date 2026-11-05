"""生成 M3 用的真音频（WAV）。

`.demo/music/` 里那 20 个 .flac 是空壳，只够验证"扫描/入库/列表"，
放到播放链上会被解码器拒绝。这个脚本用标准库合成几段能真的出声的正弦波，
用来验证 QtMultimedia 的整条解码-输出路径。
"""

import math
import os
import struct
import wave

OUT = os.path.join(os.path.dirname(__file__), "audio")


def tone(path, seconds, freq, amp=0.28, sample_rate=44100):
    # 两端各加 20ms 淡入淡出，避免爆音（爆音会让人误以为播放链路有问题）
    fade = int(sample_rate * 0.02)
    total = int(sample_rate * seconds)
    frames = bytearray()
    for i in range(total):
        env = 1.0
        if i < fade:
            env = i / fade
        elif i > total - fade:
            env = (total - i) / fade
        value = int(32767 * amp * env * math.sin(2 * math.pi * freq * i / sample_rate))
        frames += struct.pack("<h", value)
    with wave.open(path, "wb") as f:
        f.setnchannels(1)
        f.setsampwidth(2)
        f.setframerate(sample_rate)
        f.writeframes(bytes(frames))


def main():
    os.makedirs(OUT, exist_ok=True)
    tracks = [
        ("A4 - 440Hz.wav", 6.0, 440.0),
        ("E5 - 659Hz.wav", 5.0, 659.25),
        ("A5 - 880Hz.wav", 4.0, 880.0),
    ]
    for name, seconds, freq in tracks:
        path = os.path.join(OUT, name)
        tone(path, seconds, freq)
        print(f"{name}  {os.path.getsize(path)} bytes")


if __name__ == "__main__":
    main()
