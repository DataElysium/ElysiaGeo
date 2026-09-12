set_project("ElysiaGeo")
set_version("0.1.0")
set_languages("cxx20")
add_rules("mode.debug", "mode.release")
option("viewer", {default = true, description = "Build the optional raylib tactical map"})
if has_config("viewer") then
    add_requires("raylib 6.0")
end
target("elysia-geo")
    set_kind("static")
    add_files("src/*.cpp")
    add_includedirs("include", {public = true})
    add_headerfiles("include/(elysia_geo/*.hpp)")
for _, name in ipairs({"geo-query", "geo-tests"}) do
    target(name)
        set_kind("binary")
        add_deps("elysia-geo")
        add_files(name == "geo-tests" and "tests/*.cpp" or "apps/query.cpp")
        add_defines('ELYSIA_MAP_FILE="' .. path.absolute("maps/earth/land.elygeo") .. '"')
end
if has_config("viewer") then
    target("geo-viewer")
        set_kind("binary")
        add_deps("elysia-geo")
        add_packages("raylib")
        add_files("apps/viewer*.cpp")
        add_defines('ELYSIA_MAP_FILE="' .. path.absolute("maps/earth/land.elygeo") .. '"')
end
