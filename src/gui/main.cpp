#include "hrm_audio/audio.hpp"

#include <raylib.h>

#include <array>
#include <string>

/// Preview the shared chiptune and sound bank in the graphical frontend shell.
int main(int argc, char **argv) {
  bool enabled = true;
  for (int i = 1; i < argc; ++i) {
    if (std::string(argv[i]) == "--no-audio") {
      enabled = false;
    }
  }
  SetTraceLogLevel(LOG_NONE);
  InitWindow(960, 640, "Human Resource Machine - Sound room");
  SetTargetFPS(60);
  {
    hrm_audio::Audio audio(enabled);
    const std::array<const char *, 14> names = {"Select",
      "Insert",
      "Remove",
      "Pickup",
      "Deliver",
      "Store",
      "Copy",
      "Add",
      "Subtract",
      "Jump",
      "Empty inbox",
      "Error",
      "Success",
      "Reset"};
    std::size_t selected = 0;
    while (!WindowShouldClose()) {
      audio.update();
      if (IsKeyPressed(KEY_M)) {
        audio.set_music(!audio.music_enabled());
      }
      if (IsKeyPressed(KEY_N)) {
        audio.set_effects(!audio.effects_enabled());
      }
      if (IsKeyPressed(KEY_RIGHT)) {
        selected = (selected + 1) % names.size();
      }
      if (IsKeyPressed(KEY_LEFT)) {
        selected = (selected + names.size() - 1) % names.size();
      }
      if (IsKeyPressed(KEY_SPACE)) {
        audio.play(static_cast<hrm_audio::Cue>(selected));
      }
      BeginDrawing();
      ClearBackground(Color{25, 27, 38, 255});
      DrawText("HUMAN RESOURCE MACHINE", 64, 56, 30, Color{144, 213, 188, 255});
      DrawText("8-BIT SOUND ROOM", 64, 110, 22, RAYWHITE);
      const char *sprite[] = {"  #######", "  # ### #", " #########", "  #######", "  ##   ##"};
      for (int row = 0; row < 5; ++row) {
        for (int column = 0; sprite[row][column]; ++column) {
          if (sprite[row][column] == '#') {
            DrawRectangle(360 + column * 16, 225 + row * 12, 16, 12, GOLD);
          }
        }
      }
      DrawText(names[selected], 64, 370, 32, GOLD);
      DrawText("LEFT / RIGHT: choose effect     SPACE: play", 64, 430, 20, RAYWHITE);
      DrawText(audio.music_enabled() ? "M: music ON" : "M: music OFF", 64, 485, 20, SKYBLUE);
      DrawText(audio.effects_enabled() ? "N: effects ON" : "N: effects OFF", 350, 485, 20, SKYBLUE);
      if (!audio.ready()) {
        DrawText("Audio unavailable / disabled", 64, 540, 20, GRAY);
      }
      EndDrawing();
    }
  }
  CloseWindow();
  return 0;
}
