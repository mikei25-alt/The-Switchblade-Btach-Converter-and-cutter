#pragma once

#include "Analysis/AudioFile.h"
#include "Analysis/TransientDetector.h"

#include <juce_core/juce_core.h>
#include <juce_events/juce_events.h>
#include <juce_audio_formats/juce_audio_formats.h>

#include <atomic>
#include <filesystem>
#include <functional>
#include <memory>
#include <optional>
#include <unordered_map>
#include <vector>

namespace switchblade::analysis
{
    // Slice Count Ceiling — maximum number of slices processed per file
    // Increased from 4 to 50 to prevent premature processing stops
    inline constexpr std::size_t kMaxSlicesPerFile = 50;

    //==========================================================================
    //  AnalysisEngine
    //
    //  Façade over the DSP pipeline with an asynchronous dispatcher. Queues
    //  files onto a background ThreadPool, returns results to the UI thread
    //  via std::function callbacks. Non-blocking by design — the UI never
    //  waits on disk I/O or FFTs.
    //
    //  Thread Safety Model:
    //  -------------------
    //  - Background jobs run on ThreadPool; UI runs on MessageManager thread
    //  - Job dispatch uses MessageManager::callAsync to marshal results to UI
    //  - callbackLock_ guards the std::function callbacks (onStarted_, etc.)
    //  - jobMapLock_ guards the activeJobs_ map (short-lived reads/writes)
    //  - pendingJobCount_ is atomic with memory_order_acq_rel for all-complete
    //  - TransientList is safely transferred via std::move in the async lambda
    //    capture — no shared mutable state between threads after dispatch
    //
    //  Usage:
    //      AnalysisEngine engine { 4 };          // 4 worker threads
    //      engine.setOnComplete ([] (AnalysisResult r) { … });
    //      engine.enqueue (path, AnalysisMode::Auto);
    //==========================================================================
    enum class AnalysisMode
    {
        Auto,         // classifier picks (YAMNet hookup lands in Phase B+)
        Percussive,   // force Transient Detector
        Melodic,      // force Transient Detector + pitch estimation
        Texture       // force Spectral Stability / RMS scanner
    };

    enum class SourceClass
    {
        Unknown,
        Percussive,
        Melodic,
        Texture
    };

    struct AnalysisResult
    {
        int                     jobId               { 0 };    // matches enqueue() return
        std::filesystem::path   path;
        double                  sampleRate          { 0.0 };
        std::int64_t            lengthInSamples     { 0 };
        AnalysisMode            requestedMode       { AnalysisMode::Auto };
        SourceClass             classification      { SourceClass::Unknown };
        std::vector<Transient>  transients;
        std::optional<float>    pitchHz;            // set when Melodic mode runs YIN
        std::optional<float>    pitchClarity;       // 0..1 — confidence in pitch estimate
        juce::String            errorMessage;       // empty on success

        [[nodiscard]] bool ok() const noexcept { return errorMessage.isEmpty(); }

        /** Serialize to JSON suitable for tooling / CLI output. */
        [[nodiscard]] juce::String toJson (int indent = 2) const;
    };

    class AnalysisEngine
    {
    public:
        using CompletionCallback  = std::function<void (AnalysisResult)>;
        /** Fired on the message thread the moment a job starts (before DSP).
            Carry the jobId so the UI can show a "ANALYZING…" state on the
            matching pending card. */
        using StartedCallback     = std::function<void (int jobId)>;
        /** Fired on the message thread once ALL currently-queued jobs have
            finished (or been cancelled).  Not fired if the queue was empty
            when the last enqueue() was called. */
        using AllCompleteCallback = std::function<void()>;

        explicit AnalysisEngine (int numThreads = 4);
        ~AnalysisEngine();

        AnalysisEngine (const AnalysisEngine&) = delete;
        AnalysisEngine& operator= (const AnalysisEngine&) = delete;

        /** Set detector parameters for subsequently-enqueued files. */
        void setDetectorParams (TransientDetector::Params p);

        /** Called on the message thread when a job begins (before DSP runs). */
        void setOnStarted  (StartedCallback cb);

        /** Called on the message thread with each completed result. */
        void setOnComplete (CompletionCallback cb);

        /** Called on the message thread when every queued job has finished.
            Safe to call from the constructor before any enqueue(). */
        void setOnAllComplete (AllCompleteCallback cb);

        /** Queue a file for analysis. Returns a job id for cancellation. */
        int enqueue (std::filesystem::path path,
                     AnalysisMode mode = AnalysisMode::Auto);

        /** Cancel a pending job (running jobs continue). */
        void cancel (int jobId);

        /** Block until all currently queued jobs finish. Use only in CLI
            contexts or shutdown — never on the UI thread. */
        void waitForAll();

        /** Synchronous analysis — useful for CLI. Runs on calling thread. */
        [[nodiscard]] static AnalysisResult analyzeSync (
            const std::filesystem::path& path,
            AnalysisMode mode,
            const TransientDetector::Params& params);

    private:
        class Job;

        juce::AudioFormatManager        formatManager_;
        juce::ThreadPool                pool_;
        TransientDetector::Params       detectorParams_;
        StartedCallback                 onStarted_;
        CompletionCallback              onComplete_;
        AllCompleteCallback             onAllComplete_;
        std::atomic<int>                nextJobId_       { 1 };
        std::atomic<int>                pendingJobCount_ { 0 }; // in-flight job counter
        juce::CriticalSection           callbackLock_;   // guards onStarted_ / onComplete_ / onAllComplete_
        juce::CriticalSection           jobMapLock_;
        std::unordered_map<int, Job*>   activeJobs_;

        friend class Job;
    };
} // namespace switchblade::analysis
