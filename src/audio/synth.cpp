#include "hrm_audio/synth.hpp"

#include <algorithm>
#include <cmath>

namespace hrm_audio {
namespace {

enum class Wave { Pulse, Triangle, Noise, Sine };
constexpr double pi = 3.141592653589793;

/// Mix an enveloped oscillator into an existing buffer, retaining headroom for other voices.
///
/// @param mix Mono accumulator at sample_rate; overlapping notes are added in place.
/// @param start Start offset in seconds.
/// @param duration Note length in seconds, including attack and release.
/// @param frequency Initial oscillator frequency in hertz; ignored by noise.
/// @param end_frequency Final frequency reached by the linear pitch sweep.
/// @param wave Oscillator shape or deterministic noise source.
/// @param gain Linear amplitude before mixing and final quantization.
/// @param seed Reproducible noise seed, local to this note.
void note(
  std::vector<double> &mix,
  double start,
  double duration,
  double frequency,
  double end_frequency,
  Wave wave,
  double gain,
  std::uint32_t seed = 1
) {
  const auto offset = static_cast<std::size_t>(start * sample_rate);
  const auto count = static_cast<std::size_t>(duration * sample_rate);
  double phase = 0.0;
  for (std::size_t i = 0; i < count && offset + i < mix.size(); ++i) {
    const double time = static_cast<double>(i) / sample_rate;
    const double position = static_cast<double>(i) / std::max<std::size_t>(1, count - 1);
    const double envelope = std::min(1.0, time / 0.003) *
      std::min(1.0, (1.0 - position) * duration / 0.012) * (1.0 - position * 0.55);
    phase += (frequency + (end_frequency - frequency) * position) / sample_rate;
    phase -= std::floor(phase);
    double value = 0.0;
    switch (wave) {
    case Wave::Pulse:
      value = phase < 0.25 ? 0.7 : -0.233333333;
      break;
    case Wave::Triangle:
      value = 1.0 - 4.0 * std::abs(phase - 0.5);
      break;
    case Wave::Noise:
      seed = seed * 1664525u + 1013904223u;
      value = static_cast<double>(seed >> 16) / 32767.5 - 1.0;
      break;
    case Wave::Sine:
      value = std::sin(phase * 2.0 * pi);
      break;
    }
    mix[offset + i] += gain * envelope * value;
  }
}

/// Quantize the timbre while preserving a smooth release and a nonclipping 16-bit output.
std::vector<std::int16_t> pcm(const std::vector<double> &mix) {
  std::vector<std::int16_t> result;
  result.reserve(mix.size());
  for (const auto value : mix) {
    const auto quantized = std::round(std::clamp(value, -0.9, 0.9) * 128.0) / 128.0;
    result.push_back(static_cast<std::int16_t>(quantized * 32767));
  }
  if (!result.empty()) {
    result.front() = result.back() = 0;
  }
  return result;
}

/// Convert a MIDI note into equal-tempered frequency.
double pitch(int midi) {
  return 440.0 * std::pow(2.0, (midi - 69) / 12.0);
}

} // namespace

std::vector<std::int16_t> synthesize(Cue cue) {
  const double duration = cue == Cue::Success ? 0.65 : cue == Cue::Error ? 0.32 : 0.14;
  std::vector<double> mix(static_cast<std::size_t>(duration * sample_rate));
  switch (cue) {
  case Cue::Select:
    note(mix, 0, 0.03, 700, 900, Wave::Pulse, 0.24);
    break;
  case Cue::Insert:
    note(mix, 0, 0.07, 500, 1100, Wave::Pulse, 0.3);
    break;
  case Cue::Remove:
    note(mix, 0, 0.08, 700, 220, Wave::Pulse, 0.28);
    break;
  case Cue::Pickup:
  case Cue::Copy:
    note(mix, 0, 0.09, cue == Cue::Pickup ? 380 : 520, 1100, Wave::Pulse, 0.3);
    note(mix, 0, 0.02, 0, 0, Wave::Noise, 0.06);
    break;
  case Cue::Deliver:
  case Cue::Store:
    note(mix, 0, 0.09, 520, cue == Cue::Deliver ? 180 : 280, Wave::Triangle, 0.4);
    note(mix, 0.045, 0.055, 880, 880, Wave::Pulse, 0.18);
    break;
  case Cue::Add:
  case Cue::Subtract:
    note(mix, 0, 0.05, 660, 660, Wave::Pulse, 0.28);
    note(mix, 0.05, 0.06, cue == Cue::Add ? 990 : 440, 660, Wave::Pulse, 0.28);
    break;
  case Cue::Jump:
    note(mix, 0, 0.09, 300, 1400, Wave::Pulse, 0.28);
    break;
  case Cue::EmptyInbox:
    note(mix, 0, 0.06, 440, 330, Wave::Triangle, 0.22);
    break;
  case Cue::Error:
    note(mix, 0, 0.13, 220, 130, Wave::Pulse, 0.32);
    note(mix, 0.14, 0.16, 150, 75, Wave::Pulse, 0.32);
    break;
  case Cue::Success:
    for (int i = 0; i < 4; ++i) {
      const int melody[] = {72, 76, 79, 84};
      note(mix, i * 0.12, 0.22, pitch(melody[i]), pitch(melody[i]), Wave::Pulse, 0.32);
      note(mix, i * 0.12, 0.22, pitch(melody[i] - 12), pitch(melody[i] - 12), Wave::Triangle, 0.1);
    }
    break;
  case Cue::Reset:
    note(mix, 0, 0.11, 1200, 250, Wave::Triangle, 0.3);
    break;
  case Cue::Count:
    break;
  }
  return pcm(mix);
}

std::vector<std::int16_t> compose_music() {
  constexpr double step = 60.0 / 112.0 / 4.0;
  std::vector<double> mix(static_cast<std::size_t>(128 * step * sample_rate));
  const int roots[] = {48, 45, 53, 55, 48, 45, 53, 55};
  const int melody[] = {72,
    0,
    76,
    79,
    0,
    76,
    74,
    0,
    72,
    0,
    67,
    0,
    69,
    71,
    74,
    0,
    76,
    0,
    79,
    81,
    0,
    79,
    76,
    0,
    74,
    0,
    72,
    0,
    71,
    69,
    67,
    0};
  for (int i = 0; i < 128; ++i) {
    const int root = roots[i / 16];
    const double start = i * step;
    if (const auto lead = melody[i % 32]) {
      const auto frequency = pitch(lead + (i >= 64 ? 0 : -12));
      note(mix, start, step * 0.85, frequency, frequency, Wave::Pulse, 0.17);
    }
    const int chord[] = {0, (i / 16) % 4 == 1 ? 3 : 4, 7, 12};
    const auto arpeggio = pitch(root + 12 + chord[i % 4]);
    note(mix, start, step * 0.65, arpeggio, arpeggio, Wave::Pulse, 0.07);
    if (i % 4 == 0) {
      const auto bass = pitch(root - 12);
      note(mix, start, step * 3.6, bass, bass, Wave::Triangle, 0.16);
      note(mix, start, 0.09, 140, 45, Wave::Sine, 0.15);
    }
    if (i % 8 == 4) {
      note(mix, start, 0.055, 0, 0, Wave::Noise, 0.07, static_cast<std::uint32_t>(i + 1));
    }
    if (i % 2 == 0) {
      note(mix, start, 0.018, 0, 0, Wave::Noise, 0.035, static_cast<std::uint32_t>(i + 7));
    }
  }
  return pcm(mix);
}

} // namespace hrm_audio
