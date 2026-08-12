#include "Retro68.r"
#include "Types.r"

type 'ADgm' {
    RETRO68_CODE_TYPE
};

resource 'ADgm' (0, "Claude Show", locked) {
    dontBreakAtEntry, $$read("Show.flt");
};

resource 'STR ' (128, "Credit Line") {
    "Claude Show 1.0 - ants, then stars, then plasma; a new act every 15 minutes"
};

data 'sysz' (0) {
    $"0000 4000"
};
