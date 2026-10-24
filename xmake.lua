set_xmakever("3.0.0")
set_project("hrm")
set_version("0.1.0")
set_warnings("all", "extra")

add_rules("mode.debug", "mode.release")

option("tui")
  set_default(true)
  set_showmenu(true)
  set_description("Build the FTXUI terminal frontend")
option_end()

option("gui")
  set_default(true)
  set_showmenu(true)
  set_description("Build the raylib graphical frontend")
option_end()

if has_config("tui") then
  add_requires("ftxui 6.1.9")
end

if has_config("tui") or has_config("gui") then
  add_requires("yaml-cpp 0.8.0")
end

if has_config("tui") or has_config("gui") then
  add_requires("raylib 6.0")
end

target("hrm-core")
  set_kind("static")
  set_languages("c++11")
  add_files("src/core/*.cpp")
  add_headerfiles("include/(hrm/*.hpp)")
  add_includedirs("include", {public = true})
target_end()

target("hrm-oj")
  set_kind("binary")
  set_languages("c++11")
  add_files("src/oj/main.cpp")
  add_deps("hrm-core")
target_end()

if has_config("tui") or has_config("gui") then
  target("hrm-audio-synth")
    set_kind("static")
    set_languages("c++20")
    add_files("src/audio/synth.cpp")
    add_headerfiles("include/(hrm_audio/synth.hpp)")
    add_includedirs("include", {public = true})
  target_end()

  target("hrm-audio")
    set_kind("static")
    set_languages("c++20")
    add_files("src/audio/audio.cpp")
    add_headerfiles("include/(hrm_audio/audio.hpp)")
    add_includedirs("include", {public = true})
    add_deps("hrm-audio-synth")
    add_packages("raylib")
  target_end()

  target("hrm-audio-tests")
    set_kind("binary")
    set_default(false)
    set_languages("c++20")
    add_files("tests/audio.cpp")
    add_deps("hrm-audio")
  target_end()

  target("hrm-app")
    set_kind("static")
    set_languages("c++20")
    add_rules("utils.bin2c")
    add_files("assets/game.yaml", {rule = "utils.bin2c"})
    add_files("src/app/*.cpp")
    add_headerfiles("include/(hrm_app/*.hpp)")
    add_includedirs("include", {public = true})
    add_deps("hrm-core")
    add_packages("yaml-cpp")
  target_end()

  target("hrm-app-tests")
    set_kind("binary")
    set_default(false)
    set_languages("c++20")
    add_files("tests/app.cpp")
    add_deps("hrm-app")
  target_end()
end

if has_config("tui") then
  target("hrm-tui")
    set_kind("binary")
    set_languages("c++20")
    add_files("src/tui/*.cpp")
    add_deps("hrm-app", "hrm-audio")
    add_packages("ftxui", {components = {"component"}})
  target_end()
end

if has_config("gui") then
  target("hrm-gui")
    set_kind("binary")
    set_languages("c++20")
    add_files("src/gui/*.cpp")
    add_deps("hrm-core", "hrm-audio")
    add_packages("raylib")
  target_end()
end
