#include <KernelExport.h>
#include <PCI.h>
#include <malloc.h>
#include <stdio.h>
#include <string.h>
#include <graphic_driver.h>
#include <VBoxGuest-haiku.h>
#include <VBox/VBoxVideoGuest.h>
#include "../common/VBoxVideo_common.h"

#define VENDOR_ID 0x80ee
#define DEVICE_ID 0xbeef

#define DEVICE_FORMAT "vd_%04X_%04X_%02X%02X%02X"

#define ROUND_TO_PAGE_SIZE(x) (((x) + (B_PAGE_SIZE) - 1) & ~((B_PAGE_SIZE) - 1))

#define ENABLE_DEBUG_TRACE

#undef TRACE
#ifdef ENABLE_DEBUG_TRACE
#	define TRACE(x...) dprintf("VBoxVideo: " x)
#else
#	define TRACE(x...) ;
#endif

int32 api_version = B_CUR_DRIVER_API_VERSION; // revision of driver API we support

struct Benaphore {
	sem_id	sem;
	int32	count;

	status_t Init(const char* name)
	{
		count = 0;
		sem = create_sem(0, name);
		return sem < 0 ? sem : B_OK;
	}

	status_t Acquire()
	{
		if (atomic_add(&count, 1) > 0)
			return acquire_sem(sem);
		return B_OK;
	}

	status_t Release()
	{
		if (atomic_add(&count, -1) > 1)
			return release_sem(sem);
		return B_OK;
	}

	void Delete() { delete_sem(sem); }
};

struct DeviceInfo {
	uint32			openCount;		// count of how many times device has been opened
	uint32			flags;
	area_id 		sharedArea;		// area shared between driver and all accelerants
	SharedInfo*		sharedInfo;		// pointer to shared info area memory
	pci_info		pciInfo;		// copy of pci info for this device
	char			name[B_OS_NAME_LENGTH]; // name of device
};

// at most one virtual video card ever appears, no reason for this to be an array
static DeviceInfo gDeviceInfo;
static char* gDeviceNames[2] = {gDeviceInfo.name, NULL};
static bool gCanHasDevice = false; // is the device present?
static Benaphore gLock;
static pci_module_info*	gPCI;

status_t device_open(const char* name, uint32 flags, void** cookie);
status_t device_close(void* dev);
status_t device_free(void* dev);
status_t device_read(void* dev, off_t pos, void* buf, size_t* len);
status_t device_write(void* dev, off_t pos, const void* buf, size_t* len);
status_t device_ioctl(void* dev, uint32 msg, void* buf, size_t len);
static uint32 get_color_space_for_depth(uint32 depth);

static device_hooks gDeviceHooks = {
	device_open, // open
	device_close, // close
	device_free, // free
	device_ioctl, // control
	device_read, // read
	device_write, // write
	NULL, // select
	NULL, // deselect
	NULL, // read_pages
	NULL  // write_pages
};

status_t init_hardware()
{
	TRACE("init_hardware\n");
	
	if (get_module(VBOXGUEST_MODULE_NAME, (module_info **)&g_VBoxGuest) != B_OK) {
		dprintf("get_module(%s) failed\n", VBOXGUEST_MODULE_NAME);
		return B_ERROR;
	}
	
	if (get_module(B_PCI_MODULE_NAME, (module_info **)&gPCI) != B_OK) {
		dprintf("get_module(%s) failed\n", B_PCI_MODULE_NAME);
		return B_ERROR;
	}
	
	return B_OK;
}

