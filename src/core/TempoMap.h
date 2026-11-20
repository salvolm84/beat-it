#pragma once

#include <array>
#include <vector>
#include <utility>

namespace beatit
{

//==============================================================================
/** Everything the analyzer produced. Kept around so post-processing options
    (smoothing, snapping, constant tempo, ...) can be re-applied instantly
    without re-running the beat tracker. */
struct AnalysisResult
{
    double durationSeconds = 0.0;

    /** Beats from BTrack (forward + backward pass stitched), in seconds. */
    std::vector<double> rawBeats;

    /** The individual BTrack passes (kept for diagnostics). */
    std::vector<double> forwardBeats, backwardBeats;

    /** High-resolution onset strength (spectral flux), used for snapping
        beats to transients. Sample n is at time odfTimeOffset + n * odfHop. */
    std::vector<float> odf;
    double odfHop = 0.0;
    double odfTimeOffset = 0.0;

    /** Coarse envelopes (one value per envHop seconds) used for downbeat
        guessing and silence trimming. */
    std::vector<float> rmsEnvelope;
    std::vector<float> lowBandEnvelope;
    double envHop = 0.0;

    /** 12-bin chromagram (pitch-class energy, each frame L2-normalised),
        frame n centred at n * chromaHop. Used to find harmonic changes,
        which usually happen on bar lines. */
    std::vector<std::array<float, 12>> chroma;
    double chromaHop = 0.0;

    float odfAt (double timeSeconds) const;
    float rmsAt (double timeSeconds) const;
    float lowBandAt (double timeSeconds) const;

    /** Maximum of the onset function within +-halfWidth seconds of t. */
    float peakOdfNear (double timeSeconds, double halfWidth) const;
};

//==============================================================================
struct ProcessingOptions
{
    double minBpm = 55.0;        ///< octave correction range
    double maxBpm = 200.0;
    bool   fixOutliers = true;   ///< insert missed beats / drop extra beats
    bool   snapToTransients = true;
    int    smoothing = 2;        ///< half-window in beats for local linear fit (0 = off)
    bool   constantTempo = false;
    bool   trimSilence = true;   ///< drop beats in leading/trailing silence
    double offsetMs = 0.0;       ///< global offset added to every beat
};

//==============================================================================
/** How the beat list is laid out on a bar/beat grid. Shared by the UI (bar
    numbers) and the MIDI exporter so both always agree. */
struct GridLayout
{
    int preRollBeats = 0;        ///< virtual beats inserted between t=0 and the first detected beat
    int pickupBeats = 0;         ///< length of a partial first bar (0 = none)
    int beatsPerBar = 4;
    bool firstBeatSnappedToZero = false;

    /** Bar (1-based) and beat-in-bar (1-based) of a *full* beat index
        (pre-roll beats included). */
    std::pair<int, int> barAndBeat (int fullIndex) const;
};

//==============================================================================
/** The editable result: beat times + meter. */
struct BeatGrid
{
    std::vector<double> beats;   ///< sorted, seconds
    int beatsPerBar = 4;
    int denominator = 4;
    int downbeatOffset = 0;      ///< index (mod beatsPerBar) of the first downbeat

    bool isEmpty() const noexcept               { return beats.size() < 2; }
    bool isDownbeat (int beatIndex) const noexcept;
    int  firstDownbeatIndex() const noexcept;

    /** BPM (in quarter-note terms of the given denominator) of the interval starting at beat i. */
    double bpmOfInterval (int i) const noexcept;
    double bpmAtTime (double t) const noexcept;
    double averageBpm() const noexcept;

    /** Index of the last beat <= t, or -1. */
    int beatIndexAtOrBefore (double t) const noexcept;
    /** Closest beat within tolerance seconds, or -1. */
    int nearestBeat (double t, double toleranceSeconds) const noexcept;

    GridLayout computeLayout() const;
    std::pair<int, int> barAndBeatOfDetected (int beatIndex, const GridLayout& layout) const;

    /** Bar/beat position (bar, beat, fraction 0..1 within the beat) at time t. */
    struct Position { int bar = 0, beat = 0; double fraction = 0.0; bool valid = false; };
    Position positionAt (double t, const GridLayout& layout) const;

    bool operator== (const BeatGrid& o) const
    {
        return beats == o.beats && beatsPerBar == o.beatsPerBar
            && denominator == o.denominator && downbeatOffset == o.downbeatOffset;
    }
};

//==============================================================================
namespace ops
{
    /** Full post-processing pipeline from raw tracker output to final beats. */
    std::vector<double> process (const AnalysisResult& analysis, const ProcessingOptions& options);

    std::vector<double> doubleTempo (const std::vector<double>& beats);
    std::vector<double> halveTempo  (const std::vector<double>& beats, const AnalysisResult* analysis);

    std::vector<double> fixOutliers (const std::vector<double>& beats, const AnalysisResult* analysis);
    std::vector<double> snapToTransients (const std::vector<double>& beats, const AnalysisResult& analysis);
    std::vector<double> smooth (const std::vector<double>& beats, int halfWindow);
    std::vector<double> fitConstantTempo (const std::vector<double>& beats);

    /** Guess which beat phase is the bar start, using low-frequency energy (kick drums). */
    int guessDownbeatOffset (const std::vector<double>& beats, int beatsPerBar, const AnalysisResult& analysis);

    double medianInterval (const std::vector<double>& beats);

    /** Mean onset strength of the weaker alternate-beat parity divided by the
        stronger one. Low values (< ~0.5) suggest the tracker locked onto
        eighth notes and the musical beat is half as fast. */
    double alternateBeatStrengthRatio (const std::vector<double>& beats, const AnalysisResult& analysis);
}

} // namespace beatit
