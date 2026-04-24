#pragma once

#include "UI/ResultTile.h"

#include <juce_gui_basics/juce_gui_basics.h>
#include <juce_audio_utils/juce_audio_utils.h>

#include <deque>
#include <functional>
#include <memory>
#include <vector>

namespace switchblade::ui
{
    //==========================================================================
    //  ResultsVault
    //
    //  Jukebox-style expanding 4-column grid of ResultTiles. Tiles arrive with
    //  a 50 ms stagger (Timer drains a pending queue one tile per tick) so the
    //  vault fills in real-time as analysis results land.
    //
    //  Designed to be the viewed content of a juce::Viewport: it calls
    //  setSize() on itself as tiles are added.  Call setViewportWidth() each
    //  time the viewport is resized so tileW can be recalculated.
    //
    //  Usage (MainContainer):
    //      resultsVault_ = std::make_unique<ResultsVault>(fmt, cache);
    //      resultsViewport_.setViewedComponent(resultsVault_.get(), false);
    //      resultsVault_->onTilePlay     = …;
    //      resultsVault_->onTileSelected = …;
    //      // On analysis complete:
    //      resultsVault_->addSlices(file, transients, classification);
    //==========================================================================
    class ResultsVault final : public juce::Component,
                               private juce::Timer
    {
    public:
        using AudioFilePtr = std::shared_ptr<const switchblade::analysis::AudioFile>;

        ResultsVault (juce::AudioFormatManager& fmt,
                      juce::AudioThumbnailCache& cache);
        ~ResultsVault() override;

        ResultsVault (const ResultsVault&) = delete;
        ResultsVault& operator= (const ResultsVault&) = delete;

        /** Queue one tile per transient from a completed analysis result.
            Tiles land staggered at kStaggerMs intervals. */
        void addSlices (AudioFilePtr file,
                        const std::vector<switchblade::analysis::Transient>& transients,
                        switchblade::analysis::SourceClass classification);

        /** Remove all landed and pending tiles; reset the counter. */
        void clear();

        /** Call once the engine signals all-complete.  Stamps a "N SAMPLES READY"
            badge and slides an "EXPORT COLLECTION" bar in from the bottom. */
        void triggerCompletionCeremony();

        /** Recalculate tile width; call from the owner's resized(). */
        void setViewportWidth (int w);

        [[nodiscard]] int tileCount()   const noexcept { return static_cast<int> (tiles_.size()); }
        [[nodiscard]] int pendingCount() const noexcept { return static_cast<int> (pending_.size()); }

        std::function<void (AudioFilePtr, juce::int64, juce::int64)> onTilePlay;
        std::function<void (AudioFilePtr, juce::int64, juce::int64)> onTileSelected;
        /** Fired when the user clicks "EXPORT COLLECTION" in the ceremony bar. */
        std::function<void()> onExportCollection;

        //----- Component -------------------------------------------------------
        void paint     (juce::Graphics&) override;
        void resized   () override;
        void mouseDown (const juce::MouseEvent&) override;

    private:
        static constexpr int kCols        = 4;
        static constexpr int kGap         = 6;
        static constexpr int kBadgeH      = 30;
        static constexpr int kStaggerMs   = 50;
        static constexpr int kAnimMs      = 16;   // ~60 fps ceremony animation
        static constexpr int kExportBarH  = 56;   // height of the ceremony export bar

        struct PendingSlice
        {
            AudioFilePtr file;
            juce::int64  start;
            juce::int64  end;
            switchblade::analysis::SourceClass classification;
            int          index;
        };

        void timerCallback() override;
        void relayout();
        void dropOneTile();
        [[nodiscard]] int tileW() const noexcept;
        [[nodiscard]] int computeHeight() const noexcept;

        juce::AudioFormatManager&               fmt_;
        juce::AudioThumbnailCache&              cache_;

        std::deque<PendingSlice>                pending_;
        std::vector<std::unique_ptr<ResultTile>> tiles_;

        int   viewW_          { 300 };
        int   nextTileIndex_  { 1 };   // global running index for display labels
        bool  allDone_        { false };
        float ceremonyPhase_  { 0.0f }; // 0 = bar off-screen, 1 = fully landed

        JUCE_LEAK_DETECTOR (ResultsVault)
    };
} // namespace switchblade::ui
