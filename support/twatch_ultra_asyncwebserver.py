from pathlib import Path

Import("env")

project_dir = Path(env.subst("$PROJECT_DIR"))
async_webserver_src = project_dir / ".pio" / "libdeps" / "t-watch-ultra" / "ESP Async WebServer" / "src"
asynctcp_src = project_dir / ".pio" / "libdeps" / "t-watch-ultra" / "AsyncTCP" / "src"
framework_dir = Path(env.PioPlatform().get_package_dir("framework-arduinoespressif32"))
framework_libraries = framework_dir / "libraries"

if async_webserver_src.is_dir():
    include_dirs = [
        async_webserver_src,
        asynctcp_src,
        framework_libraries / "FS" / "src",
        framework_libraries / "WiFi" / "src",
    ]
    env.Append(CPPPATH=[str(path) for path in include_dirs if path.is_dir()])

    env.BuildSources(
        "$BUILD_DIR/forced_asyncwebserver",
        str(async_webserver_src),
        src_filter=["+<*.cpp>"],
    )
else:
    print("warning: ESP Async WebServer source directory was not found for t-watch-ultra")
