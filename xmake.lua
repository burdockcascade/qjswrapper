add_rules("mode.debug", "mode.release")
set_languages("cxx23")

add_requires("quickjs-ng")
add_requires("raylib")

target("qjswrapper")
    set_kind("binary")
    add_files("src/*.cpp")
    add_packages("quickjs-ng")
    add_packages("raylib")
