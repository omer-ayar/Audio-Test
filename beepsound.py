import numpy as np
from scipy.io.wavfile import write

# Parametreler
sample_rate = 44100  # Örnekleme hızı (Hz)
beep_frequency = 2000  # Bip frekansı (Hz)
beep_duration = 7  # Bip süresi (saniye)

# Süre
t = np.linspace(0, beep_duration, int(sample_rate * beep_duration), endpoint=False)

# Bip sinyali oluştur
signal = 0.5 * np.sin(2 * np.pi * beep_frequency * t)

# WAV dosyasına yaz
write('beep_sound.wav', sample_rate, (signal * 32767).astype(np.int16))
