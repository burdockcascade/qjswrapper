add_rules("mode.debug", "mode.release")
set_languages("cxx23")

add_requires("quickjs-ng 0.13.0")

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
    set_kind("headeronly")
    add_headerfiles("src/lib/*.hpp")
    add_packages("quickjs-ng")

target("test_math")
    set_kind("binary")
    add_files("src/tests/test_math.cpp")
    add_deps("qjswrapper")
    add_packages("quickjs-ng") -- fixme: should be transitive from qjswrapper, but for some reason isn't

target("test_ship")
    set_kind("binary")
    add_files("src/tests/test_ship.cpp")
    add_deps("qjswrapper")
    add_packages("quickjs-ng") -- fixme: should be transitive from qjswrapper, but for some reason isn't
    
target("test_errors")
    set_kind("binary")
    add_files("src/tests/test_errors.cpp")
    add_deps("qjswrapper")
    add_packages("quickjs-ng")