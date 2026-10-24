#include "hrm_audio/audio.hpp"

#include <raylib.h>

#include <algorithm>
#include <array>
#include <chrono>
#include <vector>

namespace hrm_audio {

struct Audio::Impl {
  struct Bank {
    Sound source{};
    std::array<Sound, 3> voices{};
    std::chrono::steady_clock::time_point last{};
    std::size_t next = 0;
  };
  bool device = false, music = true, effects = true;
  AudioStream stream{};
  std::array<Bank, static_cast<std::size_t>(Cue::Count)> banks{};
  std::vector<std::int16_t> track;
  std::array<std::int16_t, 1024> buffer{};
  std::size_t cursor = 0;
  std::chrono::steady_clock::time_point duck_until{};

  /// Initialize the backend without requiring a raylib graphics window.
  void initialize(bool enabled) {
    if (!enabled) {
      return;
    }
    SetTraceLogLevel(LOG_NONE);
    InitAudioDevice();
    device = IsAudioDeviceReady();
    if (!device) {
      return;
    }
    for (std::size_t i = 0; i < banks.size(); ++i) {
      auto samples = synthesize(static_cast<Cue>(i));
      Wave wave{};
      wave.frameCount = static_cast<unsigned int>(samples.size());
      wave.sampleRate = sample_rate;
      wave.sampleSize = 16;
      wave.channels = 1;
      wave.data = samples.data();
      auto &bank = banks[i];
      bank.source = LoadSoundFromWave(wave);
      if (IsSoundValid(bank.source)) {
        for (auto &voice : bank.voices) {
          voice = LoadSoundAlias(bank.source);
          SetSoundVolume(voice, 0.65f);
        }
      }
    }
    track = compose_music();
    SetAudioStreamBufferSizeDefault(static_cast<int>(buffer.size()));
    stream = LoadAudioStream(sample_rate, 16, 1);
    if (IsAudioStreamValid(stream)) {
      SetAudioStreamVolume(stream, 0.3f);
      PlayAudioStream(stream);
    }
  }

  ~Impl() {
    if (!device) {
      return;
    }
    if (IsAudioStreamValid(stream)) {
      StopAudioStream(stream);
      UnloadAudioStream(stream);
    }
    for (auto &bank : banks) {
      for (auto &voice : bank.voices) {
        if (IsSoundValid(voice)) {
          StopSound(voice);
          UnloadSoundAlias(voice);
        }
      }
      if (IsSoundValid(bank.source)) {
        UnloadSound(bank.source);
      }
    }
    CloseAudioDevice();
  }
};

Audio::Audio(bool enabled)
  : impl_(std::make_unique<Impl>()) {
  impl_->initialize(enabled);
}

Audio::~Audio() = default;

bool Audio::ready() const {
  return impl_->device;
}

void Audio::update() {
  auto &state = *impl_;
  if (!state.device || !state.music || !IsAudioStreamValid(state.stream) || state.track.empty()) {
    return;
  }
  SetAudioStreamVolume(
    state.stream,
    std::chrono::steady_clock::now() < state.duck_until ? 0.12f : 0.3f
  );
  for (int refill = 0; refill < 2 && IsAudioStreamProcessed(state.stream); ++refill) {
    for (auto &sample : state.buffer) {
      sample = state.track[state.cursor];
      state.cursor = (state.cursor + 1) % state.track.size();
    }
    UpdateAudioStream(state.stream, state.buffer.data(), static_cast<int>(state.buffer.size()));
  }
}

void Audio::play(Cue cue, float pan) {
  auto &state = *impl_;
  const auto index = static_cast<std::size_t>(cue);
  if (!state.device || !state.effects || index >= state.banks.size()) {
    return;
  }
  auto &bank = state.banks[index];
  const auto now = std::chrono::steady_clock::now();
  if (cue != Cue::Error && cue != Cue::Success && now - bank.last < std::chrono::milliseconds(55)) {
    return;
  }
  auto &voice = bank.voices[bank.next];
  if (!IsSoundValid(voice)) {
    return;
  }
  bank.last = now;
  bank.next = (bank.next + 1) % bank.voices.size();
  StopSound(voice);
  SetSoundPan(voice, std::clamp(pan, -1.0f, 1.0f));
  PlaySound(voice);
  if (cue == Cue::Error || cue == Cue::Success) {
    state.duck_until = now + std::chrono::milliseconds(800);
  }
}

void Audio::stop_effects() {
  if (!impl_->device) {
    return;
  }
  for (auto &bank : impl_->banks) {
    for (auto &voice : bank.voices) {
      if (IsSoundValid(voice)) {
        StopSound(voice);
      }
    }
  }
}

void Audio::set_music(bool enabled) {
  auto &state = *impl_;
  if (state.music == enabled) {
    return;
  }
  state.music = enabled;
  if (state.device && IsAudioStreamValid(state.stream)) {
    if (enabled) {
      ResumeAudioStream(state.stream);
    } else {
      PauseAudioStream(state.stream);
    }
  }
}

void Audio::set_effects(bool enabled) {
  impl_->effects = enabled;
  if (!enabled) {
    stop_effects();
  }
}

bool Audio::music_enabled() const {
  return impl_->music;
}

bool Audio::effects_enabled() const {
  return impl_->effects;
}

} // namespace hrm_audio
