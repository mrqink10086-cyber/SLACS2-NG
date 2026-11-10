-- include subprojects
includes("lib/commonlibsse-ng")

-- set project constants
set_project("SLACS2 NG")
set_version("1.0.5")
set_license("GPL-3.0")
set_languages("c++23")
set_encodings("utf-8")
set_warnings("allextra")

-- add common rules
add_rules("mode.debug", "mode.releasedbg")
add_rules("plugin.vsxmake.autoupdate")

-- define targets
target("SLACS2 NG")
    add_rules("commonlibsse-ng.plugin", {
        name = "SLACS2 NG",
        author = "mrqink10086-cyber",
        description = "SLACS2 NG: C++ SKSE port of SLACS 2 for Skyrim SE 1.6.1170"
    })

    -- add src files
    add_files("src/**.cpp")
    add_headerfiles("src/**.h")
    add_includedirs("src")
    set_pcxxheader("src/pch.h")

    on_config(function(target)
        target:set("configvar", "COMMONLIBSSE_NG_OPTION_RUNTIME_COMPATIBILITY",
            "SKSE::PluginDeclaration::RuntimeCompatibility{ SKSE::PluginDeclaration::VersionNumber{ 1, 6, 1170, 0 } }")
    end)

    after_build(function(target)
        os.execv("py", {
            path.join(os.projectdir(), "tools", "package.py"),
            "--dll", target:targetfile()
        })
    end)

target("slacs_s1_checks")
    set_kind("binary")
    set_default(false)
    set_languages("c++23")
    set_warnings("allextra")
    add_files("tests/s1_checks.cpp", "src/config/Settings.cpp", "src/runtime/Ingress.cpp")
    add_includedirs("src")

target("slacs_s4_checks")
    set_kind("binary")
    set_default(false)
    set_languages("c++23")
    set_warnings("allextra")
    add_files("tests/s4_checks.cpp", "src/config/Settings.cpp", "src/core/Core.cpp")
    add_includedirs("src")

target("slacs_s5s6_checks")
    set_kind("binary")
    set_default(false)
    set_languages("c++23")
    set_warnings("allextra")
    add_files("tests/s5s6_checks.cpp", "src/config/Settings.cpp", "src/core/Core.cpp", "src/runtime/Scheduler.cpp")
    add_includedirs("src")

target("slacs_s7_checks")
    set_kind("binary")
    set_default(false)
    set_languages("c++23")
    set_warnings("allextra")
    add_files("tests/s7_checks.cpp", "src/config/Settings.cpp", "src/core/Core.cpp")
    add_includedirs("src")

target("slacs_s8_checks")
    set_kind("binary")
    set_default(false)
    set_languages("c++23")
    set_warnings("allextra")
    add_files("tests/s8_checks.cpp", "src/config/Settings.cpp", "src/menu/MenuState.cpp", "src/menu/MenuText.cpp")
    add_includedirs("src")

target("slacs_s9_checks")
    set_kind("binary")
    set_default(false)
    set_languages("c++23")
    set_warnings("allextra")
    add_files("tests/s9_checks.cpp")
    add_includedirs("src")

target("slacs_s5_persistence_checks")
    set_kind("binary")
    set_default(false)
    set_languages("c++23")
    set_warnings("allextra")
    add_files("tests/s5_persistence_checks.cpp", "src/persistence/SerializationCodec.cpp")
    add_includedirs("src")

target("slacs_overlay_checks")
    set_kind("binary")
    set_default(false)
    add_files("tests/overlay_checks.cpp", "src/config/Settings.cpp", "src/integration/OverlayAdapter.cpp")
    add_includedirs("tests/overlay_stubs", "src")

target("slacs_effects_checks")
    set_kind("binary")
    set_default(false)
    add_files("tests/effects_checks.cpp", "src/integration/Effects.cpp")
    add_includedirs("tests/effects_stubs", "src")

target("slacs_pplus_checks")
    set_kind("binary")
    set_default(false)
    add_files("tests/pplus_checks.cpp")
    add_includedirs("src")

target("slacs_actor_candidates_checks")
    set_kind("binary")
    set_default(false)
    add_files("tests/actor_candidates_checks.cpp", "src/integration/ActorCandidates.cpp")
    add_includedirs("tests/actor_candidates_stubs", "src")

target("slacs_retry_schedule_checks")
    set_kind("binary")
    set_default(false)
    add_files("tests/retry_schedule_checks.cpp")
    add_includedirs("src")

target("slacs_background_pump_checks")
    set_kind("binary")
    set_default(false)
    add_files("tests/background_pump_checks.cpp", "src/runtime/BackgroundPump.cpp")
    add_includedirs("tests/background_pump_stubs", "src")

target("slacs_pplus_callback_checks")
    set_kind("binary")
    set_default(false)
    add_files("tests/pplus_callback_checks.cpp", "src/integration/PPlusAdapter.cpp")
    add_includedirs("tests/pplus_callback_stubs", "src")
