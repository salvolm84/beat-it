#pragma once

#include "AudioEngine.h"
#include "BeatAnalyzer.h"
#include "MidiExporter.h"
#include "TempoCurveView.h"
#include "Theme.h"
#include "WaveformView.h"

#include <juce_audio_utils/juce_audio_utils.h>
#include <optional>

//==============================================================================
/** Text button that draws a vector transport icon instead of text. */
class IconButton : public juce::Button
{
public:
    enum class Icon { play, pause, stop, toStart, loop, follow, zoomIn, zoomOut, zoomFit };

    explicit IconButton (Icon i, const juce::String& tooltip = {}) : juce::Button ({}), icon (i)
    {
        setTooltip (tooltip);
        setWantsKeyboardFocus (false);
    }

    void setIcon (Icon i)  { if (icon != i) { icon = i; repaint(); } }
    void paintButton (juce::Graphics&, bool highlighted, bool down) override;

private:
    Icon icon;
};

//==============================================================================
/** Container for the scrollable side panel; painting/layout delegated to the owner. */
class SidePanel : public juce::Component
{
public:
    std::function<void (juce::Graphics&)> painter;
    std::function<void()> layout;
    void paint (juce::Graphics& g) override  { if (painter) painter (g); }
    void resized() override                  { if (layout) layout(); }
};

