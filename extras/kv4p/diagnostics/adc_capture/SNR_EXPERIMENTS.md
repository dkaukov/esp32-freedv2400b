# Discriminator quality experiments

These experiments characterize Codec2's existing `discriminatorSnrDb` estimate.
It is useful as a modem-quality indicator, but it is not calibrated RF SNR.

Run the reproducible host experiment from the repository root:

```bash
g++ -std=c++11 -O2 -Isrc tools/analyze_snr_distortions.cpp \
  -o /tmp/analyze_snr_distortions
/tmp/analyze_snr_distortions
```

## Deterministic filtering

All 24 scheduled voice frames remained decoded.

| Input | Reported quality |
|---|---:|
| Exact Codec2 waveform | 174.25 dB |
| 5.4 kHz first-order low-pass | 27.53 dB |
| 4 kHz first-order low-pass | 22.31 dB |
| 3 kHz first-order low-pass | 16.35 dB |
| 500-4000 Hz first-order band-pass | 12.29 dB |
| 200-4000 Hz first-order band-pass | 16.49 dB |
| Captured KV4P RF path | 16.69 dB |

The ideal value is dominated by the estimator's near-zero denominator. A
noiseless 200-4000 Hz model closely reproduces the hardware result, showing
that deterministic voice-band filtering establishes most of the observed
quality ceiling.

## Noise after 200-4000 Hz filtering

| Added AWGN | Injected wideband SNR | Reported quality |
|---:|---:|---:|
| 0% | infinite | 16.49 dB |
| 5% | 24.5 dB | 16.41 dB |
| 10% | 18.5 dB | 16.19 dB |
| 20% | 12.4 dB | 15.42 dB |
| 30% | 8.9 dB | 14.40 dB |
| 50% | 4.6 dB | 12.30 dB |
| 75% | 1.4 dB | 10.17 dB |
| 100% | -0.6 dB | 8.52 dB |

The response is monotonic but compressed. The decoder's ten-sample boxcar
provides about 10 dB processing gain for white noise, while deterministic
filter distortion remains as a roughly 16-17 dB ceiling. On the tested KV4P
path, approximately 4-6 dB represents noise and 16-17 dB represents the
practical maximum. Treat those values as hardware-specific calibration.
