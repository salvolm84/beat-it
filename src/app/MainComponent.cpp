#include "MainComponent.h"

#include <cmath>

using namespace beatit;

namespace
{
    enum MenuIds
    {
        menuOpen = 1, menuAnalyze, menuExportMidi, menuExportCsv, menuExportClick, menuPadFolder, menuAudioSettings,
        menuUndo = 100, menuRedo, menuDeleteBeat, menuAddBeatAtPlayhead, menuDoubleTempo, menuHalveTempo, menuAutoDownbeat,
        menuPlay = 200, menuToStart, menuMetronome, menuLoop, menuFollow,
        menuZoomIn = 300, menuZoomOut, menuZoomFit,
        menuShortcuts = 400, menuAbout
    };

    const juce::String appVersion { JUCE_APPLICATION_VERSION_STRING };

    juce::String formatTime (double t)
    {
        t = std::max (0.0, t);
        const int minutes = (int) (t / 60.0);
        const double secs = t - minutes * 60.0;
        return juce::String (minutes) + ":" + juce::String (secs, 3).paddedLeft ('0', 6);
    }

    void styleSlider (juce::Slider& s, double lo, double hi, double step, const juce::String& suffix)
    {
        s.setSliderStyle (juce::Slider::LinearHorizontal);
        s.setTextBoxStyle (juce::Slider::TextBoxRight, false, 64, 20);
        s.setRange (lo, hi, step);
        s.setTextValueSuffix (suffix);
        s.setWantsKeyboardFocus (false);
    }

    void styleLabel (juce::Label& l, const juce::String& text, bool dim = true)
    {
        l.setText (text, juce::dontSendNotification);
        l.setFont (juce::Font (juce::FontOptions (13.0f)));
        l.setColour (juce::Label::textColourId, dim ? theme::textDim : theme::text);
        l.setMinimumHorizontalScale (0.8f);
    }

    const juce::StringArray supportedExtensions { ".wav", ".mp3", ".aif", ".aiff", ".flac", ".m4a", ".caf" };
}

