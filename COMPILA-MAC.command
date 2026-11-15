#!/bin/bash
# Supreme Drums - (c) 2026 Giulia Casu.
# Compila VST3 e AU (64 bit, Apple Silicon + Intel) su macOS e li installa per Ableton Live, Logic, ecc.
cd "$(dirname "$0")"
fail() { echo; echo "ERRORE: $1"; read -p "Premi Invio per chiudere"; exit 1; }
xcode-select -p >/dev/null 2>&1 || fail "Installa Xcode dall'App Store e aprilo una volta."
command -v cmake >/dev/null || fail "Serve CMake: installalo con 'brew install cmake' (https://brew.sh) oppure da https://cmake.org/download"
cmake -S . -B build -G Xcode -DCMAKE_OSX_ARCHITECTURES="arm64;x86_64" -DCMAKE_OSX_DEPLOYMENT_TARGET=10.15 || fail "Configurazione non riuscita: mandami il testo qui sopra."
cmake --build build --config Release --target SupremeDrums_VST3 SupremeDrums_AU || fail "Compilazione non riuscita: mandami il testo qui sopra."
VST="build/SupremeDrums_artefacts/Release/VST3/Supreme Drums.vst3"
AU="build/SupremeDrums_artefacts/Release/AU/Supreme Drums.component"
# firma locale (ad hoc): necessaria su Apple Silicon. Per distribuire ad altri serve la firma Developer ID + notarizzazione.
codesign --force --deep -s - "$VST" && codesign --force --deep -s - "$AU" || fail "Firma non riuscita."
mkdir -p ~/Library/Audio/Plug-Ins/VST3 ~/Library/Audio/Plug-Ins/Components
rm -rf ~/Library/Audio/Plug-Ins/VST3/"Supreme Drums.vst3" ~/Library/Audio/Plug-Ins/Components/"Supreme Drums.component"
cp -R "$VST" ~/Library/Audio/Plug-Ins/VST3/ && cp -R "$AU" ~/Library/Audio/Plug-Ins/Components/ || fail "Copia non riuscita."
xattr -dr com.apple.quarantine ~/Library/Audio/Plug-Ins/VST3/"Supreme Drums.vst3" ~/Library/Audio/Plug-Ins/Components/"Supreme Drums.component" 2>/dev/null
killall -9 AudioComponentRegistrar 2>/dev/null
echo; echo "Verifica dell'Audio Unit (auval)..."
auval -v aumu Spdr Supa | tail -n 12
mkdir -p dist && rm -rf dist/* && cp -R "$VST" "$AU" dist/
echo; echo "FATTO. Plugin installati e copiati anche nella cartella 'dist'."
echo "In Ableton Live: Preferenze > Plug-in > attiva VST3 e Audio Units > Ripeti scansione."
read -p "Premi Invio per chiudere"