//==============================================================================
class MainComponent : public juce::Component,
                      public juce::MenuBarModel,
                      public juce::FileDragAndDropTarget,
                      private juce::Timer,
                      private juce::ScrollBar::Listener
{
public:
    MainComponent();
    ~MainComponent() override;

    void openFile (const juce::File& file);

    //==============================================================================
    void paint (juce::Graphics&) override;
    void paintOverChildren (juce::Graphics&) override;
    void resized() override;
    bool keyPressed (const juce::KeyPress&) override;

    bool isInterestedInFileDrag (const juce::StringArray& files) override;
    void fileDragEnter (const juce::StringArray&, int, int) override  { dragHighlight = true; repaint(); }
    void fileDragExit (const juce::StringArray&) override             { dragHighlight = false; repaint(); }
    void filesDropped (const juce::StringArray& files, int, int) override;

    juce::StringArray getMenuBarNames() override;
    juce::PopupMenu getMenuForIndex (int, const juce::String&) override;
    void menuItemSelected (int, int) override;

private:
    //==============================================================================
    void timerCallback() override;
    void scrollBarMoved (juce::ScrollBar*, double newRangeStart) override;

    // file / analysis
    void showOpenDialog();
    void onAudioLoaded (std::shared_ptr<juce::AudioBuffer<float>> buffer, double sampleRate, const juce::File& file, int generation);
    void startAnalysis();
    void onAnalysisFinished (std::optional<beatit::AnalysisResult> result, int generation);
    void reprocess (bool pushUndoFirst);
    void readProcessingOptionsFromUI();

    // grid editing
    void pushUndo();
    void undo();
    void redo();
    void gridChanged();
    void moveBeat (int index, double t);
    void addBeat (double t);
    void deleteBeat (int index);
    void setDownbeat (int index);
    void shiftDownbeat (int delta);
    void autoDownbeat();
    void changeTempoOctave (bool doubleIt);
    void setTimeSignature (int numerator, int denominator);

    // export
    void exportMidi();
    void exportCsv();

    // transport / view
    void togglePlay();
    void returnToStart();
    void setViewRange (double start, double end);
    void zoom (double factor, std::optional<double> anchor = std::nullopt);
    void zoomToFit();
    void setLoop (double start, double end, bool enabled);
    void jumpBeats (int delta, bool bars);

    // misc
    void setBusy (bool isBusy, const juce::String& message = {});
    void setStatus (const juce::String& message, bool isError = false);
    void updateInfoLabels();
    void updateControlStates();
    void layoutSidePanel();
    void paintSidePanel (juce::Graphics&);
    void loadSettings();
    void saveSettings();
    void showAudioSettings();
    void showShortcuts();

    //==============================================================================
    BeatItLookAndFeel lnf;
    juce::TooltipWindow tooltips { this, 600 };

    juce::AudioDeviceManager deviceManager;
    juce::AudioSourcePlayer sourcePlayer;
    AudioEngine engine;
    juce::AudioFormatManager formatManager;
    juce::ThreadPool pool { juce::ThreadPoolOptions{}.withNumberOfThreads (1).withThreadName ("BeatIt worker") };
    std::unique_ptr<juce::FileChooser> chooser;
    std::unique_ptr<juce::PropertiesFile> settings;

    // document state
    std::shared_ptr<juce::AudioBuffer<float>> audioBuffer;
    double fileSampleRate = 44100.0;
    juce::File currentFile;
    std::optional<beatit::AnalysisResult> analysis;
    beatit::ProcessingOptions procOptions;
    beatit::BeatGrid grid;
    beatit::GridLayout layout;
    std::vector<beatit::BeatGrid> undoStack, redoStack;
    bool hasManualEdits = false;
    int loadGeneration = 0;

    // background work
    std::atomic<float> progress { 0.0f };
    std::atomic<bool> cancelRequested { false };
    double progressValue = 0.0;
    bool busy = false;

    // view
    double viewStart = 0.0, viewEnd = 10.0;
    double loopStart = 0.0, loopEnd = 0.0;
    bool dragHighlight = false;

    //==============================================================================
    // top bar
    juce::TextButton openButton { "Open..." }, analyzeButton { "Analyze" }, exportButton { "Export MIDI..." },
                     audioSettingsButton { "Audio..." };
    juce::Label fileLabel;

    WaveformView waveform;
    TempoCurveView tempoView;
    juce::ScrollBar scrollBar { false };

    // transport
    IconButton toStartButton { IconButton::Icon::toStart, "Return to start (Enter)" },
               playButton    { IconButton::Icon::play, "Play / pause (Space)" },
               stopButton    { IconButton::Icon::stop, "Stop" },
               loopButton    { IconButton::Icon::loop, "Loop region (L) - shift-drag on the waveform to set it" },
               followButton  { IconButton::Icon::follow, "Follow playhead (F)" },
               zoomOutButton { IconButton::Icon::zoomOut, "Zoom out (-)" },
               zoomInButton  { IconButton::Icon::zoomIn, "Zoom in (+)" },
               zoomFitButton { IconButton::Icon::zoomFit, "Zoom to fit (0)" };
    juce::Label timeLabel, barBeatLabel, bpmLabel;

    // side panel
    juce::Viewport sideViewport;
    SidePanel side;
    std::vector<std::pair<juce::String, juce::Rectangle<int>>> sections;

    juce::Label rangeLabel, smoothingLabel, offsetLabel;
    juce::Slider rangeSlider { juce::Slider::TwoValueHorizontal, juce::Slider::NoTextBox };
    juce::Slider smoothingSlider, offsetSlider;
    juce::ToggleButton snapToggle { "Snap beats to transients" }, fixToggle { "Fix missed / extra beats" },
                       silenceToggle { "Ignore silent intro / outro" }, constantToggle { "Constant tempo (straight grid)" },
                       autoAnalyzeToggle { "Analyze automatically on load" };

    juce::Label timeSigLabel, downbeatLabel, tempoOctaveLabel, gridInfoLabel, editHintLabel;
    juce::ComboBox numeratorBox, denominatorBox;
    juce::TextButton downbeatPrevButton { "<" }, downbeatNextButton { ">" }, downbeatAutoButton { "Auto" },
                     doubleButton { "x2" }, halveButton { "/2" }, undoButton { "Undo" }, redoButton { "Redo" };

    juce::Label musicLabel, clickLabel, speedLabel;
    juce::Slider musicSlider, clickSlider, speedSlider;
    juce::ToggleButton muteToggle { "Mute" }, metronomeToggle { "Metronome" }, accentToggle { "Accent bar starts" };

    juce::Label ppqLabel;
    juce::ComboBox ppqBox;
    juce::ToggleButton clickTrackToggle { "Include click note track" }, markersToggle { "Include bar markers" };
    juce::TextButton exportMidiButton { "Export MIDI tempo map..." }, exportCsvButton { "CSV..." };

    // status bar
    juce::Label statusLabel;
    juce::ProgressBar progressBar { progressValue };
    juce::TextButton cancelButton { "Cancel" };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MainComponent)
};
