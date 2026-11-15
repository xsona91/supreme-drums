/*
  Supreme Drums
  Copyright (c) 2026 Giulia Casu. All rights reserved.
  Created by Giulia Casu on 12 November 2026.
*/

#pragma once

#include <juce_gui_extra/juce_gui_extra.h>
#include "PluginProcessor.h"

class SupremeDrumsEditor : public juce::AudioProcessorEditor,
                           private juce::Timer
{
public:
    explicit SupremeDrumsEditor (SupremeDrumsProcessor&);
    ~SupremeDrumsEditor() override;

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    void timerCallback() override;
    void handleEvent (const juce::var& msg);
    void saveFile (const juce::String& name, const juce::String& b64);
    void startMidiDrag (const juce::String& name, const juce::String& b64);
    void emit (const juce::Identifier& id, const juce::var& payload);

    static std::optional<juce::WebBrowserComponent::Resource> provideResource (const juce::String& url);

    SupremeDrumsProcessor& processor;
    std::unique_ptr<juce::WebBrowserComponent> web;   // null when WebView2 is not available
    juce::Label missingLabel;
    juce::HyperlinkButton missingLink { "Scarica Microsoft Edge WebView2 Runtime / Download it",
                                        juce::URL ("https://go.microsoft.com/fwlink/p/?LinkId=2124703") };
    static bool webViewAvailable (juce::String& why);
    std::unique_ptr<juce::FileChooser> chooser;
    int lastStep = -2;
    bool lastRunning = false;
    double lastTempo = -1.0;
    juce::String lastParams;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (SupremeDrumsEditor)
};