//==============================================================================
void IconButton::paintButton (juce::Graphics& g, bool highlighted, bool down)
{
    auto r = getLocalBounds().toFloat().reduced (1.0f);
    const bool on = getToggleState();

    g.setColour (on ? theme::accent.withAlpha (0.22f) : theme::panelRaised);
    if (highlighted) g.setColour (on ? theme::accent.withAlpha (0.32f) : theme::panelRaised.brighter (0.08f));
    if (down)        g.setColour (theme::panelRaised.brighter (0.18f));
    g.fillRoundedRectangle (r, 6.0f);
    g.setColour (on ? theme::accent : theme::outline);
    g.drawRoundedRectangle (r, 6.0f, 1.0f);

    auto c = r.withSizeKeepingCentre (std::min (r.getWidth(), r.getHeight()) * 0.46f,
                                      std::min (r.getWidth(), r.getHeight()) * 0.46f);
    g.setColour (isEnabled() ? (on ? theme::accent : theme::text) : theme::textDim);

    juce::Path p;
    switch (icon)
    {
        case Icon::play:
            p.addTriangle (c.getX() + c.getWidth() * 0.12f, c.getY(), c.getX() + c.getWidth() * 0.12f, c.getBottom(),
                           c.getRight(), c.getCentreY());
            g.fillPath (p);
            break;
        case Icon::pause:
            g.fillRect (c.withWidth (c.getWidth() * 0.34f));
            g.fillRect (c.withTrimmedLeft (c.getWidth() * 0.66f));
            break;
        case Icon::stop:
            g.fillRoundedRectangle (c.reduced (c.getWidth() * 0.06f), 2.0f);
            break;
        case Icon::toStart:
            g.fillRect (c.withWidth (c.getWidth() * 0.16f));
            p.addTriangle (c.getRight(), c.getY(), c.getRight(), c.getBottom(), c.getX() + c.getWidth() * 0.18f, c.getCentreY());
            g.fillPath (p);
            break;
        case Icon::loop:
        {
            auto e = c.reduced (0.0f, c.getHeight() * 0.15f);
            p.addRoundedRectangle (e, e.getHeight() * 0.45f);
            g.strokePath (p, juce::PathStrokeType (2.0f));
            juce::Path a;
            a.addTriangle (e.getCentreX() - 4.0f, e.getY() - 4.0f, e.getCentreX() + 3.0f, e.getY(), e.getCentreX() - 4.0f, e.getY() + 4.0f);
            g.fillPath (a);
            break;
        }
        case Icon::follow:
        {
            // playhead with a "moving right" chevron
            g.fillRect (juce::Rectangle<float> (c.getX() + c.getWidth() * 0.15f, c.getY(), 2.0f, c.getHeight()));
            const float cx = c.getX() + c.getWidth() * 0.5f, w = c.getWidth() * 0.35f, h = c.getHeight() * 0.35f;
            p.startNewSubPath (cx, c.getCentreY() - h);
            p.lineTo (cx + w, c.getCentreY());
            p.lineTo (cx, c.getCentreY() + h);
            g.strokePath (p, juce::PathStrokeType (2.2f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
            break;
        }
        case Icon::zoomIn:
        case Icon::zoomOut:
            g.fillRect (c.withSizeKeepingCentre (c.getWidth(), 2.0f));
            if (icon == Icon::zoomIn)
                g.fillRect (c.withSizeKeepingCentre (2.0f, c.getHeight()));
            break;
        case Icon::zoomFit:
            p.addArrow ({ c.getCentreX() - 1.0f, c.getCentreY(), c.getX(), c.getCentreY() }, 2.0f, 7.0f, 5.0f);
            p.addArrow ({ c.getCentreX() + 1.0f, c.getCentreY(), c.getRight(), c.getCentreY() }, 2.0f, 7.0f, 5.0f);
            g.fillPath (p);
            break;
    }
}

//==============================================================================
MainComponent::MainComponent()
{
    setLookAndFeel (&lnf);
    juce::LookAndFeel::setDefaultLookAndFeel (&lnf);
    setWantsKeyboardFocus (true);
    formatManager.registerBasicFormats(); // WAV, AIFF, FLAC, and CoreAudio (MP3/M4A/CAF) on macOS

    // ---- audio
    auto err = deviceManager.initialiseWithDefaultDevices (0, 2);
    if (err.isNotEmpty())
        DBG ("Audio init error: " << err);
    sourcePlayer.setSource (&engine);
    deviceManager.addAudioCallback (&sourcePlayer);

    // ---- top bar
    for (auto* b : { &openButton, &analyzeButton, &exportButton, &audioSettingsButton })
    {
        addAndMakeVisible (b);
        b->setWantsKeyboardFocus (false);
    }
    exportButton.setColour (juce::TextButton::buttonColourId, theme::accent);
    exportButton.setColour (juce::TextButton::textColourOffId, juce::Colours::black);
    openButton.onClick = [this] { showOpenDialog(); };
    analyzeButton.onClick = [this] { startAnalysis(); };
    exportButton.onClick = [this] { exportMidi(); };
    audioSettingsButton.onClick = [this] { showAudioSettings(); };
    openButton.setTooltip ("Open a WAV / MP3 / AIFF / FLAC / M4A file (Cmd+O). You can also drop a file on the window.");
    analyzeButton.setTooltip ("Re-run beat tracking from scratch (Cmd+R)");
    exportButton.setTooltip ("Write a MIDI file containing the tempo map (Cmd+E)");

    addAndMakeVisible (fileLabel);
    styleLabel (fileLabel, "No file loaded");

    // ---- views
    addAndMakeVisible (waveform);
    addAndMakeVisible (tempoView);
    addAndMakeVisible (scrollBar);
    scrollBar.setAutoHide (false);
    scrollBar.addListener (this);
    waveform.setEmptyMessage ("Drop a WAV or MP3 file here\nor click Open...");

    waveform.onSeek = [this] (double t) { engine.setPosition (t); };
    waveform.onRangeRequested = [this] (double s, double e) { setViewRange (s, e); };
    waveform.onEditBegin = [this] { pushUndo(); };
    waveform.onMoveBeat = [this] (int i, double t) { moveBeat (i, t); };
    waveform.onEditEnd = [this] { hasManualEdits = true; updateInfoLabels(); };
    waveform.onAddBeat = [this] (double t) { addBeat (t); };
    waveform.onDeleteBeat = [this] (int i) { deleteBeat (i); };
    waveform.onSetDownbeat = [this] (int i) { setDownbeat (i); };
    waveform.onLoopSet = [this] (double s, double e) { setLoop (s, e, true); };
    waveform.onLoopCleared = [this] { setLoop (0, 0, false); };
    waveform.onPlayFrom = [this] (double t) { engine.setPosition (t); engine.play(); };

    tempoView.onSeek = [this] (double t) { engine.setPosition (t); };
    tempoView.onWheel = [this] (const juce::MouseEvent& e, const juce::MouseWheelDetails& w)
        { waveform.mouseWheelMove (e.getEventRelativeTo (&waveform), w); };
    tempoView.onMagnify = [this] (const juce::MouseEvent& e, float s)
        { waveform.mouseMagnify (e.getEventRelativeTo (&waveform), s); };

    // ---- transport
    for (auto* b : { &toStartButton, &playButton, &stopButton, &loopButton, &followButton,
                     &zoomOutButton, &zoomInButton, &zoomFitButton })
        addAndMakeVisible (b);

    loopButton.setClickingTogglesState (true);
    followButton.setClickingTogglesState (true);
    followButton.setToggleState (true, juce::dontSendNotification);

    toStartButton.onClick = [this] { returnToStart(); };
    playButton.onClick = [this] { togglePlay(); };
    stopButton.onClick = [this] { engine.stop(); };
    loopButton.onClick = [this] { setLoop (loopStart, loopEnd, loopButton.getToggleState()); };
    zoomInButton.onClick = [this] { zoom (0.5); };
    zoomOutButton.onClick = [this] { zoom (2.0); };
    zoomFitButton.onClick = [this] { zoomToFit(); };

    for (auto* l : { &timeLabel, &barBeatLabel, &bpmLabel })
    {
        addAndMakeVisible (l);
        l->setJustificationType (juce::Justification::centredLeft);
    }
    timeLabel.setFont (juce::Font (juce::FontOptions (juce::Font::getDefaultMonospacedFontName(), 22.0f, juce::Font::plain)));
    barBeatLabel.setFont (juce::Font (juce::FontOptions (16.0f)));
    bpmLabel.setFont (juce::Font (juce::FontOptions (16.0f, juce::Font::bold)));
    bpmLabel.setColour (juce::Label::textColourId, theme::accentAlt);
    barBeatLabel.setColour (juce::Label::textColourId, theme::accent);

    // ---- side panel
    sideViewport.setViewedComponent (&side, false);
    sideViewport.setScrollBarsShown (true, false);
    sideViewport.setScrollBarThickness (8);
    addAndMakeVisible (sideViewport);
    side.painter = [this] (juce::Graphics& g) { paintSidePanel (g); };
    side.layout  = [this] { layoutSidePanel(); };

    auto addSide = [this] (juce::Component& c) { side.addAndMakeVisible (c); c.setWantsKeyboardFocus (false); };

    // analysis
    styleLabel (rangeLabel, "BPM range");
    rangeSlider.setRange (30.0, 300.0, 1.0);
    rangeSlider.setMinAndMaxValues (procOptions.minBpm, procOptions.maxBpm, juce::dontSendNotification);
    rangeSlider.setTooltip ("Allowed tempo range. The detected tempo is halved/doubled to fall inside it. "
                            "Narrow it if the result is twice or half the real tempo.");
    styleLabel (smoothingLabel, "Smoothing");
    styleSlider (smoothingSlider, 0, 8, 1, " beats");
    smoothingSlider.setTooltip ("Evens out per-beat jitter in the tempo curve (local linear fit). 0 = raw beats.");
    styleLabel (offsetLabel, "Offset");
    styleSlider (offsetSlider, -100, 100, 0.5, " ms");
    offsetSlider.setTooltip ("Shift every beat earlier/later. Double-click to reset.");
    offsetSlider.setDoubleClickReturnValue (true, 0.0);
    smoothingSlider.setDoubleClickReturnValue (true, 2.0);

    snapToggle.setTooltip ("Move each beat onto the nearest strong transient (+-50 ms) for sample-accurate grid lines");
    fixToggle.setTooltip ("Insert beats the tracker skipped and remove doubled beats");
    silenceToggle.setTooltip ("Drop beats where there is no audio (the MIDI pre-roll covers the gap)");
    constantToggle.setTooltip ("Fit one straight tempo through all beats (for songs played to a click)");
    autoAnalyzeToggle.setTooltip ("Run the analysis as soon as a file is opened");

    for (auto* c : std::initializer_list<juce::Component*> { &rangeLabel, &rangeSlider, &smoothingLabel, &smoothingSlider,
                                                              &offsetLabel, &offsetSlider, &snapToggle, &fixToggle,
                                                              &silenceToggle, &constantToggle, &autoAnalyzeToggle })
        addSide (*c);

    rangeSlider.onDragStart = smoothingSlider.onDragStart = offsetSlider.onDragStart = [this] { pushUndo(); };
    rangeSlider.onValueChange = [this] { updateInfoLabels(); reprocess (false); };
    smoothingSlider.onValueChange = offsetSlider.onValueChange = [this] { reprocess (false); };
    for (auto* t : { &snapToggle, &fixToggle, &silenceToggle, &constantToggle })
        t->onClick = [this] { reprocess (true); };
    autoAnalyzeToggle.onClick = [this] { saveSettings(); };

    // grid
    styleLabel (timeSigLabel, "Time signature");
    styleLabel (downbeatLabel, "Bar start");
    styleLabel (tempoOctaveLabel, "Tempo");
    styleLabel (gridInfoLabel, "", false);
    gridInfoLabel.setJustificationType (juce::Justification::topLeft);
    styleLabel (editHintLabel, "Drag beat lines to move (Cmd = fine), double-click to add, Option-click to delete, "
                               "right-click for more. Help menu lists all shortcuts.");
    editHintLabel.setFont (juce::Font (juce::FontOptions (11.5f)));
    editHintLabel.setJustificationType (juce::Justification::topLeft);

    for (int i = 1; i <= 16; ++i) numeratorBox.addItem (juce::String (i), i);
    for (int d : { 2, 4, 8, 16 }) denominatorBox.addItem (juce::String (d), d);
    numeratorBox.setSelectedId (4, juce::dontSendNotification);
    denominatorBox.setSelectedId (4, juce::dontSendNotification);
    numeratorBox.onChange = denominatorBox.onChange = [this]
        { setTimeSignature (numeratorBox.getSelectedId(), denominatorBox.getSelectedId()); };

    downbeatPrevButton.setTooltip ("Move bar starts one beat earlier");
    downbeatNextButton.setTooltip ("Move bar starts one beat later");
    downbeatAutoButton.setTooltip ("Guess bar starts from bass / harmonic changes");
    doubleButton.setTooltip ("Double the tempo (insert a beat between every pair)");
    halveButton.setTooltip ("Halve the tempo (keep every other beat)");
    undoButton.setTooltip ("Undo (Cmd+Z)");
    redoButton.setTooltip ("Redo (Cmd+Shift+Z)");
    downbeatPrevButton.onClick = [this] { shiftDownbeat (-1); };
    downbeatNextButton.onClick = [this] { shiftDownbeat (1); };
    downbeatAutoButton.onClick = [this] { autoDownbeat(); };
    halfBeatButton.setClickingTogglesState (true);
    halfBeatButton.setColour (juce::TextButton::buttonOnColourId, theme::accent);
    halfBeatButton.setColour (juce::TextButton::textColourOnId, juce::Colours::black);
    halfBeatButton.setTooltip ("Move every beat by half a beat. Use it when the grid sits on the off-beats "
                               "(hi-hat / guitar skank) instead of the kick and bass, common in reggae, ska and swing.");
    halfBeatButton.onClick = [this] { reprocess (true, true); };
    doubleButton.onClick = [this] { changeTempoOctave (true); };
    halveButton.onClick = [this] { changeTempoOctave (false); };
    undoButton.onClick = [this] { undo(); };
    redoButton.onClick = [this] { redo(); };

    for (auto* c : std::initializer_list<juce::Component*> { &timeSigLabel, &numeratorBox, &denominatorBox, &downbeatLabel,
                                                              &downbeatPrevButton, &downbeatNextButton, &downbeatAutoButton,
                                                              &halfBeatButton, &tempoOctaveLabel, &doubleButton, &halveButton, &undoButton,
                                                              &redoButton, &gridInfoLabel, &editHintLabel })
        addSide (*c);

    // playback
    styleLabel (musicLabel, "Music");
    styleSlider (musicSlider, 0, 100, 1, " %");
    styleLabel (clickLabel, "Click");
    styleSlider (clickSlider, 0, 100, 1, " %");
    styleLabel (speedLabel, "Speed");
    styleSlider (speedSlider, 50, 150, 5, " %");
    speedSlider.setDoubleClickReturnValue (true, 100.0);
    speedSlider.setValue (100.0, juce::dontSendNotification);
    speedSlider.setTooltip ("Playback speed (changes pitch). Slow down to check beat placement.");
    metronomeToggle.setTooltip ("Metronome click on every beat (M)");

    musicSlider.onValueChange = [this] { engine.setMusicGain ((float) std::pow (musicSlider.getValue() / 100.0, 2.0)); };
    clickSlider.onValueChange = [this] { engine.setClickGain ((float) std::pow (clickSlider.getValue() / 100.0, 2.0)); };
    speedSlider.onValueChange = [this] { engine.setSpeed (speedSlider.getValue() / 100.0); };
    muteToggle.onClick = [this] { engine.setMusicMuted (muteToggle.getToggleState()); };
    metronomeToggle.onClick = [this] { engine.setMetronomeEnabled (metronomeToggle.getToggleState()); };
    accentToggle.onClick = [this] { engine.setAccentDownbeats (accentToggle.getToggleState()); };

    for (auto* c : std::initializer_list<juce::Component*> { &musicLabel, &musicSlider, &muteToggle, &clickLabel, &clickSlider,
                                                              &metronomeToggle, &accentToggle, &speedLabel, &speedSlider })
        addSide (*c);

    // export
    styleLabel (ppqLabel, "Resolution");
    for (int p : { 480, 960, 1920 }) ppqBox.addItem (juce::String (p) + " PPQ", p);
    ppqBox.setSelectedId (960, juce::dontSendNotification);
    clickTrackToggle.setTooltip ("Adds a MIDI track with a wood-block note on each beat, handy for checking in the DAW");
    markersToggle.setTooltip ("Adds a marker meta-event at every bar");
    exportMidiButton.setColour (juce::TextButton::buttonColourId, theme::accent);
    exportMidiButton.setColour (juce::TextButton::textColourOffId, juce::Colours::black);
    exportMidiButton.onClick = [this] { exportMidi(); };
    exportCsvButton.onClick = [this] { exportCsv(); };
    exportCsvButton.setTooltip ("Export beats as CSV: time, bar, beat, BPM (Cmd+Shift+E)");
    styleLabel (preCountLabel, "Pre-count");
    preCountBox.addItem ("Off", 1);
    for (int b = 1; b <= 16; ++b) preCountBox.addItem (juce::String (b) + (b == 1 ? " beat" : " beats"), b + 1);
    preCountBox.setSelectedId (1, juce::dontSendNotification);
    preCountBox.setTooltip ("Count-in beats added before the song in all exports. The audio has to be delayed by the "
                            "same amount: use \"Pad audio folder\" for the song and its stems.");
    preCountBox.onChange = [this] { refreshPreCount(); gridChanged(); saveSettings(); };
    styleLabel (preCountInfoLabel, {});
    preCountInfoLabel.setFont (juce::Font (juce::FontOptions (12.0f)));
    padFolderButton.onClick = [this] { padAudioFolder(); };
    padFolderButton.setTooltip ("Prepend the pre-count silence to every audio file in a folder (not sub-folders). "
                                "Padded WAV copies go in a new sub-folder; the originals are not changed.");
    exportClickButton.onClick = [this] { exportMetronomeAudio(); };
    exportClickButton.setTooltip ("Export the metronome alone as a WAV file, same length and sample rate as the song, "
                                  "to place at 0:00 in the DAW (Cmd+Option+E)");

    for (auto* c : std::initializer_list<juce::Component*> { &ppqLabel, &ppqBox, &clickTrackToggle, &markersToggle,
                                                              &exportMidiButton, &exportCsvButton, &exportClickButton,
                                                              &preCountLabel, &preCountBox, &preCountInfoLabel, &padFolderButton })
        addSide (*c);

    // ---- status bar
    addAndMakeVisible (statusLabel);
    styleLabel (statusLabel, "Ready");
    addChildComponent (progressBar);
    addChildComponent (cancelButton);
    cancelButton.onClick = [this] { cancelRequested = true; };
    cancelButton.setWantsKeyboardFocus (false);

    loadSettings();
    updateControlStates();
    updateInfoLabels();

    setSize (1360, 860);
    startTimerHz (40);
}

MainComponent::~MainComponent()
{
    saveSettings();
    cancelRequested = true;
    pool.removeAllJobs (true, 10000);
    deviceManager.removeAudioCallback (&sourcePlayer);
    sourcePlayer.setSource (nullptr);
    scrollBar.removeListener (this);
    juce::LookAndFeel::setDefaultLookAndFeel (nullptr);
    setLookAndFeel (nullptr);
}

//==============================================================================
void MainComponent::loadSettings()
{
    juce::PropertiesFile::Options o;
    o.applicationName = "BeatIt";
    o.filenameSuffix = ".settings";
    o.osxLibrarySubFolder = "Application Support";
    settings = std::make_unique<juce::PropertiesFile> (o);

    auto& s = *settings;
    musicSlider.setValue (s.getDoubleValue ("musicVol", 85.0));
    clickSlider.setValue (s.getDoubleValue ("clickVol", 70.0));
    metronomeToggle.setToggleState (s.getBoolValue ("metronome", true), juce::sendNotification);
    accentToggle.setToggleState (s.getBoolValue ("accent", true), juce::sendNotification);
    followButton.setToggleState (s.getBoolValue ("follow", true), juce::dontSendNotification);
    autoAnalyzeToggle.setToggleState (s.getBoolValue ("autoAnalyze", true), juce::dontSendNotification);

    procOptions.minBpm = s.getDoubleValue ("minBpm", procOptions.minBpm);
    procOptions.maxBpm = s.getDoubleValue ("maxBpm", procOptions.maxBpm);
    procOptions.smoothing = s.getIntValue ("smoothing", procOptions.smoothing);
    procOptions.snapToTransients = s.getBoolValue ("snap", procOptions.snapToTransients);
    procOptions.fixOutliers = s.getBoolValue ("fixOutliers", procOptions.fixOutliers);
    procOptions.trimSilence = s.getBoolValue ("trimSilence", procOptions.trimSilence);

    rangeSlider.setMinAndMaxValues (procOptions.minBpm, procOptions.maxBpm, juce::dontSendNotification);
    smoothingSlider.setValue (procOptions.smoothing, juce::dontSendNotification);
    offsetSlider.setValue (0.0, juce::dontSendNotification);
    snapToggle.setToggleState (procOptions.snapToTransients, juce::dontSendNotification);
    fixToggle.setToggleState (procOptions.fixOutliers, juce::dontSendNotification);
    silenceToggle.setToggleState (procOptions.trimSilence, juce::dontSendNotification);
    constantToggle.setToggleState (false, juce::dontSendNotification);

    ppqBox.setSelectedId (s.getIntValue ("ppq", 960), juce::dontSendNotification);
    preCountBox.setSelectedId (juce::jlimit (0, 16, s.getIntValue ("preCount", 0)) + 1, juce::dontSendNotification);
    clickTrackToggle.setToggleState (s.getBoolValue ("clickTrack", true), juce::dontSendNotification);
    markersToggle.setToggleState (s.getBoolValue ("markers", false), juce::dontSendNotification);

    engine.setMusicGain ((float) std::pow (musicSlider.getValue() / 100.0, 2.0));
    engine.setClickGain ((float) std::pow (clickSlider.getValue() / 100.0, 2.0));

    if (auto xml = s.getXmlValue ("audioDevice"))
        deviceManager.initialise (0, 2, xml.get(), true);
}

void MainComponent::saveSettings()
{
    if (settings == nullptr)
        return;

    auto& s = *settings;
    s.setValue ("musicVol", musicSlider.getValue());
    s.setValue ("clickVol", clickSlider.getValue());
    s.setValue ("metronome", metronomeToggle.getToggleState());
    s.setValue ("accent", accentToggle.getToggleState());
    s.setValue ("follow", followButton.getToggleState());
    s.setValue ("autoAnalyze", autoAnalyzeToggle.getToggleState());
    s.setValue ("minBpm", rangeSlider.getMinValue());
    s.setValue ("maxBpm", rangeSlider.getMaxValue());
    s.setValue ("smoothing", (int) smoothingSlider.getValue());
    s.setValue ("snap", snapToggle.getToggleState());
    s.setValue ("fixOutliers", fixToggle.getToggleState());
    s.setValue ("trimSilence", silenceToggle.getToggleState());
    s.setValue ("ppq", ppqBox.getSelectedId());
    s.setValue ("preCount", std::max (0, preCountBox.getSelectedId() - 1));
    s.setValue ("clickTrack", clickTrackToggle.getToggleState());
    s.setValue ("markers", markersToggle.getToggleState());

    if (auto xml = deviceManager.createStateXml())
        s.setValue ("audioDevice", xml.get());

    s.saveIfNeeded();
}

//==============================================================================
void MainComponent::paint (juce::Graphics& g)
{
    g.fillAll (theme::background);

    // top bar
    g.setColour (theme::panel);
    g.fillRect (getLocalBounds().removeFromTop (52));
    g.setColour (theme::outline);
    g.fillRect (0, 52, getWidth(), 1);

    // app title
    g.setColour (theme::accent);
    g.setFont (juce::Font (juce::FontOptions (19.0f, juce::Font::bold)));
    g.drawText ("Beat It", 16, 6, 80, 28, juce::Justification::centredLeft);
    g.setColour (theme::textDim);
    g.setFont (juce::Font (juce::FontOptions (11.0f)));
    g.drawText ("v" + appVersion, 16, 31, 80, 14, juce::Justification::centredLeft);

    // status bar
    g.setColour (theme::panel);
    g.fillRect (getLocalBounds().removeFromBottom (28));
    g.setColour (theme::outline);
    g.fillRect (0, getHeight() - 28, getWidth(), 1);

    // side panel background
    g.setColour (theme::panel);
    g.fillRect (sideViewport.getBounds().expanded (0, 0));
}

void MainComponent::paintOverChildren (juce::Graphics& g)
{
    if (dragHighlight)
    {
        g.setColour (theme::accent.withAlpha (0.15f));
        g.fillRect (getLocalBounds());
        g.setColour (theme::accent);
        g.drawRect (getLocalBounds(), 3);
    }
}

void MainComponent::resized()
{
    auto area = getLocalBounds();

    // top bar
    {
        auto top = area.removeFromTop (52).reduced (12, 10);
        top.removeFromLeft (84); // title
        openButton.setBounds (top.removeFromLeft (90));
        top.removeFromLeft (8);
        analyzeButton.setBounds (top.removeFromLeft (90));
        top.removeFromLeft (8);
        exportButton.setBounds (top.removeFromLeft (120));
        audioSettingsButton.setBounds (top.removeFromRight (90));
        top.removeFromLeft (16);
        top.removeFromRight (12);
        fileLabel.setBounds (top);
    }

    // status bar
    {
        auto status = area.removeFromBottom (28).reduced (12, 4);
        cancelButton.setBounds (status.removeFromRight (70));
        status.removeFromRight (8);
        progressBar.setBounds (status.removeFromRight (220));
        statusLabel.setBounds (status);
    }

    // side panel
    sideViewport.setBounds (area.removeFromRight (316));
    side.setSize (sideViewport.getWidth() - sideViewport.getScrollBarThickness(), 900);

    auto main = area.reduced (12, 10);

    // transport row
    {
        auto t = main.removeFromBottom (48).withTrimmedTop (6);
        const int b = t.getHeight();
        toStartButton.setBounds (t.removeFromLeft (b + 6)); t.removeFromLeft (6);
        playButton.setBounds (t.removeFromLeft (b + 18));  t.removeFromLeft (6);
        stopButton.setBounds (t.removeFromLeft (b + 6));   t.removeFromLeft (14);
        loopButton.setBounds (t.removeFromLeft (b + 6));   t.removeFromLeft (6);
        followButton.setBounds (t.removeFromLeft (b + 6)); t.removeFromLeft (18);

        zoomFitButton.setBounds (t.removeFromRight (b + 4)); t.removeFromRight (6);
        zoomInButton.setBounds (t.removeFromRight (b + 4));  t.removeFromRight (6);
        zoomOutButton.setBounds (t.removeFromRight (b + 4)); t.removeFromRight (18);

        timeLabel.setBounds (t.removeFromLeft (130));
        barBeatLabel.setBounds (t.removeFromLeft (170));
        bpmLabel.setBounds (t);
    }

    scrollBar.setBounds (main.removeFromBottom (12));
    main.removeFromBottom (4);
    tempoView.setBounds (main.removeFromBottom (juce::jlimit (100, 190, main.getHeight() / 4)));
    main.removeFromBottom (6);
    waveform.setBounds (main);
}

void MainComponent::layoutSidePanel()
{
    sections.clear();
    auto area = side.getLocalBounds().reduced (14, 10);
    const int rowH = 26, gap = 5, labelW = 88;

    auto beginSection = [&] (const juce::String& title)
    {
        area.removeFromTop (6);
        sections.push_back ({ title, area.withHeight (0) });
        area.removeFromTop (22);
    };
    auto endSection = [&]
    {
        auto& r = sections.back().second;
        r = r.withBottom (area.getY() + 2);
        area.removeFromTop (8);
    };
    auto row = [&] (int h = rowH) { auto r = area.removeFromTop (h); area.removeFromTop (h < rowH ? 2 : gap); return r; };
    auto labelled = [&] (juce::Label& l) { auto r = row(); l.setBounds (r.removeFromLeft (labelW)); return r; };

    // ANALYSIS
    beginSection ("ANALYSIS");
    rangeSlider.setBounds (labelled (rangeLabel));
    smoothingSlider.setBounds (labelled (smoothingLabel));
    offsetSlider.setBounds (labelled (offsetLabel));
    snapToggle.setBounds (row (22));
    fixToggle.setBounds (row (22));
    silenceToggle.setBounds (row (22));
    constantToggle.setBounds (row (22));
    autoAnalyzeToggle.setBounds (row (22));
    endSection();

    // GRID
    beginSection ("GRID & BARS");
    {
        auto r = labelled (timeSigLabel);
        numeratorBox.setBounds (r.removeFromLeft (64));
        r.removeFromLeft (6);
        denominatorBox.setBounds (r.removeFromLeft (64));
    }
    {
        auto r = labelled (downbeatLabel);
        downbeatPrevButton.setBounds (r.removeFromLeft (36)); r.removeFromLeft (4);
        downbeatNextButton.setBounds (r.removeFromLeft (36)); r.removeFromLeft (8);
        downbeatAutoButton.setBounds (r.removeFromLeft (64)); r.removeFromLeft (8);
        halfBeatButton.setBounds (r);
    }
    {
        auto r = labelled (tempoOctaveLabel);
        halveButton.setBounds (r.removeFromLeft (36)); r.removeFromLeft (4);
        doubleButton.setBounds (r.removeFromLeft (36)); r.removeFromLeft (8);
        const int w = (r.getWidth() - 4) / 2;
        undoButton.setBounds (r.removeFromLeft (w)); r.removeFromLeft (4);
        redoButton.setBounds (r);
    }
    gridInfoLabel.setBounds (row (34));
    editHintLabel.setBounds (row (30));
    endSection();

    // PLAYBACK
    beginSection ("PLAYBACK");
    {
        auto r = labelled (musicLabel);
        muteToggle.setBounds (r.removeFromRight (62));
        musicSlider.setBounds (r);
    }
    clickSlider.setBounds (labelled (clickLabel));
    {
        auto r = row (22);
        metronomeToggle.setBounds (r.removeFromLeft (r.getWidth() / 2 - 10));
        accentToggle.setBounds (r);
    }
    speedSlider.setBounds (labelled (speedLabel));
    endSection();

    // EXPORT
    beginSection ("EXPORT");
    {
        auto r = labelled (preCountLabel);
        preCountBox.setBounds (r.removeFromLeft (120));
    }
    preCountInfoLabel.setBounds (row (18));
    {
        auto r = labelled (ppqLabel);
        ppqBox.setBounds (r.removeFromLeft (120));
    }
    clickTrackToggle.setBounds (row (22));
    markersToggle.setBounds (row (22));
    {
        auto r = row (32);
        exportCsvButton.setBounds (r.removeFromRight (64));
        r.removeFromRight (6);
        exportMidiButton.setBounds (r);
    }
    exportClickButton.setBounds (row (28));
    padFolderButton.setBounds (row (28));
    endSection();

    const int needed = area.getY() + 10;
    if (side.getHeight() != needed)
        side.setSize (side.getWidth(), needed);
}

void MainComponent::paintSidePanel (juce::Graphics& g)
{
    g.fillAll (theme::panel);
    g.setColour (theme::outline);
    g.fillRect (0, 0, 1, side.getHeight());

    for (auto& [title, r] : sections)
    {
        auto box = r.expanded (8, 4).toFloat();
        g.setColour (theme::background.withAlpha (0.55f));
        g.fillRoundedRectangle (box, 8.0f);
        g.setColour (theme::textDim);
        g.setFont (juce::Font (juce::FontOptions (11.5f, juce::Font::bold)).withExtraKerningFactor (0.12f));
        g.drawText (title, r.getX(), r.getY() - 2, r.getWidth(), 20, juce::Justification::centredLeft);
    }
}

//==============================================================================
void MainComponent::timerCallback()
{
    const double pos = engine.getPosition();
    waveform.setPlayhead (pos);
    tempoView.setPlayhead (pos);

    if (engine.isPlaying() && followButton.getToggleState())
    {
        const double span = viewEnd - viewStart;
        if (pos > viewEnd - span * 0.05 || pos < viewStart)
            setViewRange (pos - span * 0.1, pos + span * 0.9);
    }

    playButton.setIcon (engine.isPlaying() ? IconButton::Icon::pause : IconButton::Icon::play);
    timeLabel.setText (formatTime (pos), juce::dontSendNotification);

    if (! shownGrid.isEmpty())
    {
        const auto p = shownGrid.positionAt (pos, shownLayout);
        const juce::String prefix (pos < timelineOffset() ? "Count-in  " : "");
        barBeatLabel.setText (p.valid ? prefix + "Bar " + juce::String (p.bar) + "  Beat " + juce::String (p.beat) : juce::String ("-"),
                              juce::dontSendNotification);
        bpmLabel.setText (juce::String (shownGrid.bpmAtTime (pos), 2) + " BPM", juce::dontSendNotification);
    }
    else
    {
        barBeatLabel.setText ({}, juce::dontSendNotification);
        bpmLabel.setText ({}, juce::dontSendNotification);
    }

    if (busy)
        progressValue = (double) progress.load();
}

void MainComponent::scrollBarMoved (juce::ScrollBar*, double newStart)
{
    const double span = viewEnd - viewStart;
    setViewRange (newStart, newStart + span);
}

//==============================================================================
void MainComponent::showOpenDialog()
{
    auto startDir = settings != nullptr ? juce::File (settings->getValue ("lastDir")) : juce::File();
    if (! startDir.isDirectory())
        startDir = juce::File::getSpecialLocation (juce::File::userMusicDirectory);

    chooser = std::make_unique<juce::FileChooser> ("Open a song", startDir, "*.wav;*.mp3;*.aif;*.aiff;*.flac;*.m4a;*.caf");
    chooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
                          [this] (const juce::FileChooser& fc)
    {
        const auto f = fc.getResult();
        if (f.existsAsFile())
            openFile (f);
    });
}

bool MainComponent::isInterestedInFileDrag (const juce::StringArray& files)
{
    for (auto& f : files)
        if (supportedExtensions.contains (juce::File (f).getFileExtension(), true))
            return true;
    return false;
}

void MainComponent::filesDropped (const juce::StringArray& files, int, int)
{
    dragHighlight = false;
    repaint();
    for (auto& f : files)
        if (supportedExtensions.contains (juce::File (f).getFileExtension(), true))
        {
            openFile (juce::File (f));
            break;
        }
}

void MainComponent::openFile (const juce::File& file)
{
    if (! file.existsAsFile())
        return;

    if (settings != nullptr)
        settings->setValue ("lastDir", file.getParentDirectory().getFullPathName());

    cancelRequested = true;          // stop any running analysis
    pool.removeAllJobs (true, 5000);
    cancelRequested = false;

    engine.stop();
    const int generation = ++loadGeneration;
    setBusy (true, "Loading " + file.getFileName() + "...");

    juce::Component::SafePointer<MainComponent> safe (this);
    pool.addJob ([this, safe, file, generation]
    {
        std::unique_ptr<juce::AudioFormatReader> reader (formatManager.createReaderFor (file));
        std::shared_ptr<juce::AudioBuffer<float>> buffer;
        double rate = 0.0;

        if (reader != nullptr && reader->lengthInSamples > 0)
        {
            const auto len = (int) std::min<juce::int64> (reader->lengthInSamples, (juce::int64) reader->sampleRate * 60 * 40);
            const int channels = (int) juce::jlimit (1u, 2u, reader->numChannels);
            buffer = std::make_shared<juce::AudioBuffer<float>> (channels, len);
            reader->read (buffer.get(), 0, len, 0, true, channels > 1);
            rate = reader->sampleRate;
        }

        juce::MessageManager::callAsync ([safe, buffer, rate, file, generation]
        {
            if (safe != nullptr)
                safe->onAudioLoaded (buffer, rate, file, generation);
        });
    });
}

void MainComponent::onAudioLoaded (std::shared_ptr<juce::AudioBuffer<float>> buffer, double rate,
                                   const juce::File& file, int generation)
{
    if (generation != loadGeneration)
        return;

    setBusy (false);

    if (buffer == nullptr || rate <= 0.0)
    {
        setStatus ("Could not read " + file.getFileName() + " - unsupported or damaged file.", true);
        return;
    }

    audioBuffer = buffer;
    fileSampleRate = rate;
    currentFile = file;
    analysis.reset();
    grid.beats.clear();
    undoStack.clear();
    redoStack.clear();
    hasManualEdits = false;
    setLoop (0, 0, false);

    halfBeatButton.setToggleState (false, juce::dontSendNotification);   // a per-song fix
    preCount = {};
    engine.setAudio (audioBuffer, rate);   // also resets the lead-in
    waveform.setAudio (audioBuffer.get(), rate);
    waveform.setAudioOffset (0.0);
    gridChanged();
    zoomToFit();

    const double dur = buffer->getNumSamples() / rate;
    fileLabel.setText (file.getFileName() + "    " + formatTime (dur) + "  -  " + juce::String (rate / 1000.0, 1)
                           + " kHz  -  " + (buffer->getNumChannels() > 1 ? "stereo" : "mono"),
                       juce::dontSendNotification);
    fileLabel.setColour (juce::Label::textColourId, theme::text);
    getTopLevelComponent()->setName ("Beat It - " + file.getFileName());

    setStatus ("Loaded " + file.getFileName());
    updateControlStates();

    if (autoAnalyzeToggle.getToggleState())
        startAnalysis();
}

void MainComponent::startAnalysis()
{
    if (audioBuffer == nullptr || busy)
        return;

    engine.pause();
    cancelRequested = false;
    progress = 0.0f;
    setBusy (true, "Analyzing beats with BTrack...");

    const int generation = loadGeneration;
    auto buffer = audioBuffer;
    const double rate = fileSampleRate;
    juce::Component::SafePointer<MainComponent> safe (this);

    pool.addJob ([this, safe, buffer, rate, generation]
    {
        auto result = BeatAnalyzer::analyze (*buffer, rate, [this] (float p)
        {
            progress = p;
            return ! cancelRequested.load();
        });

        juce::MessageManager::callAsync ([safe, r = std::move (result), generation]() mutable
        {
            if (safe != nullptr)
                safe->onAnalysisFinished (std::move (r), generation);
        });
    });
}

void MainComponent::onAnalysisFinished (std::optional<AnalysisResult> result, int generation)
{
    if (generation != loadGeneration)
        return; // stale result from a file that's no longer loaded

    setBusy (false);

    if (! result.has_value())
    {
        setStatus (cancelRequested ? "Analysis cancelled." : "Analysis failed (file too short?).", ! cancelRequested);
        return;
    }

    if (! grid.isEmpty())
        pushUndo();

    analysis = std::move (result);
    readProcessingOptionsFromUI();
    grid.beats = ops::process (*analysis, procOptions);
    grid.downbeatOffset = ops::guessDownbeatOffset (grid.beats, grid.beatsPerBar, *analysis);
    hasManualEdits = false;
    refreshPreCount();
    gridChanged();

    // Developer hook: BEATIT_SNAPSHOT=/path/prefix renders the UI to PNGs after
    // analysis (full view + zoomed in) so the UI can be checked headlessly.
    const auto snapshotPrefix = juce::SystemStats::getEnvironmentVariable ("BEATIT_SNAPSHOT", {});
    if (snapshotPrefix.isNotEmpty())
    {
        juce::Component::SafePointer<MainComponent> safe (this);
        juce::Timer::callAfterDelay (600, [safe, snapshotPrefix]
        {
            if (safe == nullptr) return;
            auto write = [&] (const juce::String& suffix)
            {
                auto img = safe->createComponentSnapshot (safe->getLocalBounds(), true, 2.0f);
                juce::File f (snapshotPrefix + suffix + ".png");
                f.deleteFile();
                juce::FileOutputStream os (f);
                juce::PNGImageFormat().writeImageToStream (img, os);
            };
            write ("-full");
            safe->engine.setPosition (30.0);
            safe->setViewRange (28.0, 36.0);
            safe->waveform.setPlayhead (30.0);
            safe->tempoView.setPlayhead (30.0);
            safe->timerCallback();
            write ("-zoom");
            if (juce::SystemStats::getEnvironmentVariable ("BEATIT_SNAPSHOT_QUIT", {}).isNotEmpty())
                juce::JUCEApplicationBase::quit();
        });
    }

    if (grid.isEmpty())
        setStatus ("No beats found - is the file silent?", true);
    else
        setStatus ("Found " + juce::String (grid.beats.size()) + " beats, average "
                   + juce::String (grid.averageBpm(), 1) + " BPM. Check the grid, then Export MIDI.");
}

void MainComponent::readProcessingOptionsFromUI()
{
    procOptions.minBpm = rangeSlider.getMinValue();
    procOptions.maxBpm = rangeSlider.getMaxValue();
    procOptions.smoothing = (int) smoothingSlider.getValue();
    procOptions.offsetMs = offsetSlider.getValue();
    procOptions.snapToTransients = snapToggle.getToggleState();
    procOptions.fixOutliers = fixToggle.getToggleState();
    procOptions.trimSilence = silenceToggle.getToggleState();
    procOptions.constantTempo = constantToggle.getToggleState();
    procOptions.halfBeatShift = halfBeatButton.getToggleState();
}

void MainComponent::reprocess (bool pushUndoFirst, bool reguessBarStarts)
{
    readProcessingOptionsFromUI();
    smoothingSlider.setEnabled (! procOptions.constantTempo);

    if (! analysis.has_value())
        return;

    if (pushUndoFirst)
        pushUndo();

    const size_t oldCount = grid.beats.size();
    grid.beats = ops::process (*analysis, procOptions);

    if (reguessBarStarts || grid.beats.size() != oldCount)
        grid.downbeatOffset = ops::guessDownbeatOffset (grid.beats, grid.beatsPerBar, *analysis);

    if (hasManualEdits)
        setStatus ("Processing re-applied: manual beat edits were replaced (Cmd+Z to undo).");
    hasManualEdits = false;
    refreshPreCount();
    gridChanged();
}

//==============================================================================
void MainComponent::pushUndo()
{
    undoStack.push_back ({ grid, preCount });
    if (undoStack.size() > 300)
        undoStack.erase (undoStack.begin());
    redoStack.clear();
    updateControlStates();
}

void MainComponent::undo()
{
    if (undoStack.empty()) return;
    redoStack.push_back ({ grid, preCount });
    const auto state = undoStack.back();
    undoStack.pop_back();
    restoreState (state);
    setStatus ("Undo");
}

void MainComponent::redo()
{
    if (redoStack.empty()) return;
    undoStack.push_back ({ grid, preCount });
    const auto state = redoStack.back();
    redoStack.pop_back();
    restoreState (state);
    setStatus ("Redo");
}

void MainComponent::restoreState (const DocState& state)
{
    grid = state.grid;
    // the silence travels with the undo step, unless the pre-count setting changed since
    if (state.preCount.beats == selectedPreCountBeats() && state.preCount.isActive())
        setPreCount (state.preCount);
    else
        refreshPreCount();
    gridChanged();
}

void MainComponent::updateShownGrid()
{
    shownGrid = ops::applyPreCount (grid, preCount);
    shownLayout = shownGrid.computeLayout();
    engine.setBeats (shownGrid.beats, downbeatFlags (shownGrid));
}

void MainComponent::gridChanged()
{
    layout = grid.computeLayout();
    updateShownGrid();

    if (numeratorBox.getSelectedId() != grid.beatsPerBar)
        numeratorBox.setSelectedId (grid.beatsPerBar, juce::dontSendNotification);
    if (denominatorBox.getSelectedId() != grid.denominator)
        denominatorBox.setSelectedId (grid.denominator, juce::dontSendNotification);

    waveform.setGrid (&shownGrid, &shownLayout, countInBeats());
    tempoView.setGrid (&shownGrid);
    updateInfoLabels();
    updateControlStates();
}

void MainComponent::moveBeat (int shownIndex, double timelineTime)
{
    // UI coordinates (pre-count included) -> song coordinates
    const int i = shownIndex - countInBeats();
    const double t = timelineTime - timelineOffset();
    if (i < 0 || i >= (int) grid.beats.size())
        return;

    const double lo = i > 0 ? grid.beats[(size_t) i - 1] + 0.03 : 0.0;
    const double hi = i + 1 < (int) grid.beats.size() ? grid.beats[(size_t) i + 1] - 0.03 : songDuration();
    grid.beats[(size_t) i] = juce::jlimit (lo, std::max (lo, hi), t);
    layout = grid.computeLayout();
    updateShownGrid();
    waveform.setGrid (&shownGrid, &shownLayout, countInBeats());
    tempoView.setGrid (&shownGrid);
}

std::vector<bool> MainComponent::downbeatFlags (const BeatGrid& g)
{
    std::vector<bool> downbeats (g.beats.size());
    for (size_t i = 0; i < g.beats.size(); ++i)
        downbeats[i] = g.isDownbeat ((int) i);
    return downbeats;
}

int MainComponent::selectedPreCountBeats() const
{
    return std::max (0, preCountBox.getSelectedId() - 1);
}

double MainComponent::songDuration() const
{
    return audioBuffer != nullptr && fileSampleRate > 0.0 ? audioBuffer->getNumSamples() / fileSampleRate : 0.0;
}

void MainComponent::refreshPreCount()
{
    setPreCount (ops::computePreCount (grid, selectedPreCountBeats(), fileSampleRate));
}

void MainComponent::setPreCount (PreCount pc)
{
    const double oldOffset = timelineOffset();
    const double oldDuration = engine.getDuration();
    const bool wasShowingAll = viewStart <= 1.0e-6 && viewEnd >= oldDuration - 1.0e-6;

    preCount = pc;
    const double delta = timelineOffset() - oldOffset;

    engine.setLeadIn (pc.isActive() ? pc.silenceSamples : 0);   // the playhead keeps its place in the music
    waveform.setAudioOffset (timelineOffset());

    if (std::abs (delta) > 1.0e-12)
    {
        if (loopEnd > loopStart)
            setLoop (std::max (0.0, loopStart + delta), loopEnd + delta, loopButton.getToggleState());

        if (wasShowingAll) zoomToFit();
        else               setViewRange (viewStart + delta, viewEnd + delta);
    }
}

void MainComponent::addBeat (double timelineTime)
{
    const double t = timelineTime - timelineOffset();
    if (audioBuffer != nullptr && t < 0.0 && timelineTime >= 0.0)
    {
        setStatus ("That's inside the pre-count - its beats follow the song's first beats automatically.");
        return;
    }
    if (audioBuffer == nullptr || t < 0.0 || t > songDuration())
        return;

    if (grid.nearestBeat (t, 0.03) >= 0)
    {
        setStatus ("There is already a beat there.");
        return;
    }

    pushUndo();
    auto it = std::lower_bound (grid.beats.begin(), grid.beats.end(), t);
    const int index = (int) std::distance (grid.beats.begin(), it);
    grid.beats.insert (it, t);

    // keep the bar lines after the edit where they were
    if (index <= grid.firstDownbeatIndex())
        grid.downbeatOffset = (grid.downbeatOffset + 1) % std::max (1, grid.beatsPerBar);

    hasManualEdits = true;
    gridChanged();
    waveform.setSelectedBeat (index + countInBeats());
    setStatus ("Beat added at " + formatTime (timelineTime));
}

void MainComponent::deleteBeat (int shownIndex)
{
    const int i = shownIndex - countInBeats();
    if (i < 0 || i >= (int) grid.beats.size())
        return;

    pushUndo();
    grid.beats.erase (grid.beats.begin() + i);
    if (i < grid.firstDownbeatIndex())
        grid.downbeatOffset = (grid.downbeatOffset - 1 + grid.beatsPerBar) % std::max (1, grid.beatsPerBar);

    hasManualEdits = true;
    waveform.setSelectedBeat (-1);
    gridChanged();
    setStatus ("Beat deleted");
}

void MainComponent::setDownbeat (int shownIndex)
{
    if (shownIndex < 0 || shownIndex >= (int) shownGrid.beats.size())
        return;

    pushUndo();
    const int bpb = std::max (1, grid.beatsPerBar);
    grid.downbeatOffset = ((shownIndex - countInBeats()) % bpb + bpb) % bpb;
    gridChanged();
    setStatus ("Bar lines realigned");
}

void MainComponent::shiftDownbeat (int delta)
{
    if (grid.isEmpty()) return;
    pushUndo();
    const int bpb = std::max (1, grid.beatsPerBar);
    grid.downbeatOffset = ((grid.downbeatOffset + delta) % bpb + bpb) % bpb;
    gridChanged();
}

void MainComponent::autoDownbeat()
{
    if (grid.isEmpty() || ! analysis.has_value()) return;
    pushUndo();
    grid.downbeatOffset = ops::guessDownbeatOffset (grid.beats, grid.beatsPerBar, *analysis);
    gridChanged();
    setStatus ("Bar starts estimated from bass and harmonic changes");
}

void MainComponent::changeTempoOctave (bool doubleIt)
{
    if (grid.isEmpty()) return;
    pushUndo();
    grid.beats = doubleIt ? ops::doubleTempo (grid.beats) : ops::halveTempo (grid.beats, analysis ? &*analysis : nullptr);
    if (analysis.has_value())
        grid.downbeatOffset = ops::guessDownbeatOffset (grid.beats, grid.beatsPerBar, *analysis);
    hasManualEdits = true;
    refreshPreCount();   // the count-in follows the new tempo
    gridChanged();
    setStatus (juce::String (doubleIt ? "Tempo doubled" : "Tempo halved") + " - average now "
               + juce::String (grid.averageBpm(), 1) + " BPM");
}

void MainComponent::setTimeSignature (int num, int den)
{
    if (num <= 0 || den <= 0 || (num == grid.beatsPerBar && den == grid.denominator))
        return;

    pushUndo();
    grid.beatsPerBar = num;
    grid.denominator = den;
    if (analysis.has_value() && ! grid.isEmpty())
        grid.downbeatOffset = ops::guessDownbeatOffset (grid.beats, num, *analysis);
    else
        grid.downbeatOffset = grid.downbeatOffset % num;
    gridChanged();
}

//==============================================================================
void MainComponent::exportMidi()
{
    if (grid.isEmpty())
    {
        setStatus ("Nothing to export yet - open and analyze a song first.", true);
        return;
    }

    auto dir = currentFile.getParentDirectory();
    auto target = dir.getChildFile (currentFile.getFileNameWithoutExtension() + " - tempo map.mid");

    chooser = std::make_unique<juce::FileChooser> ("Export MIDI tempo map", target, "*.mid");
    chooser->launchAsync (juce::FileBrowserComponent::saveMode | juce::FileBrowserComponent::warnAboutOverwriting,
                          [this] (const juce::FileChooser& fc)
    {
        auto f = fc.getResult();
        if (f == juce::File())
            return;
        if (! f.hasFileExtension (".mid;.midi"))
            f = f.withFileExtension (".mid");

        MidiExportOptions o;
        o.ppq = ppqBox.getSelectedId() > 0 ? ppqBox.getSelectedId() : 960;
        o.includeClickTrack = clickTrackToggle.getToggleState();
        o.includeBarMarkers = markersToggle.getToggleState();
        o.songName = currentFile.getFileNameWithoutExtension();

        const auto pc = currentPreCount();
        const auto eg = ops::applyPreCount (grid, pc);
        const auto el = eg.computeLayout();
        const auto result = MidiExporter::exportToFile (eg, o, f);
        if (result.failed())
        {
            setStatus (result.getErrorMessage(), true);
            return;
        }

        setStatus ("Exported " + f.getFileName());
        saveSettings();

        juce::String msg;
        msg << "Saved " << f.getFileName() << "\n\n"
            << (int) grid.beats.size() << " beats, " << juce::String (grid.averageBpm(), 2) << " BPM average, "
            << grid.beatsPerBar << "/" << grid.denominator << ".\n";
        if (pc.isActive())
            msg << "Includes a " << pc.beats << "-beat pre-count: the audio must start "
                << juce::String (pc.silenceSeconds(), 3) << " s later (" << (juce::int64) pc.silenceSamples << " samples at "
                << juce::String (pc.sampleRate, 0) << " Hz). Use \"Pad audio folder\" to add this silence to the song and its stems.\n";
        if (el.preRollBeats > 0)
            msg << "The first " << el.preRollBeats << " beat(s) are a lead-in covering the audio before the first detected beat.\n";
        if (el.pickupBeats > 0)
            msg << "Bar 1 is a " << el.pickupBeats << "/" << grid.denominator << " pickup bar so that bar 2 starts on the downbeat.\n";
        msg << "\nIn Reaper: put the audio item at the very start of the project (0:00), then Insert > Media file... "
               "choose this MIDI file and answer Yes to \"import tempo map\". "
               "(Set the audio track's timebase to Time so it doesn't stretch.)";

        juce::Component::SafePointer<MainComponent> safe (this);
        juce::AlertWindow::showAsync (juce::MessageBoxOptions()
                                          .withIconType (juce::MessageBoxIconType::InfoIcon)
                                          .withTitle ("MIDI tempo map exported")
                                          .withMessage (msg)
                                          .withButton ("OK")
                                          .withButton ("Show in Finder")
                                          .withAssociatedComponent (this),
                                      [f] (int button) { if (button == 0) f.revealToUser(); });
    });
}

void MainComponent::exportCsv()
{
    if (grid.isEmpty())
    {
        setStatus ("Nothing to export yet.", true);
        return;
    }

    auto target = currentFile.getParentDirectory().getChildFile (currentFile.getFileNameWithoutExtension() + " - beats.csv");
    chooser = std::make_unique<juce::FileChooser> ("Export beats as CSV", target, "*.csv");
    chooser->launchAsync (juce::FileBrowserComponent::saveMode | juce::FileBrowserComponent::warnAboutOverwriting,
                          [this] (const juce::FileChooser& fc)
    {
        auto f = fc.getResult();
        if (f == juce::File()) return;
        if (! f.hasFileExtension (".csv")) f = f.withFileExtension (".csv");

        const auto r = MidiExporter::exportCsv (exportGrid(), f);
        setStatus (r.wasOk() ? "Exported " + f.getFileName() : r.getErrorMessage(), r.failed());
    });
}

void MainComponent::exportMetronomeAudio()
{
    if (grid.isEmpty() || audioBuffer == nullptr)
    {
        setStatus ("Nothing to export yet - open and analyze a song first.", true);
        return;
    }

    auto target = currentFile.getParentDirectory().getChildFile (currentFile.getFileNameWithoutExtension() + " - metronome.wav");
    chooser = std::make_unique<juce::FileChooser> ("Export metronome audio", target, "*.wav");
    chooser->launchAsync (juce::FileBrowserComponent::saveMode | juce::FileBrowserComponent::warnAboutOverwriting,
                          [this] (const juce::FileChooser& fc)
    {
        auto f = fc.getResult();
        if (f == juce::File() || audioBuffer == nullptr || grid.isEmpty()) return;
        if (! f.hasFileExtension (".wav;.wave")) f = f.withFileExtension (".wav");

        const auto pc = currentPreCount();
        const auto eg = ops::applyPreCount (grid, pc);
        const auto r = AudioEngine::exportMetronome (eg.beats, downbeatFlags (eg), accentToggle.getToggleState(),
                                                     fileSampleRate, audioBuffer->getNumSamples() + pc.silenceSamples, f);
        if (r.failed())
        {
            setStatus (r.getErrorMessage(), true);
            return;
        }

        setStatus ("Exported " + f.getFileName() + (pc.isActive() ? " (with " + juce::String (pc.beats) + "-beat pre-count)" : juce::String())
                   + " - place it at 0:00 next to the song");
    });
}

void MainComponent::padAudioFolder()
{
    const auto pc = currentPreCount();
    if (busy || ! pc.isActive())
    {
        setStatus ("Choose a pre-count and analyze a song first.", true);
        return;
    }

    chooser = std::make_unique<juce::FileChooser> ("Choose the folder with the audio files to pad", currentFile.getParentDirectory());
    chooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectDirectories,
                          [this, pc] (const juce::FileChooser& fc)
    {
        const auto folder = fc.getResult();
        if (! folder.isDirectory())
            return;

        const auto sources = AudioPadder::findAudioFiles (formatManager, folder);
        if (sources.isEmpty())
        {
            setStatus ("No audio files in " + folder.getFileName(), true);
            return;
        }

        const auto dest = folder.getChildFile ("Pre-count " + juce::String (pc.beats) + (pc.beats == 1 ? " beat" : " beats"));

        juce::String msg;
        msg << "Prepend " << juce::String (pc.silenceSeconds(), 3) << " s of silence (" << pc.beats << " beats, "
            << (juce::int64) pc.silenceSamples << " samples at " << juce::String (pc.sampleRate, 0) << " Hz) to "
            << sources.size() << " file(s):\n\n";
        for (int i = 0; i < sources.size(); ++i)
        {
            if (i == 15) { msg << "... and " << (sources.size() - 15) << " more\n"; break; }
            msg << "  " << sources[i].getFileName() << "\n";
        }
        msg << "\nPadded WAV copies go into the sub-folder \"" << dest.getFileName() << "\" (existing files there are replaced). "
               "The originals are not changed.\n\n"
               "Don't include a metronome WAV that was already exported with this pre-count.";

        juce::Component::SafePointer<MainComponent> safe (this);
        juce::AlertWindow::showAsync (juce::MessageBoxOptions()
                                          .withIconType (juce::MessageBoxIconType::QuestionIcon)
                                          .withTitle ("Pad audio folder")
                                          .withMessage (msg)
                                          .withButton ("Pad files")
                                          .withButton ("Cancel")
                                          .withAssociatedComponent (this),
                                      [safe, sources, dest, pc] (int button)
                                      {
                                          if (button == 1 && safe != nullptr)
                                              safe->startPadding (sources, dest, pc);
                                      });
    });
}

