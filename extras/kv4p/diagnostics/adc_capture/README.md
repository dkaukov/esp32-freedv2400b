# KV4P ADC capture

Captures ten seconds of signed PCM8 discriminator audio from the real Arduino
Audio Tools ADC path. The ADC configuration matches the receiver:

```cpp
config.use_apll = true;
config.sample_rate = 48400;
```

That request was measured for the hardware/software configuration used during
development. It is not a universal ESP32 setting; calibrate the actual board.

Inject a stable, independently generated 1200 Hz tone into the receiver and
run:

```bash
python tools/capture_adc_tone.py --port PORT --output adc_1200hz.wav
```

The host tool uploads the firmware, saves a 48 kHz mono WAV, measures the
steady-state tone, and reports inferred ADC sample rate and clock error. It
requires `pyserial` and `numpy`. A digitally inserted tone would not measure
the physical ADC clock.

To keep the real ADC read loop but replace every ADC value with a digitally
generated 1200 Hz waveform based on a nominal 48 kHz phase step:

```bash
python tools/capture_adc_tone.py --port PORT --inject-tone \
  --output injected_1200hz.wav
```

This mode validates PCM ordering, serial transfer, and WAV creation. Its tone
is 1200 Hz by construction and therefore cannot measure the physical ADC clock.

For the same diagnostic with APLL disabled:

```bash
python tools/capture_adc_tone.py --port PORT --inject-tone --no-apll \
  --output injected_1200hz_no_apll.wav
```
