#include "Retro68.r"
#include "Types.r"

type 'ADgm' {
    RETRO68_CODE_TYPE
};

resource 'ADgm' (0, "Cinema", locked) {
    dontBreakAtEntry, $$read("Cinema.flt");
};

resource 'STR ' (128, "Credit Line") {
    "Cinema 1.1 - streams the Movies library; next film every 15 minutes"
};

data 'sysz' (0) {
    $"0000 8000"
};