void MainComponent::startPadding (juce::Array<juce::File> sources, juce::File dest, PreCount pc)
{
    if (busy)
        return;

    if (const auto r = dest.createDirectory(); r.failed())
    {
        setStatus ("Can't create " + dest.getFullPathName() + ": " + r.getErrorMessage(), true);
        return;
    }

    engine.pause();
    cancelRequested = false;
    progress = 0.0f;
    setBusy (true, "Padding " + juce::String (sources.size()) + " file(s)...");

    juce::Component::SafePointer<MainComponent> safe (this);

    pool.addJob ([this, safe, sources, dest, pc]
    {
        juce::AudioFormatManager formats;
        formats.registerBasicFormats();

        const auto targets = AudioPadder::destinationsFor (sources, dest);
        juce::StringArray errors;
        int written = 0;

        for (int i = 0; i < sources.size() && ! cancelRequested.load(); ++i)
        {
            const auto r = AudioPadder::padFile (formats, sources[i], targets[i], pc, [this, i, n = sources.size()] (float p)
            {
                progress = ((float) i + p) / (float) n;
                return ! cancelRequested.load();
            });

            if (r.wasOk())                 ++written;
            else if (! cancelRequested)    errors.add (sources[i].getFileName() + ": " + r.getErrorMessage());
        }

        const bool cancelled = cancelRequested.load();
        juce::MessageManager::callAsync ([safe, dest, written, errors, cancelled, total = sources.size()]
        {
            if (safe == nullptr)
                return;

            safe->setBusy (false);

            if (cancelled)
            {
                safe->setStatus ("Padding cancelled after " + juce::String (written) + " of " + juce::String (total) + " file(s).", true);
                return;
            }

            safe->setStatus ("Padded " + juce::String (written) + " of " + juce::String (total) + " file(s) into " + dest.getFileName(),
                             ! errors.isEmpty());

            juce::String msg;
            msg << written << " of " << total << " file(s) written to\n" << dest.getFullPathName() << "\n";
            if (! errors.isEmpty())
                msg << "\nFailed:\n" << errors.joinIntoString ("\n") << "\n";
            msg << "\nIn Reaper, put these files, the metronome WAV and the MIDI tempo map (exported with the same "
                   "pre-count) all at 0:00.";

            juce::AlertWindow::showAsync (juce::MessageBoxOptions()
                                              .withIconType (errors.isEmpty() ? juce::MessageBoxIconType::InfoIcon
                                                                              : juce::MessageBoxIconType::WarningIcon)
                                              .withTitle ("Audio folder padded")
                                              .withMessage (msg)
                                              .withButton ("OK")
                                              .withButton ("Show in Finder")
                                              .withAssociatedComponent (safe.getComponent()),
                                          [dest] (int button) { if (button == 0) dest.revealToUser(); });
        });
    });
}