status_t init_driver()
{
	TRACE("init_driver\n");
	
	gLock.Init("VBoxVideo driver lock");
	
	uint32 pciIndex = 0;
	
	while (gPCI->get_nth_pci_info(pciIndex, &gDeviceInfo.pciInfo) == B_OK) {
		if (gDeviceInfo.pciInfo.vendor_id == VENDOR_ID && gDeviceInfo.pciInfo.device_id == DEVICE_ID) {
			sprintf(gDeviceInfo.name, "graphics/" DEVICE_FORMAT,
				gDeviceInfo.pciInfo.vendor_id, gDeviceInfo.pciInfo.device_id,
				gDeviceInfo.pciInfo.bus, gDeviceInfo.pciInfo.device, gDeviceInfo.pciInfo.function);
			TRACE("found device %s\n", gDeviceInfo.name);
			
			gCanHasDevice = true;
			gDeviceInfo.openCount = 0;
			
			size_t sharedSize = (sizeof(SharedInfo) + 7) & ~7;
			gDeviceInfo.sharedArea = create_area("vboxvideo shared info",
				(void**)&gDeviceInfo.sharedInfo, B_ANY_KERNEL_ADDRESS,
				ROUND_TO_PAGE_SIZE(sharedSize), B_FULL_LOCK,
				B_KERNEL_READ_AREA | B_KERNEL_WRITE_AREA | B_USER_CLONEABLE_AREA);

			uint16_t width, height, vwidth, bpp, flags;
			VBoxVideoGetModeRegisters(&width, &height, &vwidth, &bpp, &flags);
			
			gDeviceInfo.sharedInfo->currentMode.space = get_color_space_for_depth(bpp);
			gDeviceInfo.sharedInfo->currentMode.virtual_width = width;
			gDeviceInfo.sharedInfo->currentMode.virtual_height = height;
			gDeviceInfo.sharedInfo->currentMode.h_display_start = 0;
			gDeviceInfo.sharedInfo->currentMode.v_display_start = 0;
			gDeviceInfo.sharedInfo->currentMode.flags = 0;
			gDeviceInfo.sharedInfo->currentMode.timing.h_display = width;
			gDeviceInfo.sharedInfo->currentMode.timing.v_display = height;
			// not used, but this makes a reasonable-sounding refresh rate show in screen prefs:
			gDeviceInfo.sharedInfo->currentMode.timing.h_total = 1000;
			gDeviceInfo.sharedInfo->currentMode.timing.v_total = 1;
			gDeviceInfo.sharedInfo->currentMode.timing.pixel_clock = 850;
			
			// map the PCI memory space
			uint32 command_reg = gPCI->read_pci_config(gDeviceInfo.pciInfo.bus,
				gDeviceInfo.pciInfo.device, gDeviceInfo.pciInfo.function,  PCI_command, 2);
			command_reg |= PCI_command_io | PCI_command_memory | PCI_command_master;
			gPCI->write_pci_config(gDeviceInfo.pciInfo.bus, gDeviceInfo.pciInfo.device,
				gDeviceInfo.pciInfo.function, PCI_command, 2, command_reg);
			
			gDeviceInfo.sharedInfo->framebufferArea =
				map_physical_memory("vboxvideo framebuffer", (phys_addr_t)gDeviceInfo.pciInfo.u.h0.base_registers[0],
					gDeviceInfo.pciInfo.u.h0.base_register_sizes[0], B_ANY_KERNEL_BLOCK_ADDRESS | B_MTR_WC,
					B_READ_AREA + B_WRITE_AREA, &(gDeviceInfo.sharedInfo->framebuffer));
			
			break;
		}
		
		pciIndex++;
	}
	
	return B_OK;
}

const char** publish_devices()
{
	TRACE("publish_devices\n");
	if (gCanHasDevice) {
		return (const char**)gDeviceNames;
	}
	else {
		return NULL;
	}
}

device_hooks* find_device(const char* name)
{
	TRACE("find_device\n");
	if (gCanHasDevice && strcmp(name, gDeviceInfo.name) == 0) {
		return &gDeviceHooks;
	}
	else {
		return NULL;
	}
}

void uninit_driver()
{
	TRACE("uninit_driver\n");
	gLock.Delete();
	put_module(VBOXGUEST_MODULE_NAME);
}

status_t device_open(const char* name, uint32 flags, void** cookie)
{
	TRACE("device_open\n");
	
	if (!gCanHasDevice || strcmp(name, gDeviceInfo.name) != 0)
		return B_BAD_VALUE;
	
	// TODO init device!
	
	*cookie = (void*)&gDeviceInfo;
	
	return B_OK;
}

status_t device_close(void* dev)
{
	TRACE("device_close\n");
	return B_ERROR;
}

status_t device_free(void* dev)
{
	TRACE("device_free\n");
	
	DeviceInfo& di = *(DeviceInfo*)dev;
	gLock.Acquire();
	
	if (di.openCount <= 1) {
		// TODO deinit device!
	
		delete_area(di.sharedArea);
		di.sharedArea = -1;
		di.sharedInfo = NULL;
	}
	
	if (di.openCount > 0)
		di.openCount--;
	
	gLock.Release();
	
	return B_OK;
}

status_t device_read(void* dev, off_t pos, void* buf, size_t* len)
{
	TRACE("device_read\n");
	return B_NOT_ALLOWED;
}

status_t device_write(void* dev, off_t pos, const void* buf, size_t* len)
{
	TRACE("device_write\n");
	return B_NOT_ALLOWED;
}

status_t device_ioctl(void* cookie, uint32 msg, void* buf, size_t len)
{
	TRACE("device_ioctl\n");
	
	DeviceInfo* dev = (DeviceInfo*)cookie;
	
	switch (msg) {
	case B_GET_ACCELERANT_SIGNATURE:
		strcpy((char*)buf, "vboxvideo.accelerant");
		return B_OK;
	
	case VBOXVIDEO_GET_PRIVATE_DATA:
		return user_memcpy(buf, &dev->sharedArea, sizeof(area_id));
		
	case VBOXVIDEO_GET_DEVICE_NAME:
		if (user_strlcpy((char*)buf, gDeviceInfo.name, len) < B_OK)
			return B_BAD_ADDRESS;
		else
			return B_OK;
	
	case VBOXVIDEO_SET_DISPLAY_MODE: {
		display_mode* mode = (display_mode*)buf;
		VBoxVideoSetModeRegisters(mode->timing.h_display, mode->timing.v_display,
			mode->timing.h_display, get_depth_for_color_space(mode->space), 0, 0, 0);
		gDeviceInfo.sharedInfo->currentMode = *mode;
		return B_OK;
	}
	default:
		return B_BAD_VALUE;
	}
	
}
