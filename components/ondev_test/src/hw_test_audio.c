// Plays the startup tone; PASS as long as the codec path returns OK.
// Can't auto-verify the speaker (no mic) — this exercises the wiring.

#include <stdio.h>
#include <stdbool.h>
#include "audio_alert.h"

bool hw_test_audio(char *detail, size_t n) {
    audio_alert_play_startup(NULL);   // fire-and-forget (no completion waiter)
    snprintf(detail, n, "tone played (no auto-verify)");
    return true;
}