//==============================================================================
void MainComponent::togglePlay()
{
    if (audioBuffer == nullptr) return;
    if (engine.isPlaying()) engine.pause();
    else                    engine.play();
}

void MainComponent::returnToStart()
{
    engine.setPosition (loopButton.getToggleState() && loopEnd > loopStart ? loopStart : 0.0);
    if (followButton.getToggleState())
    {
        const double span = viewEnd - viewStart;
        if (engine.getPosition() < viewStart || engine.getPosition() > viewEnd)
            setViewRange (engine.getPosition() - span * 0.1, engine.getPosition() + span * 0.9);
    }
}

void MainComponent::setViewRange (double start, double end)
{
    const double dur = audioBuffer != nullptr ? engine.getDuration() : 10.0;
    double span = juce::jlimit (0.05, std::max (0.05, dur), end - start);
    start = juce::jlimit (0.0, std::max (0.0, dur - span), start);

    viewStart = start;
    viewEnd = start + span;

    scrollBar.setRangeLimits (0.0, dur, juce::dontSendNotification);
    scrollBar.setCurrentRange (viewStart, span, juce::dontSendNotification);
    waveform.setVisibleRange (viewStart, viewEnd);
    tempoView.setVisibleRange (viewStart, viewEnd);
}

void MainComponent::zoom (double factor, std::optional<double> anchor)
{
    const double span = viewEnd - viewStart;
    const double a = anchor.value_or (engine.getPosition() >= viewStart && engine.getPosition() <= viewEnd
                                          ? engine.getPosition() : viewStart + span * 0.5);
    const double ratio = (a - viewStart) / span;
    const double newSpan = span * factor;
    setViewRange (a - ratio * newSpan, a + (1.0 - ratio) * newSpan);
}

