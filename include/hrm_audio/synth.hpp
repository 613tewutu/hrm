#pragma once

#include <cstdint>
#include <vector>

namespace hrm_audio {

constexpr int sample_rate = 48000;
enum class Cue {
  Select,
  Insert,
  Remove,
  Pickup,
  Deliver,
  Store,
  Copy,
  Add,
  Subtract,
  Jump,
  EmptyInbox,
  Error,
  Success,
  Reset,
  Count
};

/// Generate deterministic mono PCM for a short chiptune effect, with silent endpoints.
/// @param cue Sound identity; the Count sentinel produces silence.
/// @return Owned 48 kHz signed 16-bit samples, ready for playback or export.
std::vector<std::int16_t> synthesize(Cue cue);
/// Compose an original eight-bar loop with pulse melody, arpeggios, triangle bass, and noise drums.
std::vector<std::int16_t> compose_music();

} // namespace hrm_audio
