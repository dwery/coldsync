#include <sys/types.h>
#include <stdlib.h>
#include <unistd.h>
#include "usb_generic.h"

const char *HS_usb_functions[] = {
	"Generic",
	"Debugger",
	"Hotsync",
	"Console",
	"RemoteFileSys",
	NULL
};
