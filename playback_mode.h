#pragma once

// Global playback mode state for the application.
// All renderers query this to determine how to display pages.
class PlaybackMode {
public:
    enum class Mode {
        Normal,      // Standard page-by-page navigation
        Performance  // Respects page breaks, repeats, jumps
    };

    static Mode get() { return mode_; }
    static void set(Mode mode) { mode_ = mode; }
    static bool is_performance() { return mode_ == Mode::Performance; }

private:
    inline static Mode mode_ = Mode::Normal;
};
