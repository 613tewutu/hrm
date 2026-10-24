#pragma once

#include "hrm_audio/synth.hpp"

#include <memory>

namespace hrm_audio {

/// Own one shared audio device, a looping chiptune, and a bounded pool of effect voices.
/// Create a single instance per frontend, and call update from that frontend's UI loop.
class Audio {
public:
  /// Initialize audio when enabled; an unavailable device leaves a silent, usable frontend.
  explicit Audio(bool enabled = true);
  /// Stop playback and release voices, stream, and the audio device.
  ~Audio();
  Audio(const Audio &) = delete;
  Audio &operator=(const Audio &) = delete;
  /// Return whether an audio device is available.
  bool ready() const;
  /// Refill processed music buffers without allocating or changing game state.
  void update();
  /// Play a cached effect; ordinary cues are throttled during rapid interaction.
  /// @param cue Effect to play; Count is ignored.
  /// @param pan Stereo position from -1 (left) to 1 (right).
  void play(Cue cue, float pan = 0.0f);
  /// Stop active effects; the background music retains its position.
  void stop_effects();
  /// Enable or pause background music without resetting its playhead.
  void set_music(bool enabled);
  /// Enable effects, or stop active effects when disabled.
  void set_effects(bool enabled);
  /// Return the music toggle independent of device availability.
  bool music_enabled() const;
  /// Return the effects toggle independent of device availability.
  bool effects_enabled() const;

private:
  struct Impl;
  std::unique_ptr<Impl> impl_;
};

} // namespace hrm_audio
