#pragma once

#include "AudioEngine.h"
#include "AudioPadder.h"
#include "BeatAnalyzer.h"
#include "MidiExporter.h"
#include "TempoCurveView.h"
#include "Theme.h"
#include "WaveformView.h"

#include <juce_audio_utils/juce_audio_utils.h>
#include <optional>
#include <set>

//==============================================================================
/** Text button that draws a vector transport icon instead of text. */
class IconButton : public juce::Button
{
public:
    enum class Icon { play, pause, stop, toStart, loop, follow, zoomIn, zoomOut, zoomFit, undo, redo };

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
/** Container for the scrollable side panel; painting/layout delegated to the owner.
    Section headers are clickable (collapse / expand). */
class SidePanel : public juce::Component
{
public:
    std::function<void (juce::Graphics&)> painter;
    std::function<void()> layout;
    std::function<int (juce::Point<int>)> headerAt;      ///< section index under the point, or -1
    std::function<void (int)> onHeaderClicked;
    int hoveredHeader = -1;

    void paint (juce::Graphics& g) override  { if (painter) painter (g); }
    void resized() override                  { if (layout) layout(); }
    void mouseMove (const juce::MouseEvent& e) override
    {
        const int h = headerAt ? headerAt (e.getPosition()) : -1;
        if (h != hoveredHeader)
        {
            hoveredHeader = h;
            setMouseCursor (h >= 0 ? juce::MouseCursor::PointingHandCursor : juce::MouseCursor::NormalCursor);
            repaint();
        }
    }
    void mouseExit (const juce::MouseEvent&) override   { hoveredHeader = -1; setMouseCursor ({}); repaint(); }
    void mouseDown (const juce::MouseEvent& e) override
    {
        const int h = headerAt ? headerAt (e.getPosition()) : -1;
        if (h >= 0 && onHeaderClicked) onHeaderClicked (h);
    }
};

//==============================================================================
/** Transport readout: time, bar, beat and tempo in a display panel. */
class ReadoutDisplay : public juce::Component
{
public:
    void setValues (const juce::String& time, const juce::String& bar, const juce::String& beat,
                    const juce::String& bpm, bool countIn)
    {
        if (time == timeText && bar == barText && beat == beatText && bpm == bpmText && countIn == inCountIn)
            return;
        timeText = time; barText = bar; beatText = beat; bpmText = bpm; inCountIn = countIn;
        repaint();
    }
    void paint (juce::Graphics&) override;

private:
    juce::String timeText { "0:00.000" }, barText { "-" }, beatText { "-" }, bpmText { "-" };
    bool inCountIn = false;
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
    void reprocess (bool pushUndoFirst, bool reguessBarStarts = false);
    void readProcessingOptionsFromUI();

    // grid editing
    void pushUndo();
    void undo();
    void redo();
    void gridChanged();
    static std::vector<bool> downbeatFlags (const beatit::BeatGrid&);
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
    void exportMetronomeAudio();
    void padAudioFolder();
    void startPadding (juce::Array<juce::File> sources, juce::File destFolder, beatit::PreCount preCount);
    beatit::PreCount currentPreCount() const   { return preCount; }
    beatit::BeatGrid exportGrid() const        { return shownGrid; }

    // pre-count timeline: the editor, playback and exports show the song delayed by the
    // pre-count silence with the count-in beats in front. `grid` stays in song time.
    int    selectedPreCountBeats() const;
    int    countInBeats() const noexcept        { return preCount.isActive() ? preCount.beats : 0; }
    double timelineOffset() const noexcept      { return preCount.isActive() ? preCount.silenceSeconds() : 0.0; }
    double songDuration() const;
    void   refreshPreCount();                   ///< recompute the silence from the grid + selected beats
    void   setPreCount (beatit::PreCount pc);
    void   updateShownGrid();

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
    void showAbout();
    void showExportMenu();
    void applyTheme (theme::Id id, bool save);
    void toggleSection (int index);

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
    beatit::PreCount preCount;                  ///< silence amount, fixed until the setting or the tempo changes
    beatit::BeatGrid shownGrid;                 ///< grid with the pre-count applied (timeline time)
    beatit::GridLayout shownLayout;

    struct DocState { beatit::BeatGrid grid; beatit::PreCount preCount; };
    std::vector<DocState> undoStack, redoStack;
    void restoreState (const DocState&);
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
    juce::TextButton openButton { "Open..." }, analyzeButton { "Analyze" },
                     exportButton { juce::CharPointer_UTF8 ("Export  \xe2\x96\xbe") },
                     audioSettingsButton { "Audio..." };
    IconButton undoButton { IconButton::Icon::undo, "Undo (Cmd+Z)" },
               redoButton { IconButton::Icon::redo, "Redo (Cmd+Shift+Z)" };
    juce::Label fileLabel, fileInfoLabel;

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
    ReadoutDisplay readout;

    // side panel
    juce::Viewport sideViewport;
    SidePanel side;
    struct Section { juce::String title; juce::Rectangle<int> header, body; bool collapsed = false; };
    std::vector<Section> sections;
    std::set<juce::String> collapsedSections;
    juce::String statusText;
    bool statusIsError = false;

    juce::Label rangeLabel, smoothingLabel, offsetLabel;
    juce::Slider rangeSlider { juce::Slider::TwoValueHorizontal, juce::Slider::NoTextBox };
    juce::Slider smoothingSlider, offsetSlider;
    juce::ToggleButton snapToggle { "Snap beats to transients" }, fixToggle { "Fix missed / extra beats" },
                       silenceToggle { "Ignore silent intro / outro" }, constantToggle { "Constant tempo (straight grid)" },
                       autoAnalyzeToggle { "Analyze automatically on load" };

    juce::Label timeSigLabel, downbeatLabel, tempoOctaveLabel, gridInfoLabel;
    juce::ComboBox numeratorBox, denominatorBox;
    juce::TextButton downbeatPrevButton { "<" }, downbeatNextButton { ">" }, downbeatAutoButton { "Auto" },
                     halfBeatButton { juce::CharPointer_UTF8 ("\xc2\xbd beat") },
                     doubleButton { juce::CharPointer_UTF8 ("\xc3\x97" "2") }, halveButton { juce::CharPointer_UTF8 ("\xc3\xb7" "2") };

    juce::Label musicLabel, clickLabel, speedLabel;
    juce::Slider musicSlider, clickSlider, speedSlider;
    juce::ToggleButton muteToggle { "Mute" }, metronomeToggle { "Click" }, accentToggle { "Accent" };

    juce::Label ppqLabel, preCountLabel, preCountInfoLabel;
    juce::ComboBox ppqBox, preCountBox;
    juce::ToggleButton clickTrackToggle { "Include click note track" }, markersToggle { "Include bar markers" };
    juce::TextButton exportMidiButton { "Export MIDI tempo map..." }, exportCsvButton { "CSV..." },
                     exportClickButton { "Metronome WAV..." },
                     padFolderButton { "Pad audio folder with pre-count..." };

    // status bar
    juce::Label statusLabel;
    juce::ProgressBar progressBar { progressValue };
    juce::TextButton cancelButton { "Cancel" };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MainComponent)
};
