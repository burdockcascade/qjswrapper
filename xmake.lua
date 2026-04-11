add_rules("mode.debug", "mode.release")
set_languages("cxx23")

add_requires("quickjs-ng 0.13.0", {build = true})
add_requires("catch2 3.x", {build = true})
add_requires("raylib 5.5", {build = true})

add_includedirs("include")

task("amalgamate")
    on_run(function ()
        -- Use the system python (or the one from your venv)
        local python = is_host("windows") and ".venv/Scripts/python.exe" or "python3"
        
        local entry = "src/qjswrapper.hpp"
        local output = "include/qjswrapper.hpp"
        local script = "tools/amalgamate.py"

        print("Running custom amalgamation script...")
        
        -- Arguments: script_path, entry_file, output_file, include_directory
        os.execv(python, {script, entry, output, "src"})
    end)
    set_menu {
        usage = "xmake amalgamate",
        description = "Generate a single header file for qjswrapper.",
    }

target("qjswrapper")
    set_kind("headeronly")
    add_headerfiles("src/*.hpp")

target("unit-tests")
    set_kind("binary")
    add_packages("catch2")
    add_packages("quickjs-ng")
    add_files("tests/*.cpp")

target("example_object")
    set_kind("binary")
    add_files("examples/example_object.cpp")
    add_packages("quickjs-ng")

target("example_class")
    set_kind("binary")
    add_files("examples/example_class.cpp")
    add_packages("quickjs-ng")

 target("example_demo")
     set_kind("binary")
     add_files("examples/example_demo.cpp")
     add_packages("quickjs-ng")

 target("example_module")
     set_kind("binary")
     add_files("examples/example_module.cpp")
     add_packages("quickjs-ng")

target("example_bytecode")
     set_kind("binary")
     add_files("examples/example_bytecode.cpp")
     add_packages("quickjs-ng")

target("example_raylib")
     set_kind("binary")
     add_files("examples/example_raylib.cpp")
     add_packages("quickjs-ng")
     add_packages("raylib")