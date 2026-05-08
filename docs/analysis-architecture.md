# Analysis Architecture

This page describes the internal design of Switchblade's audio analysis pipeline as of v0.4.0.

---

## Pipeline Overview

```
AudioFile
   │
   ▼
mixToMono()
   │
   ├─── Percussive path ─────────────────────────────────────────────────┐
   │     SpectralFlux::process()                                          │
   │       → Hann window → real FFT → HWR magnitude diff → novelty curve │
   │     AdaptiveThreshold (median+MAD gate)                              │
   │     Local-maxima peak picker                                         │
   │     Silence gate (−40 dBFS)                                          │
   │     snapToZeroCrossing (±5 ms)                                       │
   │                                                                       │
   ├─── Tonal path ──────────────────────────────────────────────────────┤
   │     TextureAnalyzer::analyze()                                        │
   │       → FFT per frame → spectral centroid curve                      │
   │       → RingVariance sliding std-dev → stability score               │
   │     Stable-region onset detection                                     │
   │     snapToZeroCrossing (±5 ms)                                       │
   │                                                                       │
   └─── Auto path ───────────────────────────────────────────────────────┘
         AnalysisEngine classifier
           → SpectralFlux onset rate
           → PitchDetector YIN scan (clarity score)
         Dispatch to Percussive or Tonal

Result: std::vector<Transient>
   │
   ▼
computeNaturalEnds() — energy-decay endpoint per slice
   │
   ▼
Density Guard — cap slice count
   │
   ▼
Export (WAV slices, named per mode)
```

---

## SpectralFlux

**File:** `Source/Analysis/SpectralFlux.h/.cpp`

Computes the half-wave-rectified spectral flux (HRSF) novelty function:

```
SF(n) = Σ_k  max(0,  |X(n,k)| - |X(n-1,k)|)
```

Implemented as an offline batch processor (`process(span<float> mono) → vector<float>`). Key implementation choices:

- **Hann window** — applied in-place before each FFT; zero at frame edges prevents discontinuity artifacts.
- **In-place prevMag update** — magnitudes are written back to `prevMag_` during the HWR loop, eliminating the `curMag_` intermediate copy and `std::swap`.
- **Half-fill scratch** — `std::copy` of N real samples then `std::fill` of the N imaginary positions (2× cheaper than the previous fill-2N-then-copy-N approach).
- **`max(0, x)` branchless** — compiles to MAXPS on x86; the branch version does not auto-vectorise.
- Output is sqrt-normalised: `v[n] = √(SF(n)/nBins)`.

---

## AdaptiveThreshold

**File:** `Source/Analysis/AdaptiveThreshold.h`

Sliding-window gate with median + MAD estimator:

```
threshold(n) = max(floor,  median(W) + k × 1.4826 × MAD(W))
```

- `k = 2.4` (default) — MAD multiplier; 1.4826 scales MAD to a consistent σ estimator under Gaussian noise.
- `floorAbs = 0.008` — prevents spurious triggers in near-silent material.
- Window size 65 frames (~750 ms at 11.6 ms/hop) — wide enough to smooth ghost triggers across long files.
- **Pre-allocated scratch** (`mutable vector<float> scratch_`) reuses the same buffer for both sort passes, eliminating the per-call heap allocation.

---

## TransientDetector

**File:** `Source/Analysis/TransientDetector.h/.cpp`

Wraps SpectralFlux + AdaptiveThreshold + peak-picker + zero-crossing snap.

### Onset estimation

The local maximum in the novelty curve at frame `i` occurs when the burst's energy centre passes the Hann-window midpoint. The estimated onset is:

```
rawSampleIndex = i × hop + fftN/2
```

This places the estimate at `i×hop + 1024` (centre of the analysis window), which approximates the actual burst start to within one hop (~11 ms).

> **v0.3 bug**: the formula was `i × hop + fftN/4`. This placed estimates ~512 samples before the burst, causing the silence gate to erase all non-leading transients.

### Silence gate

After peak-picking, candidates are filtered:

```
localPeak = max |mono[rawSampleIndex .. rawSampleIndex + 10ms]|
keep if  localPeak ≥ filePeak × 0.01   (−40 dBFS)
```

### Leading-content fallback

Files that start immediately with loud content (common one-shots) have no detectable local maximum: frame 0 is never checked by the `i=1..N-2` loop. If no candidates survive the silence gate **and** the first two hops have RMS > −60 dBFS, a synthetic onset at sample 0 is added.

---

## TextureAnalyzer

**File:** `Source/Analysis/TextureAnalyzer.h/.cpp`

Detects stable-timbre regions (pads, sustained notes, melodic phrases).

1. Per-frame Hann-windowed FFT → spectral centroid `c(n) = Σ k·|X[k]| / Σ |X[k]|`.
2. **RingVariance** computes sliding std-dev of the centroid in O(1) per frame using Welford's online formula over a ring buffer.
3. Stability score: `1 − min(1, σ / maxσ / threshold)`.
4. Transitions from `stable=false → stable=true` (with active RMS) are slice candidates.

---

## PitchDetector

**File:** `Source/Analysis/PitchDetector.h`

Implements the **YIN** algorithm (de Cheveigné & Kawahara 2002) on a single frame:

1. Difference function `d(τ) = Σ (x_t - x_{t+τ})²`
2. Cumulative mean normalised: `d'(τ) = d(τ) / [(1/τ) Σ d(j)]`
3. First τ where `d'(τ) < threshold` (default 0.15) is the estimated period.
4. Parabolic interpolation for sub-sample accuracy.
5. `clarity = 1 − d'(τ_best)` — returned with the F0 estimate.

Complexity: O(W²) per frame. For the auto-mode classifier only 4–8 frames are scanned; this is fast enough in practice. A future optimisation could replace with FFT-based autocorrelation (O(W log W)).

---

## RingVariance

**File:** `Source/Analysis/TextureAnalyzer.cpp` (internal class)

Maintains a running mean and sum-of-squares over the last N values using a circular buffer:

```cpp
float push(float x) {
    if (full) { sum -= old; sumSq -= old*old; }
    // insert x at head, advance head
    sum += x;  sumSq += x*x;
    return sqrt(max(0, sumSq/n - (sum/n)²));
}
```

Cost: O(1) per push, O(N) construction. Replaces `vector::erase(begin())` which was O(N) per push.

---

## ZeroCrossing

**File:** `Source/Analysis/ZeroCrossing.h/.cpp`

`snapToZeroCrossing(span, target, radius)` searches `[target-radius, target+radius]` for the nearest sample-pair sign change (`a × b < 0`). Returns the index of the sample following the crossing, or `target` if no crossing is found. Used by every analysis path to align slice boundaries.
