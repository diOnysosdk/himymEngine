#pragma once

namespace rev {
namespace sequence {

// A cue represents a scene block on the timeline
struct Cue {
    float start;       // Start time (seconds)
    float end;         // End time (seconds)
    float fade_in;     // Fade in duration (seconds)
    float fade_out;    // Fade out duration (seconds)
    int id;            // Scene/shader ID or other identifier
    float opacity;     // Current opacity (calculated during playback)
};

// Timeline manages multiple cues
struct Timeline {
    Cue* cues;
    int cue_count;
    int capacity;
    float current_time;
};

// Timeline creation and destruction
Timeline CreateTimeline(int reserve_cues = 64);
void DestroyTimeline(Timeline& timeline);

// Timeline building
void AddCue(Timeline& timeline, float start, float end, float fade_in, float fade_out, int id);
void AddCue(Timeline& timeline, const Cue& cue);
void SortCues(Timeline& timeline);  // Sort by start time

// Playback control
void Update(Timeline& timeline, float delta_time);
void SetTime(Timeline& timeline, float time);
float GetTime(const Timeline& timeline);

// Querying
int GetActiveCues(const Timeline& timeline, Cue** out_cues, int max_cues);
float GetOpacity(const Cue& cue, float timeline_time);

}  // namespace sequence
}  // namespace rev
