/*
  Supreme Drums
  Copyright (c) 2026 Giulia Casu. All rights reserved.
  Created by Giulia Casu on 12 November 2026.
*/

#include "PluginEditor.h"
#include "BinaryData.h"

namespace
{

    juce::var makeObject (std::initializer_list<std::pair<const char*, juce::var>> props)
    {
        juce::DynamicObject::Ptr o = new juce::DynamicObject();
        for (const auto& p : props)
            o->setProperty (p.first, p.second);
        return juce::var (o.get());
    }
}

//==============================================================================
std::optional<juce::WebBrowserComponent::Resource> SupremeDrumsEditor::provideResource (const juce::String& url)
{
    const auto path = url.fromFirstOccurrenceOf ("/", false, false).upToFirstOccurrenceOf ("?", false, false);
    if (path.isEmpty() || path == "index.html")
    {
        std::vector<std::byte> data ((size_t) BinaryData::index_htmlSize);
        std::memcpy (data.data(), BinaryData::index_html, data.size());
        return juce::WebBrowserComponent::Resource { std::move (data), "text/html" };
    }
    return std::nullopt;
}

static juce::File webViewLoaderFile()
{
    return juce::File::getSpecialLocation (juce::File::currentExecutableFile).getSiblingFile ("WebView2Loader.dll");
}

// JUCE calls the WebView2 loader without checking that it exists: check it (and the Edge runtime) first,
// so a missing file or runtime shows a message instead of crashing or freezing the host.
bool SupremeDrumsEditor::webViewAvailable (juce::String& why)
{
   #if JUCE_WINDOWS
    const auto dll = webViewLoaderFile();
    if (! dll.existsAsFile())
    {
        why = "Manca il file WebView2Loader.dll accanto al plugin: reinstalla la cartella \"Supreme Drums.vst3\" completa.\n"
              "WebView2Loader.dll is missing next to the plugin: reinstall the whole \"Supreme Drums.vst3\" folder.";
        return false;
    }
    juce::DynamicLibrary lib;
    if (! lib.open (dll.getFullPathName()))
    {
        why = "Impossibile aprire WebView2Loader.dll. / Cannot load WebView2Loader.dll.";
        return false;
    }
    using GetVersion = long (__stdcall*) (const wchar_t*, wchar_t**);
    auto getVersion = (GetVersion) lib.getFunction ("GetAvailableCoreWebView2BrowserVersionString");
    wchar_t* version = nullptr;
    const bool ok = getVersion != nullptr && lib.getFunction ("CreateCoreWebView2EnvironmentWithOptions") != nullptr
                    && getVersion (nullptr, &version) >= 0 && version != nullptr;
    if (version != nullptr)
    {
        juce::DynamicLibrary ole ("ole32.dll");
        if (auto freeFn = (void (__stdcall*) (void*)) ole.getFunction ("CoTaskMemFree"))
            freeFn (version);
    }
    if (! ok)
        why = "Per mostrare l'interfaccia serve Microsoft Edge WebView2 Runtime (gratuito, gia' presente in Windows 11).\n"
              "The interface needs the free Microsoft Edge WebView2 Runtime (included in Windows 11).";
    return ok;
   #else
    juce::ignoreUnused (why);
    return true;
   #endif
}

SupremeDrumsEditor::SupremeDrumsEditor (SupremeDrumsProcessor& p)
    : AudioProcessorEditor (&p),
      processor (p)
{
    juce::String why;
    if (webViewAvailable (why))
    {
        web = std::make_unique<juce::WebBrowserComponent> (juce::WebBrowserComponent::Options {}
                   .withBackend (juce::WebBrowserComponent::Options::Backend::webview2)
                   .withWinWebView2Options (juce::WebBrowserComponent::Options::WinWebView2 {}
                                                .withDLLLocation (webViewLoaderFile())
                                                .withUserDataFolder (juce::File::getSpecialLocation (juce::File::tempDirectory)
                                                                         .getChildFile ("SupremeDrumsWebView")))
                   .withNativeIntegrationEnabled()
                   .withKeepPageLoadedWhenBrowserIsHidden()
                   .withResourceProvider ([] (const juce::String& url) { return provideResource (url); })
                   .withEventListener ("sd", [this] (juce::var msg) { handleEvent (msg); }));
        addAndMakeVisible (*web);
        web->goToURL (juce::WebBrowserComponent::getResourceProviderRoot());
    }
    else
    {
        // The audio engine keeps working (MIDI, saved sounds, automation); only the interface is missing.
        missingLabel.setText ("SUPREME DRUMS\n\n" + why + "\n\nIl motore audio continua a funzionare. / The audio engine keeps working.",
                              juce::dontSendNotification);
        missingLabel.setJustificationType (juce::Justification::centred);
        missingLabel.setColour (juce::Label::textColourId, juce::Colours::white);
        missingLabel.setFont (juce::FontOptions (17.0f));
        addAndMakeVisible (missingLabel);
        missingLink.setColour (juce::HyperlinkButton::textColourId, juce::Colour (0xff6fe6ff));
        addAndMakeVisible (missingLink);
    }

    // Compact 2:1 instrument panel, resizable while keeping its proportions.
    setResizable (true, true);
    setResizeLimits (900, 450, 2400, 1200);
    if (auto* c = getConstrainer())
        c->setFixedAspectRatio (2.0);
    setSize (1200, 600);
    startTimerHz (30);
}

