add_rules("mode.debug", "mode.release")
set_languages("cxx23")

add_requires("quickjs-ng 0.13.0")
add_requires("catch2 3.x")

target("unit-tests")
    set_kind("binary")
    add_packages("catch2")
    add_packages("quickjs-ng")
    add_files("tests/*.cpp")
    add_includedirs("src")

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