void MainComponent::zoomToFit()
{
    setViewRange (0.0, audioBuffer != nullptr ? engine.getDuration() : 10.0);
}

void MainComponent::setLoop (double start, double end, bool enabled)
{
    loopStart = start;
    loopEnd = end;
    const bool on = enabled && end > start + 0.05;
    loopButton.setToggleState (on, juce::dontSendNotification);
    engine.setLoop (start, end, on);
    waveform.setLoop (start, end, on);

    if (on && (engine.getPosition() < start || engine.getPosition() > end))
        engine.setPosition (start);
}

void MainComponent::jumpBeats (int delta, bool bars)
{
    if (shownGrid.isEmpty()) return;

    const double pos = engine.getPosition();
    int i = shownGrid.beatIndexAtOrBefore (pos + (delta < 0 ? -0.02 : 0.0));
    if (delta < 0 && i >= 0 && pos - shownGrid.beats[(size_t) i] > 0.02)
        ++i; // first step back lands on the current beat

    int target = i;
    if (bars)
    {
        target = i + delta;
        while (target > 0 && target < (int) shownGrid.beats.size() - 1 && ! shownGrid.isDownbeat (target))
            target += delta > 0 ? 1 : -1;
    }
    else
    {
        target = i + delta;
    }

    target = juce::jlimit (0, (int) shownGrid.beats.size() - 1, target);
    engine.setPosition (shownGrid.beats[(size_t) target]);
}

