set_project("SanctifiedVidette")
set_version("0.1.0")

set_languages("c++20")

add_rules("mode.debug", "mode.release")

add_rules("plugin.compile_commands.autoupdate")

add_requires("glfw")

add_requires("glbinding")

add_requires("imgui", {
    configs = {
        glfw = true,
        opengl3 = true
    }
})

target("SanctifiedVidette")
    set_kind("binary")
    add_files("src/*.cpp")
    add_packages("glfw", "glbinding", "imgui")
    add_defines("GLFW_INCLUDE_NONE")

    if is_plat("windows") then
        -- keep windows min/max macros from colliding with std::min/std::max
        add_defines("NOMINMAX", "WIN32_LEAN_AND_MEAN")
    end

    -- copy shaders next to the exe after each build, so that we can load it at runtime
    after_build(function (target)
        local shaderdir = path.join(target:targetdir(), "shaders")
        os.mkdir(shaderdir)
        os.cp(path.join(os.projectdir(), "shaders", "*"), shaderdir)
    end)
