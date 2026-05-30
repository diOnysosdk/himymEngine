#include "rev_sequence.h"
#include <cstring>

namespace rev {
namespace sequence {

// Clamp helper
static float Clamp(float value, float min, float max) {
    if (value < min) return min;
    if (value > max) return max;
    return value;
}

Timeline CreateTimeline(int reserve_cues) {
    Timeline timeline;
    timeline.cues = new Cue[reserve_cues];
    timeline.cue_count = 0;
    timeline.capacity = reserve_cues;
    timeline.current_time = 0.0f;
    return timeline;
}

void DestroyTimeline(Timeline& timeline) {
    if (timeline.cues) {
        delete[] timeline.cues;
        timeline.cues = nullptr;
    }
    timeline.cue_count = 0;
    timeline.capacity = 0;
}

void AddCue(Timeline& timeline, float start, float end, float fade_in, float fade_out, int id) {
    Cue cue;
    cue.start = start;
    cue.end = end;
    cue.fade_in = fade_in;
    cue.fade_out = fade_out;
    cue.id = id;
    cue.opacity = 0.0f;
    AddCue(timeline, cue);
}

void AddCue(Timeline& timeline, const Cue& cue) {
    if (timeline.cue_count >= timeline.capacity) {
        // Grow array
        int new_capacity = timeline.capacity * 2;
        Cue* new_cues = new Cue[new_capacity];
        memcpy(new_cues, timeline.cues, timeline.cue_count * sizeof(Cue));
        delete[] timeline.cues;
        timeline.cues = new_cues;
        timeline.capacity = new_capacity;
    }
    
    timeline.cues[timeline.cue_count++] = cue;
}

void SortCues(Timeline& timeline) {
    // Simple bubble sort
    for (int i = 0; i < timeline.cue_count - 1; ++i) {
        for (int j = 0; j < timeline.cue_count - i - 1; ++j) {
            if (timeline.cues[j].start > timeline.cues[j + 1].start) {
                Cue temp = timeline.cues[j];
                timeline.cues[j] = timeline.cues[j + 1];
                timeline.cues[j + 1] = temp;
            }
        }
    }
}

void Update(Timeline& timeline, float delta_time) {
    timeline.current_time += delta_time;
    
    // Update opacity for all cues
    for (int i = 0; i < timeline.cue_count; ++i) {
        timeline.cues[i].opacity = GetOpacity(timeline.cues[i], timeline.current_time);
    }
}

void SetTime(Timeline& timeline, float time) {
    timeline.current_time = time;
    
    // Update opacity for all cues
    for (int i = 0; i < timeline.cue_count; ++i) {
        timeline.cues[i].opacity = GetOpacity(timeline.cues[i], timeline.current_time);
    }
}

float GetTime(const Timeline& timeline) {
    return timeline.current_time;
}

int GetActiveCues(const Timeline& timeline, Cue** out_cues, int max_cues) {
    int count = 0;
    
    for (int i = 0; i < timeline.cue_count && count < max_cues; ++i) {
        const Cue& cue = timeline.cues[i];
        
        // Check if cue is active (opacity > 0)
        if (cue.opacity > 0.0f) {
            out_cues[count++] = const_cast<Cue*>(&cue);
        }
    }
    
    return count;
}

float GetOpacity(const Cue& cue, float timeline_time) {
    // Before cue starts
    if (timeline_time < cue.start) {
        return 0.0f;
    }
    
    // After cue ends
    if (timeline_time > cue.end) {
        return 0.0f;
    }
    
    float opacity = 1.0f;
    
    // Fade in
    if (cue.fade_in > 0.0f) {
        float fade_in_end = cue.start + cue.fade_in;
        if (timeline_time < fade_in_end) {
            opacity = (timeline_time - cue.start) / cue.fade_in;
        }
    }
    
    // Fade out
    if (cue.fade_out > 0.0f) {
        float fade_out_start = cue.end - cue.fade_out;
        if (timeline_time > fade_out_start) {
            opacity *= (cue.end - timeline_time) / cue.fade_out;
        }
    }
    
    return Clamp(opacity, 0.0f, 1.0f);
}

}  // namespace sequence
}  // namespace rev
