add_rules("mode.debug", "mode.release")
set_languages("cxx23")

add_requires("quickjs-ng")

target("qjswrapper")
    set_kind("binary")
    add_files("src/*.cpp")
    add_packages("quickjs-ng")
