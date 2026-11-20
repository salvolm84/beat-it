#!/bin/zsh
# Build Beat It (Release, Apple Silicon) and run the self-test.
#   ./build.sh            build app + tests, run tests
#   ./build.sh install    ...and copy the app to /Applications
set -euo pipefail
cd "$(dirname "$0")"

cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j"$(sysctl -n hw.ncpu)"

./build/BeatItTest_artefacts/Release/BeatItTest

APP="build/BeatIt_artefacts/Release/Beat It.app"
# ad-hoc sign the whole bundle (the linker only signs the executable)
codesign --force --deep --sign - "$APP"
echo "\nBuilt: $APP"

if [[ "${1:-}" == "install" ]]; then
    rm -rf "/Applications/Beat It.app"
    cp -R "$APP" /Applications/
    echo "Installed to /Applications/Beat It.app"
fi