//==============================================================================
void MainComponent::setBusy (bool isBusy, const juce::String& message)
{
    busy = isBusy;
    progressValue = isBusy ? -1.0 : 0.0;   // -1 = indeterminate until first progress report
    progressBar.setVisible (isBusy);
    cancelButton.setVisible (isBusy);
    if (message.isNotEmpty())
        setStatus (message);
    updateControlStates();
}

void MainComponent::setStatus (const juce::String& message, bool isError)
{
    statusLabel.setText (message, juce::dontSendNotification);
    statusLabel.setColour (juce::Label::textColourId, isError ? theme::danger : theme::textDim);
}

void MainComponent::updateInfoLabels()
{
    const auto lo = (int) rangeSlider.getMinValue(), hi = (int) rangeSlider.getMaxValue();
    rangeLabel.setText ("BPM " + juce::String (lo) + "-" + juce::String (hi), juce::dontSendNotification);

    {
        const int pcBeats = std::max (0, preCountBox.getSelectedId() - 1);
        const auto pc = currentPreCount();
        juce::String info;
        if (pcBeats == 0)
            info = "No count-in; exports start at the audio start";
        else if (! pc.isActive())
            info = "Analyze a song to compute the silence";
        else
            info << "Adds " << juce::String (pc.silenceSeconds(), 3) << " s of silence (" << (juce::int64) pc.silenceSamples
                 << " samples @ " << juce::String (pc.sampleRate / 1000.0, 1) << " kHz)";
        preCountInfoLabel.setText (info, juce::dontSendNotification);
    }

    if (grid.isEmpty())
    {
        gridInfoLabel.setText ("No beats yet", juce::dontSendNotification);
        return;
    }

    double mn = 1.0e9, mx = 0.0;
    for (int i = 0; i + 1 < (int) grid.beats.size(); ++i)
    {
        const double b = grid.bpmOfInterval (i);
        mn = std::min (mn, b);
        mx = std::max (mx, b);
    }

    const int bars = grid.barAndBeatOfDetected ((int) grid.beats.size() - 1, layout).first;
    gridInfoLabel.setText (juce::String (grid.beats.size()) + " beats  -  " + juce::String (bars) + " bars  -  avg "
                               + juce::String (grid.averageBpm(), 2) + " BPM\nrange " + juce::String (mn, 1) + " - "
                               + juce::String (mx, 1) + " BPM" + (hasManualEdits ? "  -  edited" : ""),
                           juce::dontSendNotification);
}

