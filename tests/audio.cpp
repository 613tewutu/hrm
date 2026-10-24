#include "hrm_audio/audio.hpp"

#include <cmath>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <set>
#include <stdexcept>
#include <string>

namespace {

/// Fail immediately when a synthesis or lifecycle invariant is violated.
void check(bool condition, const char *message) {
  if (!condition) {
    throw std::runtime_error(message);
  }
}

/// Write little-endian integer bytes for a portable PCM WAV header.
void integer(std::ostream &file, std::uint32_t value, int bytes) {
  for (int i = 0; i < bytes; ++i) {
    file.put(static_cast<char>((value >> (i * 8)) & 255));
  }
}

/// Export generated audio for manual listening without opening an audio device.
void export_wave(const std::filesystem::path &path, const std::vector<std::int16_t> &samples) {
  std::ofstream file(path, std::ios::binary);
  file.exceptions(std::ios::badbit | std::ios::failbit);
  const auto bytes = static_cast<std::uint32_t>(samples.size() * 2);
  file << "RIFF";
  integer(file, bytes + 36, 4);
  file << "WAVEfmt ";
  integer(file, 16, 4);
  integer(file, 1, 2);
  integer(file, 1, 2);
  integer(file, hrm_audio::sample_rate, 4);
  integer(file, hrm_audio::sample_rate * 2, 4);
  integer(file, 2, 2);
  integer(file, 16, 2);
  file << "data";
  integer(file, bytes, 4);
  for (const auto sample : samples) {
    integer(file, static_cast<std::uint16_t>(sample), 2);
  }
}

/// Check audible energy, headroom, zero endpoints, and DC offset.
void validate(const std::vector<std::int16_t> &samples) {
  check(!samples.empty() && samples.front() == 0 && samples.back() == 0, "unclean audio endpoints");
  double energy = 0.0;
  std::int64_t dc = 0;
  for (const auto sample : samples) {
    check(std::abs(static_cast<int>(sample)) < 30000, "audio clipped");
    energy += static_cast<double>(sample) * sample;
    dc += sample;
  }
  check(std::sqrt(energy / samples.size()) > 100, "silent or inaudible audio");
  check(std::abs(static_cast<double>(dc) / samples.size()) < 200, "excessive DC offset");
}

} // namespace

/// Verify synthesis and silent backend controls, optionally exporting a preview sound bank.
int main(int argc, char **argv) {
  try {
    const auto directory = argc > 1 ? std::filesystem::path(argv[1]) : std::filesystem::path();
    if (!directory.empty()) {
      std::filesystem::create_directories(directory);
    }
    std::set<std::vector<std::int16_t>> distinct;
    for (int i = 0; i < static_cast<int>(hrm_audio::Cue::Count); ++i) {
      const auto cue = static_cast<hrm_audio::Cue>(i);
      const auto samples = hrm_audio::synthesize(cue);
      validate(samples);
      check(samples == hrm_audio::synthesize(cue), "effect synthesis must be deterministic");
      check(distinct.insert(samples).second, "effects must have distinct sounds");
      if (!directory.empty()) {
        export_wave(directory / ("effect-" + std::to_string(i) + ".wav"), samples);
      }
    }
    const auto music = hrm_audio::compose_music();
    validate(music);
    check(music == hrm_audio::compose_music(), "music must be deterministic");
    check(music.size() > hrm_audio::sample_rate * 15, "music loop is unexpectedly short");
    if (!directory.empty()) {
      export_wave(directory / "music.wav", music);
    }
    hrm_audio::Audio silent(false);
    check(!silent.ready(), "disabled audio must not initialize a device");
    silent.set_music(false);
    silent.set_effects(false);
    check(!silent.music_enabled() && !silent.effects_enabled(), "mute toggles did not apply");
    silent.update();
    silent.play(hrm_audio::Cue::Success);
    silent.stop_effects();
    silent.set_music(true);
    silent.set_effects(true);
    check(silent.music_enabled() && silent.effects_enabled(), "unmute toggles did not apply");
    std::cout << "Chiptune synthesis and silent audio controls passed\n";
  } catch (const std::exception &error) {
    std::cerr << error.what() << '\n';
    return 1;
  }
}
