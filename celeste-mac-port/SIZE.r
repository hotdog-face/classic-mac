/* SIZE.r -- memory partition for the app.  The binary carries ~325KB of
 * SFX plus ~1MB of music loops in its data segment, so the System 7 /
 * MultiFinder default partition is far too small.  Needs a >=2MB Mac; for
 * a 1MB Classic, rebuild without music_data.c (and shrink this). */
#include "Processes.r"

resource 'SIZE' (-1) {
	reserved,
	acceptSuspendResumeEvents,
	reserved,
	canBackground,
	doesActivateOnFGSwitch,
	backgroundAndForeground,
	dontGetFrontClicks,
	ignoreChildDiedEvents,
	is32BitCompatible,
	notHighLevelEventAware,
	onlyLocalHLEvents,
	notStationeryAware,
	dontUseTextEditServices,
	reserved,
	reserved,
	reserved,
	2048 * 1024,
	1792 * 1024
};