void MainComponent::updateControlStates()
{
    const bool hasAudio = audioBuffer != nullptr;
    const bool hasGrid = ! grid.isEmpty();

    analyzeButton.setEnabled (hasAudio && ! busy);
    openButton.setEnabled (true);
    exportButton.setEnabled (hasGrid && ! busy);
    exportMidiButton.setEnabled (hasGrid && ! busy);
    exportCsvButton.setEnabled (hasGrid && ! busy);
    exportClickButton.setEnabled (hasGrid && hasAudio && ! busy);
    padFolderButton.setEnabled (hasGrid && currentPreCount().isActive() && ! busy);

    for (auto* b : { &downbeatPrevButton, &downbeatNextButton, &doubleButton, &halveButton })
        b->setEnabled (hasGrid && ! busy);
    downbeatAutoButton.setEnabled (hasGrid && analysis.has_value() && ! busy);
    halfBeatButton.setEnabled (analysis.has_value() && ! busy);
    undoButton.setEnabled (! undoStack.empty() && ! busy);
    redoButton.setEnabled (! redoStack.empty() && ! busy);

    for (auto* b : std::initializer_list<juce::Component*> { &playButton, &stopButton, &toStartButton, &loopButton,
                                                              &zoomInButton, &zoomOutButton, &zoomFitButton })
        b->setEnabled (hasAudio);

    smoothingSlider.setEnabled (! constantToggle.getToggleState());
}

