#include "ImageManager.h"
#include <linux/hyplet.h>

void im_init(PIMAGE_MANAGER manager,
	           void *driver_context,
	           PMemoryLayout memory_layout) {
	manager->gid = 0;
	manager->first_active_image = NULL;
	manager->driver_context = driver_context;
	manager->memory_layout = memory_layout;
}

void im_free_image(PIMAGE_FILE image)
{
	tp_free(image);
}

BOOLEAN im_is_empty(PIMAGE_MANAGER manager)
{
	return !manager->first_active_image;
}

void im_add_image(PIMAGE_MANAGER manager, UINT64 pid,  PIMAGE_FILE img)
{
	img->pid = pid;
	manager->first_active_image =  img;

}

BOOLEAN im_is_process_exists(PIMAGE_MANAGER manager, size_t pid)
{
	if (im_is_empty(manager))
		return FALSE;
	return (manager->first_active_image->pid == pid);
}

void im_remove_process(PIMAGE_MANAGER manager, size_t pid)
{
	PIMAGE_FILE img;
	if (!im_is_process_exists(manager,pid))
		return;
	img = manager->first_active_image;
	vfree(img->attest.kaddr_copy);
	vfree(img->hooked.kaddr_copy);
	im_free_image(manager->first_active_image);
	manager->first_active_image = NULL;
}
