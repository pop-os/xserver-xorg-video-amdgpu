/*
 * Copyright © 2007 Red Hat, Inc.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice (including the next
 * paragraph) shall be included in all copies or substantial portions of the
 * Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
 * THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 *
 * Authors:
 *     Dave Airlie <airlied@redhat.com>
 *
 */
#ifndef DRMMODE_DISPLAY_H
#define DRMMODE_DISPLAY_H

#include "xf86drmMode.h"
#ifdef HAVE_LIBUDEV
#include "libudev.h"
#endif

#include "amdgpu_drm_queue.h"
#include "amdgpu_probe.h"
#include "amdgpu.h"

typedef struct {
	ScrnInfoPtr scrn;
#ifdef HAVE_LIBUDEV
	struct udev_monitor *uevent_monitor;
	InputHandlerProc uevent_handler;
#endif
	drmEventContext event_context;
	int count_crtcs;

	Bool delete_dp_12_displays;

	Bool dri2_flipping;
	Bool present_flipping;
} drmmode_rec, *drmmode_ptr;

typedef struct {
	int flip_count;
	void *event_data;
	unsigned int fe_frame;
	uint64_t fe_usec;
	xf86CrtcPtr fe_crtc;
	amdgpu_drm_handler_proc handler;
	amdgpu_drm_abort_proc abort;
} drmmode_flipdata_rec, *drmmode_flipdata_ptr;

struct drmmode_fb {
	int refcnt;
	uint32_t handle;
};

struct drmmode_scanout {
	struct amdgpu_buffer *bo;
	PixmapPtr pixmap;
	int width, height;
};

typedef struct {
	drmmode_ptr drmmode;
	drmModeCrtcPtr mode_crtc;
	int hw_id;
	struct amdgpu_buffer *cursor_buffer;
	struct drmmode_scanout rotate;
	struct drmmode_scanout scanout[2];
	DamagePtr scanout_damage;
	RegionRec scanout_last_region;
	unsigned scanout_id;
	Bool scanout_update_pending;
	Bool tear_free;

	PixmapPtr prime_scanout_pixmap;

	int dpms_mode;
	CARD64 dpms_last_ust;
	uint32_t dpms_last_seq;
	int dpms_last_fps;
	uint32_t interpolated_vblanks;

	/* Modeset needed for DPMS on */
	Bool need_modeset;
	/* A flip to this FB is pending for this CRTC */
	struct drmmode_fb *flip_pending;
	/* The FB currently being scanned out by this CRTC, if any */
	struct drmmode_fb *fb;
} drmmode_crtc_private_rec, *drmmode_crtc_private_ptr;

typedef struct {
	drmModePropertyPtr mode_prop;
	uint64_t value;
	int num_atoms;		/* if range prop, num_atoms == 1; if enum prop, num_atoms == num_enums + 1 */
	Atom *atoms;
} drmmode_prop_rec, *drmmode_prop_ptr;

typedef struct {
	drmmode_ptr drmmode;
	int output_id;
	drmModeConnectorPtr mode_output;
	drmModeEncoderPtr *mode_encoders;
	drmModePropertyBlobPtr edid_blob;
	int dpms_enum_id;
	int num_props;
	drmmode_prop_ptr props;
	int enc_mask;
	int enc_clone_mask;
	int tear_free;
} drmmode_output_private_rec, *drmmode_output_private_ptr;


enum drmmode_flip_sync {
    FLIP_VSYNC,
    FLIP_ASYNC,
};


/* Can the page flip ioctl be used for this CRTC? */
static inline Bool
drmmode_crtc_can_flip(xf86CrtcPtr crtc)
{
	drmmode_crtc_private_ptr drmmode_crtc = crtc->driver_private;

	return crtc->enabled &&
		drmmode_crtc->dpms_mode == DPMSModeOn &&
		!drmmode_crtc->rotate.bo &&
		!drmmode_crtc->scanout[drmmode_crtc->scanout_id].bo;
}


