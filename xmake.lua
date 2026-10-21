set_xmakever("3.0.0")
set_project("hrm")
set_version("0.1.0")
set_languages("c++20")
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

if has_config("gui") then
    add_requires("raylib 6.0")
end

target("hrm-core")
    set_kind("static")
    add_files("src/core/*.cpp")
    add_headerfiles("include/(hrm/*.hpp)")
    add_includedirs("include", {public = true})
target_end()

if has_config("tui") then
    target("hrm-tui")
        set_kind("binary")
        add_files("src/tui/*.cpp")
        add_deps("hrm-core")
        add_packages("ftxui", {components = {"component"}})
    target_end()
end

if has_config("gui") then
    target("hrm-gui")
        set_kind("binary")
        add_files("src/gui/*.cpp")
        add_deps("hrm-core")
        add_packages("raylib")
    target_end()
end