SupremeDrumsEditor::~SupremeDrumsEditor()
{
    stopTimer();
}

void SupremeDrumsEditor::paint (juce::Graphics& g)
{
    g.fillAll (juce::Colour (0xff2b1252));
}

void SupremeDrumsEditor::resized()
{
    if (web != nullptr)
        web->setBounds (getLocalBounds());
    auto r = getLocalBounds().reduced (40);
    missingLink.setBounds (r.removeFromBottom (40));
    missingLabel.setBounds (r);
}

void SupremeDrumsEditor::emit (const juce::Identifier& id, const juce::var& payload)
{
    if (web != nullptr)
        web->emitEventIfBrowserIsVisible (id, payload);
}

void SupremeDrumsEditor::timerCallback()
{
    const bool running = processor.isSequencerRunning();
    const int step = running ? processor.getCurrentStep() : -1;
    const double tempo = processor.getHostBpm();
    if (step != lastStep || running != lastRunning || std::abs (tempo - lastTempo) > 0.001)
    {
        lastStep = step;
        lastRunning = running;
        lastTempo = tempo;
        emit ("pos", makeObject ({ { "s", step }, { "on", running }, { "bpm", tempo } }));
    }

    processor.collectGarbage();

    // Parameters moved by Ableton's automation (or by the UI) are mirrored back to the UI.
    const auto snap = processor.paramSnapshot();
    const auto js = juce::JSON::toString (snap, true);
    if (js != lastParams)
    {
        lastParams = js;
        emit ("params", snap);
    }
}

void SupremeDrumsEditor::handleEvent (const juce::var& msg)
{
    const auto type = msg.getProperty ("type", "").toString();

    if (type == "hello")
    {
        emit ("init", makeObject ({ { "project", processor.getProjectJson() }, { "native", true } }));
        return;
    }

    if (type == "dragMidi")
    {
        startMidiDrag (msg.getProperty ("name", "Supreme Drums - Ritmo.mid").toString(), msg.getProperty ("b64", "").toString());
        return;
    }

    if (type == "openUrl")
    {
        const juce::URL url (msg.getProperty ("url", "").toString());
        if (url.getScheme() == "https" || url.getScheme() == "http")
            url.launchInDefaultBrowser();
        return;
    }

    if (type == "param")
    {
        processor.setParamFromUi (msg.getProperty ("id", "").toString(), (float) (double) msg.getProperty ("v", 0.0),
                                  (int) msg.getProperty ("g", 1) != 0);
        return;
    }

    if (type == "save")
    {
        saveFile (msg.getProperty ("name", "Supreme Drums.zip").toString(), msg.getProperty ("b64", "").toString());
        return;
    }

    processor.handleUiMessage (msg);
}

void SupremeDrumsEditor::saveFile (const juce::String& name, const juce::String& b64)
{
    auto block = std::make_shared<juce::MemoryBlock>();
    {
        juce::MemoryOutputStream mo (*block, false);
        juce::Base64::convertFromBase64 (mo, b64);
    }

    const auto ext = name.fromLastOccurrenceOf (".", true, false);
    const auto start = juce::File::getSpecialLocation (juce::File::userDocumentsDirectory).getChildFile (name);
    chooser = std::make_unique<juce::FileChooser> ("Salva", start, ext.isNotEmpty() ? "*" + ext : "*");

    chooser->launchAsync (juce::FileBrowserComponent::saveMode
                              | juce::FileBrowserComponent::canSelectFiles
                              | juce::FileBrowserComponent::warnAboutOverwriting,
                          [this, block] (const juce::FileChooser& fc)
                          {
                              const auto file = fc.getResult();
                              bool ok = false;
                              if (file != juce::File())
                                  ok = file.replaceWithData (block->getData(), block->getSize());
                              emit ("saved", makeObject ({ { "ok", ok }, { "path", ok ? file.getFullPathName() : juce::String() } }));
                          });
}

void SupremeDrumsEditor::startMidiDrag (const juce::String& name, const juce::String& b64)
{
    juce::MemoryOutputStream mo;
    if (! juce::Base64::convertFromBase64 (mo, b64) || mo.getDataSize() < 14)
        return;

    auto fileName = juce::File::createLegalFileName (name);
    if (! fileName.endsWithIgnoreCase (".mid"))
        fileName << ".mid";

    auto dir = juce::File::getSpecialLocation (juce::File::tempDirectory).getChildFile ("SupremeDrumsMidi");
    dir.createDirectory();
    auto file = dir.getChildFile (fileName);
    if (! file.replaceWithData (mo.getData(), mo.getDataSize()))
        return;

    juce::StringArray files;
    files.add (file.getFullPathName());
    juce::DragAndDropContainer::performExternalDragDropOfFiles (files, false, this);
}
