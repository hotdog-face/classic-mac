#include "Processes.r"
resource 'SIZE' (-1) {
	reserved, acceptSuspendResumeEvents, reserved, canBackground,
	doesActivateOnFGSwitch, backgroundAndForeground, dontGetFrontClicks,
	ignoreChildDiedEvents, is32BitCompatible, notHighLevelEventAware,
	onlyLocalHLEvents, notStationeryAware, dontUseTextEditServices,
	reserved, reserved, reserved, 384*1024, 256*1024
};
