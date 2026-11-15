#!/usr/bin/env python3
"""Gera os arquivos de midia de demonstracao (/musica.mp3 e /video.mp4) do LiwusOS."""

import math
import os
import struct
import subprocess
import tempfile
import wave
from PIL import Image, ImageDraw


def synth_audio_wav(path: str, duration_sec: float = 8.0, sample_rate: int = 48000) -> None:
    total_samples = int(duration_sec * sample_rate)
    # Progressao harmonica: Am -> F -> C -> G (Synthwave / Chiptune)
    chords = [
        (220.00, 261.63, 329.63),  # Am (A3, C4, E4)
        (174.61, 220.00, 261.63),  # F  (F3, A3, C4)
        (261.63, 329.63, 392.00),  # C  (C4, E4, G4)
        (196.00, 246.94, 293.66),  # G  (G3, B3, D4)
    ]
    arp_notes = [
        [440.00, 523.25, 659.25, 880.00, 659.25, 523.25, 440.00, 329.63],
        [349.23, 440.00, 523.25, 698.46, 523.25, 440.00, 349.23, 261.63],
        [523.25, 659.25, 783.99, 1046.50, 783.99, 659.25, 523.25, 392.00],
        [392.00, 493.88, 587.33, 783.99, 587.33, 493.88, 392.00, 293.66],
    ]

    frames = bytearray()
    two_pi = 2.0 * math.pi

    for i in range(total_samples):
        t = i / sample_rate
        bar = int(t / 2.0) % 4
        beat_pos = (t % 2.0) / 2.0
        step_idx = int((t % 2.0) * 4.0) % 8
        step_local = ((t % 2.0) * 4.0) % 1.0

        c0, c1, c2 = chords[bar]
        arp_f = arp_notes[bar][step_idx]
        bass_f = c0 * 0.5

        # Envelope suave do arpejo
        arp_env = math.exp(-3.5 * step_local)
        # Pad estereo com leve detune
        pad_l = (
            math.sin(two_pi * c0 * t)
            + 0.7 * math.sin(two_pi * (c1 * 1.002) * t)
            + 0.6 * math.sin(two_pi * c2 * t)
        ) * 0.18
        pad_r = (
            math.sin(two_pi * (c0 * 0.998) * t)
            + 0.7 * math.sin(two_pi * c1 * t)
            + 0.6 * math.sin(two_pi * (c2 * 1.003) * t)
        ) * 0.18

        # Baixo pulsante (oitava abaixo)
        bass_env = 0.85 if (int(t * 4.0) % 2 == 0) else 0.45
        bass = (
            math.sin(two_pi * bass_f * t)
            + 0.35 * math.sin(two_pi * (bass_f * 2.0) * t)
        ) * 0.28 * bass_env

        # Lead arpegiado com pan dinamico
        lead = (
            math.sin(two_pi * arp_f * t)
            + 0.3 * math.sin(two_pi * (arp_f * 2.0) * t)
        ) * 0.32 * arp_env
        pan = 0.5 + 0.35 * math.sin(two_pi * 0.5 * t)

        # Fade-in e fade-out suaves nas pontas
        master = 1.0
        if t < 0.25:
            master = t / 0.25
        elif t > duration_sec - 0.4:
            master = max(0.0, (duration_sec - t) / 0.4)

        left = (pad_l + bass + lead * pan) * master
        right = (pad_r + bass + lead * (1.0 - pan)) * master

        l_s16 = max(-30000, min(30000, int(left * 28000.0)))
        r_s16 = max(-30000, min(30000, int(right * 28000.0)))
        frames += struct.pack("<hh", l_s16, r_s16)

    with wave.open(path, "wb") as wf:
        wf.setnchannels(2)
        wf.setsampwidth(2)
        wf.setframerate(sample_rate)
        wf.writeframes(bytes(frames))


