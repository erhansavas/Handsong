// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 erhansavas
// micmeasure: records the default microphone for a few seconds and analyses it in
// memory only (nothing is written to disk). Reports overall level, tone level via a
// Goertzel filter relative to a local noise estimate, and the strongest spectral peaks.
//
// Usage: micmeasure <seconds> <toneHz> [--skip seconds]
#import <Foundation/Foundation.h>
#import <AudioToolbox/AudioToolbox.h>
#include <Accelerate/Accelerate.h>
#include <math.h>

enum { kRate = 48000 };
static float *g_buf; static size_t g_cap, g_len;

static void on_input(void *u, AudioQueueRef q, AudioQueueBufferRef b, const AudioTimeStamp *t, UInt32 n,
                     const AudioStreamPacketDescription *d) {
  (void)u; (void)t; (void)d;
  size_t got = b->mAudioDataByteSize / sizeof(float);
  if (g_len + got > g_cap) got = g_cap - g_len;
  memcpy(g_buf + g_len, b->mAudioData, got * sizeof(float));
  g_len += got;
  (void)n;
  AudioQueueEnqueueBuffer(q, b, 0, NULL);
}

static double goertzel(const float *x, size_t n, double f) {
  double w = 2 * M_PI * f / kRate, c = 2 * cos(w), s0 = 0, s1 = 0, s2 = 0;
  for (size_t i = 0; i < n; i++) {
    double win = 0.5 - 0.5 * cos(2 * M_PI * i / (n - 1));
    s0 = x[i] * win + c * s1 - s2; s2 = s1; s1 = s0;
  }
  double p = s1 * s1 + s2 * s2 - c * s1 * s2;
  return 20 * log10(sqrt(fmax(p, 1e-20)) / (n / 4.0) + 1e-12);
}