//==============================================================================
bool MainComponent::keyPressed (const juce::KeyPress& key)
{
    const auto mods = key.getModifiers();
    const int code = key.getKeyCode();
    const auto ch = juce::CharacterFunctions::toUpperCase ((juce::juce_wchar) code);

    if (mods.isCommandDown())
    {
        switch (ch)
        {
            case 'O': showOpenDialog(); return true;
            case 'R': startAnalysis(); return true;
            case 'E':
                if (mods.isAltDown())        exportMetronomeAudio();
                else if (mods.isShiftDown()) exportCsv();
                else                         exportMidi();
                return true;
            case 'Z': if (mods.isShiftDown()) redo(); else undo(); return true;
            case '=': case '+': zoom (0.5); return true;
            case '-': zoom (2.0); return true;
            case '0': zoomToFit(); return true;
            default: break;
        }
        return false;
    }

    if (key == juce::KeyPress::spaceKey)   { togglePlay(); return true; }
    if (key == juce::KeyPress::returnKey || key == juce::KeyPress::homeKey) { returnToStart(); return true; }
    if (key == juce::KeyPress::escapeKey)  { if (busy) cancelRequested = true; else engine.stop(); return true; }

    const int sel = waveform.getSelectedBeat();

    if (key == juce::KeyPress::deleteKey || key == juce::KeyPress::backspaceKey)
    {
        if (sel >= 0) deleteBeat (sel);
        return true;
    }

    if (key.isKeyCode (juce::KeyPress::leftKey) || key.isKeyCode (juce::KeyPress::rightKey))
    {
        const int dir = key.isKeyCode (juce::KeyPress::leftKey) ? -1 : 1;
        if (sel >= countInBeats() && sel < (int) shownGrid.beats.size())
        {
            pushUndo();
            moveBeat (sel, shownGrid.beats[(size_t) sel] + dir * (mods.isShiftDown() ? 0.010 : 0.001));
            hasManualEdits = true;
            updateInfoLabels();
        }
        else
        {
            jumpBeats (dir, mods.isShiftDown());
        }
        return true;
    }

    switch (ch)
    {
        case 'M': metronomeToggle.setToggleState (! metronomeToggle.getToggleState(), juce::sendNotification); return true;
        case 'L': setLoop (loopStart, loopEnd, ! loopButton.getToggleState()); return true;
        case 'F': followButton.setToggleState (! followButton.getToggleState(), juce::dontSendNotification); return true;
        case 'B': addBeat (engine.getPosition()); return true;
        case 'D': if (sel >= 0) setDownbeat (sel); return true;
        case '=': case '+': zoom (0.5); return true;
        case '-': zoom (2.0); return true;
        case '0': zoomToFit(); return true;
        default: break;
    }

    return false;
}

//==============================================================================
juce::StringArray MainComponent::getMenuBarNames()
{
    return { "File", "Edit", "Transport", "View", "Help" };
}

juce::PopupMenu MainComponent::getMenuForIndex (int index, const juce::String&)
{
    juce::PopupMenu m;
    const bool hasAudio = audioBuffer != nullptr, hasGrid = ! grid.isEmpty();

    auto item = [&m] (int id, const juce::String& text, const juce::String& shortcut, bool enabled = true, bool ticked = false)
    {
        juce::PopupMenu::Item i (text);
        i.itemID = id;
        i.isEnabled = enabled;
        i.isTicked = ticked;
        i.shortcutKeyDescription = shortcut;
        m.addItem (i);
    };

    switch (index)
    {
        case 0:
            item (menuOpen, "Open...", "Cmd+O");
            item (menuAnalyze, "Analyze again", "Cmd+R", hasAudio && ! busy);
            m.addSeparator();
            item (menuExportMidi, "Export MIDI tempo map...", "Cmd+E", hasGrid);
            item (menuExportCsv, "Export beats as CSV...", "Cmd+Shift+E", hasGrid);
            item (menuExportClick, "Export metronome audio...", "Cmd+Option+E", hasGrid && hasAudio);
            item (menuPadFolder, "Pad audio folder with pre-count...", {}, hasGrid && currentPreCount().isActive() && ! busy);
            m.addSeparator();
            item (menuAudioSettings, "Audio settings...", {});
            break;
        case 1:
            item (menuUndo, "Undo", "Cmd+Z", ! undoStack.empty());
            item (menuRedo, "Redo", "Cmd+Shift+Z", ! redoStack.empty());
            m.addSeparator();
            item (menuAddBeatAtPlayhead, "Add beat at playhead", "B", hasAudio);
            item (menuDeleteBeat, "Delete selected beat", "Delete", waveform.getSelectedBeat() >= 0);
            m.addSeparator();
            item (menuDoubleTempo, "Double tempo", {}, hasGrid);
            item (menuHalveTempo, "Halve tempo", {}, hasGrid);
            item (menuAutoDownbeat, "Detect bar starts", {}, hasGrid && analysis.has_value());
            break;
        case 2:
            item (menuPlay, engine.isPlaying() ? "Pause" : "Play", "Space", hasAudio);
            item (menuToStart, "Return to start", "Enter", hasAudio);
            m.addSeparator();
            item (menuMetronome, "Metronome", "M", true, metronomeToggle.getToggleState());
            item (menuLoop, "Loop", "L", hasAudio, loopButton.getToggleState());
            item (menuFollow, "Follow playhead", "F", true, followButton.getToggleState());
            break;
        case 3:
            item (menuZoomIn, "Zoom in", "+", hasAudio);
            item (menuZoomOut, "Zoom out", "-", hasAudio);
            item (menuZoomFit, "Zoom to fit", "0", hasAudio);
            break;
        case 4:
            item (menuShortcuts, "Keyboard shortcuts && tips", {});
            m.addSeparator();
            item (menuAbout, "About Beat It", {});
            break;
        default: break;
    }

    return m;
}

void MainComponent::menuItemSelected (int id, int)
{
    switch (id)
    {
        case menuOpen:              showOpenDialog(); break;
        case menuAnalyze:           startAnalysis(); break;
        case menuExportMidi:        exportMidi(); break;
        case menuExportCsv:         exportCsv(); break;
        case menuExportClick:       exportMetronomeAudio(); break;
        case menuPadFolder:         padAudioFolder(); break;
        case menuAudioSettings:     showAudioSettings(); break;
        case menuUndo:              undo(); break;
        case menuRedo:              redo(); break;
        case menuAddBeatAtPlayhead: addBeat (engine.getPosition()); break;
        case menuDeleteBeat:        deleteBeat (waveform.getSelectedBeat()); break;
        case menuDoubleTempo:       changeTempoOctave (true); break;
        case menuHalveTempo:        changeTempoOctave (false); break;
        case menuAutoDownbeat:      autoDownbeat(); break;
        case menuPlay:              togglePlay(); break;
        case menuToStart:           returnToStart(); break;
        case menuMetronome:         metronomeToggle.setToggleState (! metronomeToggle.getToggleState(), juce::sendNotification); break;
        case menuLoop:              setLoop (loopStart, loopEnd, ! loopButton.getToggleState()); break;
        case menuFollow:            followButton.setToggleState (! followButton.getToggleState(), juce::dontSendNotification); break;
        case menuZoomIn:            zoom (0.5); break;
        case menuZoomOut:           zoom (2.0); break;
        case menuZoomFit:           zoomToFit(); break;
        case menuShortcuts:         showShortcuts(); break;
        case menuAbout:             showAbout(); break;
        default: break;
    }
}

void MainComponent::showAudioSettings()
{
    auto* selector = new juce::AudioDeviceSelectorComponent (deviceManager, 0, 0, 2, 2, false, false, true, false);
    selector->setSize (520, 360);

    juce::DialogWindow::LaunchOptions o;
    o.content.setOwned (selector);
    o.dialogTitle = "Audio settings";
    o.dialogBackgroundColour = theme::panel;
    o.escapeKeyTriggersCloseButton = true;
    o.useNativeTitleBar = true;
    o.resizable = false;
    o.componentToCentreAround = this;
    o.launchAsync();
}

void MainComponent::showAbout()
{
    juce::AlertWindow::showAsync (juce::MessageBoxOptions()
        .withIconType (juce::MessageBoxIconType::NoIcon)
        .withTitle ("Beat It " + appVersion)
        .withMessage ("Variable-tempo mapper -> MIDI tempo map.\n\n"
                      "Beat tracking: BTrack (Adam Stark, GPL-3)\n"
                      "Resampling: libsamplerate\n"
                      "Built with JUCE " + juce::SystemStats::getJUCEVersion().fromFirstOccurrenceOf ("v", false, false) + "\n\n"
                      "Licensed under the GPL-3.")
        .withButton ("OK")
        .withAssociatedComponent (this),
        nullptr);
}

void MainComponent::showShortcuts()
{
    juce::AlertWindow::showAsync (juce::MessageBoxOptions()
        .withIconType (juce::MessageBoxIconType::NoIcon)
        .withTitle ("Keyboard shortcuts & tips")
        .withMessage ("Space  play / pause\n"
                      "Enter  return to start (or loop start)\n"
                      "Left / Right  previous / next beat (Shift: bar)\n"
                      "M  metronome on/off     L  loop     F  follow playhead\n"
                      "B  add a beat at the playhead (tap along while playing)\n"
                      "D  make the selected beat a bar start\n"
                      "Delete  delete the selected beat\n"
                      "Left / Right with a beat selected: nudge 1 ms (Shift: 10 ms)\n"
                      "+ / - / 0  zoom in / out / fit   (Cmd+scroll or pinch to zoom)\n"
                      "Cmd+Z / Cmd+Shift+Z  undo / redo\n\n"
                      "Mouse: drag beat lines to move them (hold Cmd for fine control), double-click to add, "
                      "Option-click to delete, Shift-drag to set a loop, right-click for more.\n\n"
                      "Workflow: open a song, check the beats with the metronome, fix the bar start, "
                      "then Export MIDI and import it into Reaper with \"import tempo map\".")
        .withButton ("OK")
        .withAssociatedComponent (this),
        nullptr);
}
