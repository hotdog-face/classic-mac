#include "Processes.r"
resource 'SIZE' (-1) {
	reserved, acceptSuspendResumeEvents, reserved, canBackground,
	doesActivateOnFGSwitch, backgroundAndForeground, dontGetFrontClicks,
	ignoreChildDiedEvents, is32BitCompatible, notHighLevelEventAware,
	onlyLocalHLEvents, notStationeryAware, dontUseTextEditServices,
	reserved, reserved, reserved, 2560*1024, 2304*1024
};