def render_video_frames(frame_dir: str, logo_path: str, fps: int = 12, duration_sec: int = 6) -> None:
    w, h = 240, 136
    total_frames = fps * duration_sec
    logo = None
    if os.path.exists(logo_path):
        logo = Image.open(logo_path).convert("RGBA").resize((44, 44), Image.Resampling.NEAREST)

    for idx in range(total_frames):
        t = idx / float(fps)
        img = Image.new("RGB", (w, h), (13, 22, 30))
        draw = ImageDraw.Draw(img)

        # Gradiente de fundo estilo Liwus Slate
        for y in range(h):
            r = int(15 + (y / h) * 14)
            g = int(26 + (y / h) * 24)
            b = int(36 + (y / h) * 32)
            draw.line([(0, y), (w, y)], fill=(r, g, b))

        # Grade em perspectiva animada (Synthwave / Slate Ciano)
        horizon_y = 74
        draw.line([(0, horizon_y), (w, horizon_y)], fill=(34, 199, 214), width=1)
        for gy in range(horizon_y + 6, h, 10):
            offset_y = int((gy + idx * 2) % 62) + horizon_y
            if offset_y < h:
                draw.line([(0, offset_y), (w, offset_y)], fill=(26, 95, 112), width=1)

        for gx in range(-w, w * 2, 28):
            top_x = w // 2 + (gx - w // 2) // 4
            bot_x = gx + int(math.sin(t * 1.5) * 10)
            draw.line([(top_x, horizon_y), (bot_x, h)], fill=(24, 82, 98), width=1)

        # Barras de espectro de audio animadas
        num_bars = 16
        bar_w = 8
        start_x = 18
        for b_idx in range(num_bars):
            phase = t * 6.0 + b_idx * 0.55
            bh = int(12 + 26 * (0.5 + 0.5 * math.sin(phase) * math.cos(phase * 0.7)))
            bx = start_x + b_idx * (bar_w + 4)
            by = horizon_y - 4 - bh
            color = (34, 199, 214) if (b_idx % 2 == 0) else (50, 196, 141)
            draw.rectangle([bx, by, bx + bar_w, horizon_y - 4], fill=color)

        # Sol / Orbe neon pulsante e logo do lince
        orb_x = w - 42
        orb_y = 38 + int(math.sin(t * 3.0) * 3)
        draw.ellipse([orb_x - 24, orb_y - 24, orb_x + 24, orb_y + 24],
                     fill=(21, 90, 104), outline=(34, 199, 214), width=2)
        if logo is not None:
            img.paste(logo, (orb_x - 22, orb_y - 22), logo)

        # Moldura superior HUD
        draw.rectangle([8, 6, 168, 26], fill=(21, 29, 38), outline=(48, 65, 81))
        draw.text((14, 11), "LIWUS OS // MP4 VIDEO", fill=(231, 238, 243))

        # Barra de tempo inferior no quadro
        prog_w = int(((idx + 1) / total_frames) * (w - 20))
        draw.rectangle([10, h - 14, w - 10, h - 8], fill=(17, 26, 34), outline=(58, 77, 94))
        draw.rectangle([10, h - 14, 10 + prog_w, h - 8], fill=(34, 199, 214))

        img.save(os.path.join(frame_dir, f"frame_{idx:04d}.png"))


def main() -> None:
    repo_root = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
    assets_dir = os.path.join(repo_root, "assets")
    mp3_out = os.path.join(assets_dir, "musica.mp3")
    mp4_out = os.path.join(assets_dir, "video.mp4")
    logo_path = os.path.join(assets_dir, "liwus_logo.bmp")

    with tempfile.TemporaryDirectory() as tmp:
        wav_mp3 = os.path.join(tmp, "musica.wav")
        wav_mp4 = os.path.join(tmp, "video_audio.wav")
        frames_dir = os.path.join(tmp, "frames")
        os.makedirs(frames_dir, exist_ok=True)

        synth_audio_wav(wav_mp3, duration_sec=8.0, sample_rate=48000)
        synth_audio_wav(wav_mp4, duration_sec=6.0, sample_rate=48000)
        render_video_frames(frames_dir, logo_path, fps=12, duration_sec=6)

        subprocess.run([
            "ffmpeg", "-y", "-i", wav_mp3,
            "-c:a", "libmp3lame", "-ar", "48000", "-ac", "2", "-b:a", "128k",
            "-metadata", "title=LiwusOS Slate Theme",
            "-metadata", "artist=Liwus Audio Engine",
            mp3_out
        ], check=True, stdout=subprocess.PIPE, stderr=subprocess.PIPE)

        subprocess.run([
            "ffmpeg", "-y",
            "-framerate", "12", "-i", os.path.join(frames_dir, "frame_%04d.png"),
            "-i", wav_mp4,
            "-pix_fmt", "yuvj420p",
            "-c:v", "mjpeg", "-q:v", "7",
            "-c:a", "libmp3lame", "-ar", "48000", "-ac", "2", "-b:a", "96k",
            "-movflags", "+faststart",
            "-shortest",
            mp4_out
        ], check=True, stdout=subprocess.PIPE, stderr=subprocess.PIPE)

    print(f"Generated {mp3_out} ({os.path.getsize(mp3_out)} bytes)")
    print(f"Generated {mp4_out} ({os.path.getsize(mp4_out)} bytes)")


if __name__ == "__main__":
    main()
