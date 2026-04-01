add_rules("mode.debug", "mode.release")
set_languages("cxx23")

add_requires("quickjs-ng", {optional = true})
add_requires("quickjs", {optional = true})
add_requires("raylib")

task("ship")
    on_run(function ()
        os.execv("qjsc", {"-N", "qjsc_ship", "-C", "-m", "-o", "src/js/ship.h", "src/js/ship.js"})
        print("JavaScript runtime compiled to src/js/runtime.h")
    end)
    set_menu {
        usage = "xmake compile-runtime",
        description = "Compile runtime.js to a C header using qjsc"
    }

target("qjswrapper")
    set_kind("binary")
    add_files("src/*.cpp")
    add_packages("quickjs-ng")
    add_packages("raylib")