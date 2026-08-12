#include "Retro68.r"
#include "Types.r"

type 'ADgm' {
    RETRO68_CODE_TYPE
};

resource 'ADgm' (0, "Ants", locked) {
    dontBreakAtEntry, $$read("Ants.flt");
};

resource 'STR ' (128, "Credit Line") {
    "Ants 1.0 - Mitchell Vizensky & Claude eat your desktop"
};

/* system-heap reservation (bytes) so After Dark can host our State */
data 'sysz' (0) {
    $"0000 4000"
};