static inline void
drmmode_fb_reference_loc(int drm_fd, struct drmmode_fb **old, struct drmmode_fb *new,
			 const char *caller, unsigned line)
{
	if (new) {
		if (new->refcnt <= 0) {
			FatalError("New FB's refcnt was %d at %s:%u",
				   new->refcnt, caller, line);
		}

		new->refcnt++;
	}

	if (*old) {
		if ((*old)->refcnt <= 0) {
			FatalError("Old FB's refcnt was %d at %s:%u",
				   (*old)->refcnt, caller, line);
		}

		if (--(*old)->refcnt == 0) {
			drmModeRmFB(drm_fd, (*old)->handle);
			free(*old);
		}
	}

	*old = new;
}

#define drmmode_fb_reference(fd, old, new) \
	drmmode_fb_reference_loc(fd, old, new, __func__, __LINE__)


extern int drmmode_page_flip_target_absolute(AMDGPUEntPtr pAMDGPUEnt,
					     drmmode_crtc_private_ptr drmmode_crtc,
					     int fb_id, uint32_t flags,
					     uintptr_t drm_queue_seq,
					     uint32_t target_msc);
extern int drmmode_page_flip_target_relative(AMDGPUEntPtr pAMDGPUEnt,
					     drmmode_crtc_private_ptr drmmode_crtc,
					     int fb_id, uint32_t flags,
					     uintptr_t drm_queue_seq,
					     uint32_t target_msc);
extern Bool drmmode_pre_init(ScrnInfoPtr pScrn, drmmode_ptr drmmode, int cpp);
extern void drmmode_init(ScrnInfoPtr pScrn, drmmode_ptr drmmode);
extern void drmmode_fini(ScrnInfoPtr pScrn, drmmode_ptr drmmode);
extern void drmmode_set_cursor(ScrnInfoPtr scrn, drmmode_ptr drmmode, int id,
			       struct amdgpu_buffer *bo);
void drmmode_adjust_frame(ScrnInfoPtr pScrn, drmmode_ptr drmmode, int x, int y);
extern Bool drmmode_set_desired_modes(ScrnInfoPtr pScrn, drmmode_ptr drmmode,
				      Bool set_hw);
extern void drmmode_copy_fb(ScrnInfoPtr pScrn, drmmode_ptr drmmode);
extern Bool drmmode_setup_colormap(ScreenPtr pScreen, ScrnInfoPtr pScrn);

extern void drmmode_crtc_scanout_destroy(drmmode_ptr drmmode,
					 struct drmmode_scanout *scanout);
extern void drmmode_scanout_free(ScrnInfoPtr scrn);

extern void drmmode_uevent_init(ScrnInfoPtr scrn, drmmode_ptr drmmode);
extern void drmmode_uevent_fini(ScrnInfoPtr scrn, drmmode_ptr drmmode);

extern int drmmode_get_crtc_id(xf86CrtcPtr crtc);
extern int drmmode_get_pitch_align(ScrnInfoPtr scrn, int bpe);
Bool amdgpu_do_pageflip(ScrnInfoPtr scrn, ClientPtr client,
			PixmapPtr new_front, uint64_t id, void *data,
			xf86CrtcPtr ref_crtc, amdgpu_drm_handler_proc handler,
			amdgpu_drm_abort_proc abort,
			enum drmmode_flip_sync flip_sync,
			uint32_t target_msc);
int drmmode_crtc_get_ust_msc(xf86CrtcPtr crtc, CARD64 *ust, CARD64 *msc);
int drmmode_get_current_ust(int drm_fd, CARD64 * ust);

Bool drmmode_wait_vblank(xf86CrtcPtr crtc, drmVBlankSeqType type,
			 uint32_t target_seq, unsigned long signal,
			 uint64_t *ust, uint32_t *result_seq);


#endif