int main(int ac, char **av) {
  @autoreleasepool {
    double secs = ac > 1 ? atof(av[1]) : 3, tone = ac > 2 ? atof(av[2]) : 440, skip = 0;
    for (int i = 3; i + 1 < ac; i++) if (!strcmp(av[i], "--skip")) skip = atof(av[i + 1]);
    g_cap = (size_t)((secs + 1) * kRate); g_buf = calloc(g_cap, sizeof(float));

    AudioStreamBasicDescription f = {kRate, kAudioFormatLinearPCM, kAudioFormatFlagIsFloat | kAudioFormatFlagIsPacked,
      4, 1, 4, 1, 32, 0};
    AudioQueueRef q = NULL;
    OSStatus st = AudioQueueNewInput(&f, on_input, NULL, CFRunLoopGetCurrent(), kCFRunLoopCommonModes, 0, &q);
    if (st) { fprintf(stderr, "AudioQueueNewInput failed: %d\n", (int)st); return 2; }
    for (int i = 0; i < 6; i++) {
      AudioQueueBufferRef b;
      AudioQueueAllocateBuffer(q, 4800 * sizeof(float), &b);
      AudioQueueEnqueueBuffer(q, b, 0, NULL);
    }
    st = AudioQueueStart(q, NULL);
    if (st) { fprintf(stderr, "AudioQueueStart failed: %d\n", (int)st); return 3; }
    CFRunLoopRunInMode(kCFRunLoopDefaultMode, secs, false);
    AudioQueueStop(q, true);
    AudioQueueDispose(q, true);

    size_t off = (size_t)(skip * kRate);
    if (g_len <= off + kRate / 4) { printf("mic: too little audio (%zu samples)\n", g_len); return 4; }
    const float *x = g_buf + off; size_t n = g_len - off;
    double sum = 0, peak = 0;
    for (size_t i = 0; i < n; i++) { sum += x[i] * x[i]; peak = fmax(peak, fabs(x[i])); }
    double rms = sqrt(sum / n);
    if (peak == 0) { printf("mic: digital silence, microphone permission is probably not granted\n"); return 5; }

    double t = goertzel(x, n, tone);
    double noise = 0; int cnt = 0;
    for (double m = 0.75; m <= 1.25; m += 0.05) {
      if (fabs(m - 1) < 0.04) continue;
      noise += pow(10, goertzel(x, n, tone * m) / 10); cnt++;
    }
    noise = 10 * log10(noise / cnt);

    // Duty cycle: how much of the time the tone is present, in 50 ms hops.
    int hop = kRate / 20, frames = (int)(n / hop), on = 0;
    double thr = noise + 10;
    for (int i = 0; i < frames; i++) if (goertzel(x + i * hop, hop, tone) > thr) on++;

    // Spectrum, averaged across 8192-point Hann frames.
    int N = 8192, half = N / 2, logN = 13;
    FFTSetup fft = vDSP_create_fftsetup(logN, kFFTRadix2);
    float *win = malloc(N * sizeof(float)), *in = malloc(N * sizeof(float));
    float *re = malloc(half * sizeof(float)), *im = malloc(half * sizeof(float));
    double *mag = calloc(half, sizeof(double));
    vDSP_hann_window(win, N, vDSP_HANN_NORM);
    int segs = 0;
    for (size_t p = 0; p + N <= n; p += N / 2, segs++) {
      vDSP_vmul(x + p, 1, win, 1, in, 1, N);
      DSPSplitComplex z = {re, im};
      vDSP_ctoz((DSPComplex *)in, 2, &z, 1, half);
      vDSP_fft_zrip(fft, &z, 1, logN, FFT_FORWARD);
      for (int k = 1; k < half; k++) mag[k] += re[k] * re[k] + im[k] * im[k];
    }
    printf("mic: rms=%.1f dBFS peak=%.1f dBFS | tone %.0f Hz: %.1f dB, local noise %.1f dB, SNR %.1f dB, present in %d/%d hops\n",
           20 * log10(rms + 1e-12), 20 * log10(peak + 1e-12), tone, t, noise, t - noise, on, frames);
    // Purity: share of the 200-6000 Hz energy that sits within +-12 Hz of the tone.
    double inBand = 0, total = 0;
    for (int k = 1; k < half; k++) {
      double hz = (double)k * kRate / N;
      if (hz < 200 || hz > 6000) continue;
      total += mag[k];
      if (fabs(hz - tone) <= 12) inBand += mag[k];
    }
    // Gaps: 5 ms RMS windows far below the typical level, and the longest such run.
    int w = kRate / 200, wins = (int)(n / w), gapW = 0, run = 0, maxRun = 0;
    double *lev = malloc(wins * sizeof(double)), *srt = malloc(wins * sizeof(double));
    for (int i = 0; i < wins; i++) {
      double e = 0;
      for (int j = 0; j < w; j++) e += x[i * w + j] * x[i * w + j];
      lev[i] = sqrt(e / w); srt[i] = lev[i];
    }
    for (int i = 1; i < wins; i++) { double v = srt[i]; int j = i - 1; while (j >= 0 && srt[j] > v) { srt[j + 1] = srt[j]; j--; } srt[j + 1] = v; }
    double typical = srt[(int)(wins * 0.9)], gapThr = typical * 0.25;
    for (int i = 0; i < wins; i++) { if (lev[i] < gapThr) { gapW++; run++; if (run > maxRun) maxRun = run; } else run = 0; }
    printf("mic: purity=%.1f%% gaps=%.1f%% longestGap=%dms typicalLevel=%.1f dBFS\n", 100 * inBand / fmax(total, 1e-20),
           100.0 * gapW / wins, maxRun * 5, 20 * log10(typical + 1e-12));
    // Envelope sketch: 5 ms levels from the middle of the capture, 0-9 relative to typical.
    printf("mic: env ");
    for (int i = wins / 2; i < wins / 2 + 90 && i < wins; i++) {
      int d = (int)(10 * lev[i] / fmax(typical, 1e-9));
      putchar(d > 9 ? '9' : '0' + (d < 0 ? 0 : d));
    }
    printf("\n");
    // Phase analysis: demodulate at the strongest bin near the tone over 2 ms blocks and count
    // abrupt phase steps, which are what dropped, repeated or overlapped audio looks like.
    {
      double f0 = tone, bestP = -1e9;
      for (double c = tone - 40; c <= tone + 40; c += 1) { double g = goertzel(x, n, c); if (g > bestP) { bestP = g; f0 = c; } }
      int bl = kRate / 500, nb = (int)(n / bl), jumps = 0, lastJump = -1000;
      double prev = 0; double hist[64] = {0}; int histN = 0; bool have = false;
      for (int i = 0; i < nb; i++) {
        double re2 = 0, im2 = 0;
        for (int j = 0; j < bl; j++) { double t2 = 2 * M_PI * f0 * (i * bl + j) / kRate; re2 += x[i * bl + j] * cos(t2); im2 -= x[i * bl + j] * sin(t2); }
        double ph = atan2(im2, re2);
        if (have) {
          double d = ph - prev; while (d > M_PI) d -= 2 * M_PI; while (d < -M_PI) d += 2 * M_PI;
          if (fabs(d) > 0.6 && sqrt(re2 * re2 + im2 * im2) / bl > typical * 0.1) {
            if (i - lastJump > 1 && histN < 64) hist[histN++] = (i - lastJump) * 2.0;
            if (i - lastJump > 1) jumps++;
            lastJump = i;
          }
        }
        prev = ph; have = true;
      }
      double mean = 0; int cnt2 = 0; for (int i = 1; i < histN; i++) { mean += hist[i]; cnt2++; }
      printf("mic: carrier=%.0fHz phaseJumps=%.1f/s meanSpacing=%.1fms spacings:", f0, jumps / ((double)n / kRate), cnt2 ? mean / cnt2 : 0);
      for (int i = 1; i < histN && i < 16; i++) printf(" %.0f", hist[i]);
      printf("\n");
    }
    // High-resolution line list around the tone: distinguishes periodic sidebands from smear.
    {
      int L = 1 << 17, lg2 = 17; if ((size_t)L > n) { L = 1 << 16; lg2 = 16; }
      FFTSetup f2 = vDSP_create_fftsetup(lg2, kFFTRadix2);
      float *w2 = malloc(L * sizeof(float)), *b2 = malloc(L * sizeof(float)), *r2 = malloc(L / 2 * sizeof(float)), *i2 = malloc(L / 2 * sizeof(float));
      vDSP_hann_window(w2, L, vDSP_HANN_NORM); vDSP_vmul(x, 1, w2, 1, b2, 1, L);
      DSPSplitComplex z2 = {r2, i2}; vDSP_ctoz((DSPComplex *)b2, 2, &z2, 1, L / 2); vDSP_fft_zrip(f2, &z2, 1, lg2, FFT_FORWARD);
      double res = (double)kRate / L, mx = 0;
      int k0 = (int)((tone - 260) / res), k1 = (int)((tone + 260) / res);
      for (int k = k0; k <= k1; k++) { double m = r2[k] * r2[k] + i2[k] * i2[k]; if (m > mx) mx = m; }
      printf("mic: lines:");
      int shown = 0;
      for (int k = k0 + 2; k < k1 - 2 && shown < 16; k++) {
        double m = r2[k] * r2[k] + i2[k] * i2[k];
        bool lm = true; for (int d = -3; d <= 3; d++) if (d && r2[k + d] * r2[k + d] + i2[k + d] * i2[k + d] > m) lm = false;
        if (lm && m > mx * 0.05) { printf(" %.1f(%.0f)", k * res, 10 * log10(m / mx)); shown++; }
      }
      printf("\n");
    }
    printf("mic: top peaks:");
    for (int top = 0; top < 6; top++) {
      int best = 0; double bv = 0;
      for (int k = 3; k < half; k++) {
        double hz = (double)k * kRate / N;
        if (hz < 40 || hz > 12000) continue;
        if (mag[k] > bv && mag[k] >= mag[k - 1] && mag[k] >= mag[k + 1]) { bv = mag[k]; best = k; }
      }
      if (!best) break;
      printf(" %.0fHz(%.0fdB)", (double)best * kRate / N, 10 * log10(bv / fmax(segs, 1) + 1e-20) - 60);
      for (int k = fmax(1, best - 4); k <= best + 4 && k < half; k++) mag[k] = 0;
    }
    printf("\n");
    return 0;
  }
}
