# RF ADC captures

`rf_2400b_11223344556670_48400_apll_pcm8.wav` is a ten-second capture of the
KV4P-compatible receive path while a second module continuously transmitted
the FreeDV 2400B payload `11 22 33 44 55 66 70` on 446.000 MHz.

Capture settings were ESP32-WROOM, Arduino Audio Tools ADC, APLL enabled,
48,400 samples/s requested, input gain 16, SA818 wide bandwidth, filters off,
and receiver volume 8. The WAV is mono unsigned PCM8 at a nominal 48 kHz.
The matching `_s8.raw` file contains the same samples as headerless signed
PCM8.

On capture, the library reported 233 voice frames, including 218 exact payload
matches, and finished synchronized with a 16.69 dB discriminator SNR estimate.
These are hardware-development measurements, not universal calibration values.

Additional captures characterize the same complete DAC, SA818 TX, RF, SA818
RX discriminator, and ESP32 ADC path:

- `rf_carrier_*`: keyed carrier with zero modulation;
- `rf_sine_1200hz_*`: fixed 1200 Hz modulation;
- `rf_stepped_300_600_1200_2400_3600hz_*`: one-second stepped tones;
- `rf_square_25hz_*`: alternating levels every 20 ms.

The unmodulated carrier measured 15.23 PCM8 RMS AC noise. The 1200 Hz tone
measured 56.61 PCM8 RMS with approximately 7.7% harmonic distortion. Relative
tone response was approximately flat from 300 through 1200 Hz, -0.6 dB at
2400 Hz, and -1.6 dB at 3600 Hz. The 25 Hz alternating-level capture shows
strong AC-coupling/high-pass behavior and should not be interpreted as a
DC-capable step response.
