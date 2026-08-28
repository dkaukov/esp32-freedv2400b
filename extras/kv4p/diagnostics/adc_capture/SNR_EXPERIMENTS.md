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

## Symbol-model experiment

`tools/analyze_symbol_models.cpp` compares three experimental estimators using
the decoder's adaptive 10-sample chip windows. Run it with:

```bash
g++ -std=c++11 -O2 -Isrc tools/analyze_symbol_models.cpp \
  -o /tmp/analyze_symbol_models
/tmp/analyze_symbol_models
```

| Input | Polarity regression | Per-chip ramp | Transition templates |
|---|---:|---:|---:|
| Exact Codec2 waveform | infinite | infinite | 54.17 dB |
| 200-4000 Hz band-pass | 9.50 dB | 11.28 dB | 15.73 dB |
| Band-pass + 20% AWGN | 7.53 dB | 9.16 dB | 10.65 dB |
| Band-pass + 50% AWGN | 2.99 dB | 4.62 dB | 4.12 dB |
| Captured KV4P RF path | 5.66 dB | 8.33 dB | 8.55 dB |

The polarity model fits one global `sample = a * polarity + b` model. The ramp
model fits `sample = a * time + b` independently inside every chip; it removes
transition slope, but its two fitted parameters per ten samples also absorb
noise and therefore make its SNR optimistic.

The transition-template result is the most defensible experiment: it learns
four ten-sample shapes (negative/positive combinations of previous and current
chip polarity) from the first half of a recording, then evaluates the second
half with only a global gain and offset fit. Its response to added noise is
predictable and it measures the real RF capture at about 8.6 dB. It is still a
decision-directed modem-quality metric, not calibrated RF SNR.

These estimators remain host-side experiments. The public decoder API and its
Codec2-compatible `discriminatorSnrDb` calculation are unchanged.
