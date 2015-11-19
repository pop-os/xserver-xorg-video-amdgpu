/*
 * Copyright © 2011 Intel Corporation.
 *             2012 Advanced Micro Devices, Inc.
 *
 * Permission is hereby granted, free of charge, to any person
 * obtaining a copy of this software and associated documentation
 * files (the "Software"), to deal in the Software without
 * restriction, including without limitation the rights to use, copy,
 * modify, merge, publish, distribute, sublicense, and/or sell copies
 * of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice (including
 * the next paragraph) shall be included in all copies or substantial
 * portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
 * EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
 * NONINFRINGEMENT.  IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT
 * HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY,
 * WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
 * DEALINGS IN THE SOFTWARE.
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <xf86.h>

#include "amdgpu_bo_helper.h"
#include "amdgpu_pixmap.h"
#include "amdgpu_glamor.h"

#include <gbm.h>

#include <GL/gl.h>

#if HAS_DEVPRIVATEKEYREC
DevPrivateKeyRec amdgpu_pixmap_index;
#else
int amdgpu_pixmap_index;
#endif

void amdgpu_glamor_exchange_buffers(PixmapPtr src, PixmapPtr dst)
{
	AMDGPUInfoPtr info = AMDGPUPTR(xf86ScreenToScrn(dst->drawable.pScreen));

	if (!info->use_glamor)
		return;
	glamor_egl_exchange_buffers(src, dst);
}

Bool amdgpu_glamor_create_screen_resources(ScreenPtr screen)
{
	ScrnInfoPtr scrn = xf86ScreenToScrn(screen);
	AMDGPUInfoPtr info = AMDGPUPTR(scrn);
	uint32_t bo_handle;

	if (!info->use_glamor)
		return TRUE;

#ifdef HAVE_GLAMOR_GLYPHS_INIT
	if (!glamor_glyphs_init(screen))
		return FALSE;
#endif

	if (!amdgpu_bo_get_handle(info->front_buffer, &bo_handle) ||
	    !glamor_egl_create_textured_screen_ext(screen,
						   bo_handle,
						   scrn->displayWidth *
						   info->pixel_bytes, NULL)) {
		return FALSE;
	}

	return TRUE;
}

Bool amdgpu_glamor_pre_init(ScrnInfoPtr scrn)
{
	AMDGPUInfoPtr info = AMDGPUPTR(scrn);
	pointer glamor_module;
	CARD32 version;

	if (!info->dri2.available)
		return FALSE;

	if (scrn->depth < 24) {
		xf86DrvMsg(scrn->scrnIndex, X_ERROR,
			   "glamor requires depth >= 24, disabling.\n");
		return FALSE;
	}
#if XORG_VERSION_CURRENT < XORG_VERSION_NUMERIC(1,15,0,0,0)
	if (!xf86LoaderCheckSymbol("glamor_egl_init")) {
		xf86DrvMsg(scrn->scrnIndex, X_ERROR,
			   "glamor requires Load \"glamoregl\" in "
			   "Section \"Module\", disabling.\n");
		return FALSE;
	}
#endif

	/* Load glamor module */
	if ((glamor_module = xf86LoadSubModule(scrn, GLAMOR_EGL_MODULE_NAME))) {
		version = xf86GetModuleVersion(glamor_module);
		if (version < MODULE_VERSION_NUMERIC(0, 3, 1)) {
			xf86DrvMsg(scrn->scrnIndex, X_ERROR,
				   "Incompatible glamor version, required >= 0.3.0.\n");
			return FALSE;
		} else {
			AMDGPUEntPtr pAMDGPUEnt = AMDGPUEntPriv(scrn);

			if (glamor_egl_init(scrn, pAMDGPUEnt->fd)) {
				xf86DrvMsg(scrn->scrnIndex, X_INFO,
					   "glamor detected, initialising EGL layer.\n");
			} else {
				xf86DrvMsg(scrn->scrnIndex, X_ERROR,
					   "glamor detected, failed to initialize EGL.\n");
				return FALSE;
			}
		}
	} else {
		xf86DrvMsg(scrn->scrnIndex, X_ERROR, "glamor not available\n");
		return FALSE;
	}

	info->use_glamor = TRUE;

	return TRUE;
}

Bool
amdgpu_glamor_create_textured_pixmap(PixmapPtr pixmap, struct amdgpu_pixmap *priv)
{
	ScrnInfoPtr scrn = xf86ScreenToScrn(pixmap->drawable.pScreen);
	AMDGPUInfoPtr info = AMDGPUPTR(scrn);
	uint32_t bo_handle;

	if ((info->use_glamor) == 0)
		return TRUE;

	if (!amdgpu_bo_get_handle(priv->bo, &bo_handle))
		return FALSE;

	return glamor_egl_create_textured_pixmap(pixmap, bo_handle,
						 pixmap->devKind);
}

static PixmapPtr
amdgpu_glamor_create_pixmap(ScreenPtr screen, int w, int h, int depth,
			    unsigned usage)
{
	ScrnInfoPtr scrn = xf86ScreenToScrn(screen);
	AMDGPUInfoPtr info = AMDGPUPTR(scrn);
	struct amdgpu_pixmap *priv;
	PixmapPtr pixmap, new_pixmap = NULL;

	if (!AMDGPU_CREATE_PIXMAP_SHARED(usage)) {
		if (info->shadow_primary) {
			if (usage != CREATE_PIXMAP_USAGE_BACKING_PIXMAP)
				return fbCreatePixmap(screen, w, h, depth, usage);

			usage |= AMDGPU_CREATE_PIXMAP_LINEAR |
				 AMDGPU_CREATE_PIXMAP_GTT;
		} else {
			pixmap = glamor_create_pixmap(screen, w, h, depth, usage);
			if (pixmap)
				return pixmap;
		}
	}

	if (w > 32767 || h > 32767)
		return NullPixmap;

	if (depth == 1)
		return fbCreatePixmap(screen, w, h, depth, usage);

	if (usage == CREATE_PIXMAP_USAGE_GLYPH_PICTURE && w <= 32 && h <= 32)
		return fbCreatePixmap(screen, w, h, depth, usage);

	pixmap = fbCreatePixmap(screen, 0, 0, depth, usage);
	if (pixmap == NullPixmap)
		return pixmap;

	if (w && h) {
		int stride;

		priv = calloc(1, sizeof(struct amdgpu_pixmap));
		if (priv == NULL)
			goto fallback_pixmap;

		priv->bo = amdgpu_alloc_pixmap_bo(scrn, w, h, depth, usage,
						  pixmap->drawable.bitsPerPixel,
						  &stride);
		if (!priv->bo)
			goto fallback_priv;

		amdgpu_set_pixmap_private(pixmap, priv);

		screen->ModifyPixmapHeader(pixmap, w, h, 0, 0, stride, NULL);

		pixmap->devPrivate.ptr = NULL;

		if (!amdgpu_glamor_create_textured_pixmap(pixmap, priv))
			goto fallback_glamor;
	}

	return pixmap;

fallback_glamor:
	if (AMDGPU_CREATE_PIXMAP_SHARED(usage)) {
		/* XXX need further work to handle the DRI2 failure case.
		 * Glamor don't know how to handle a BO only pixmap. Put
		 * a warning indicator here.
		 */
		xf86DrvMsg(scrn->scrnIndex, X_WARNING,
			   "Failed to create textured DRI2/PRIME pixmap.");
		return pixmap;
	}
	/* Create textured pixmap failed means glamor failed to
	 * create a texture from current BO for some reasons. We turn
	 * to create a new glamor pixmap and clean up current one.
	 * One thing need to be noted, this new pixmap doesn't
	 * has a priv and bo attached to it. It's glamor's responsbility
	 * to take care of it. Glamor will mark this new pixmap as a
	 * texture only pixmap and will never fallback to DDX layer
	 * afterwards.
	 */
	new_pixmap = glamor_create_pixmap(screen, w, h, depth, usage);
	amdgpu_bo_unref(&priv->bo);
fallback_priv:
	free(priv);
fallback_pixmap:
	fbDestroyPixmap(pixmap);
	if (new_pixmap)
		return new_pixmap;
	else
		return fbCreatePixmap(screen, w, h, depth, usage);
}

static Bool amdgpu_glamor_destroy_pixmap(PixmapPtr pixmap)
{
	if (pixmap->refcnt == 1) {
		if (pixmap->devPrivate.ptr) {
			struct amdgpu_buffer *bo = amdgpu_get_pixmap_bo(pixmap);

			if (bo)
				amdgpu_bo_unmap(bo);
		}

		glamor_egl_destroy_textured_pixmap(pixmap);
		amdgpu_set_pixmap_bo(pixmap, NULL);
	}
	fbDestroyPixmap(pixmap);
	return TRUE;
}

#ifdef AMDGPU_PIXMAP_SHARING

static Bool
amdgpu_glamor_share_pixmap_backing(PixmapPtr pixmap, ScreenPtr slave,
				   void **handle_p)
{
	struct amdgpu_pixmap *priv = amdgpu_get_pixmap_private(pixmap);

	if (!priv)
		return FALSE;

	return amdgpu_share_pixmap_backing(priv->bo, handle_p);
}

static Bool
amdgpu_glamor_set_shared_pixmap_backing(PixmapPtr pixmap, void *handle)
{
	ScreenPtr screen = pixmap->drawable.pScreen;
	ScrnInfoPtr scrn = xf86ScreenToScrn(screen);
	struct amdgpu_pixmap *priv;

	if (!amdgpu_set_shared_pixmap_backing(pixmap, handle))
		return FALSE;

	priv = amdgpu_get_pixmap_private(pixmap);

	if (!amdgpu_glamor_create_textured_pixmap(pixmap, priv)) {
		xf86DrvMsg(scrn->scrnIndex, X_ERROR,
			   "Failed to get PRIME drawable for glamor pixmap.\n");
		return FALSE;
	}

	screen->ModifyPixmapHeader(pixmap,
				   pixmap->drawable.width,
				   pixmap->drawable.height,
				   0, 0, 0, NULL);

	return TRUE;
}

#endif /* AMDGPU_PIXMAP_SHARING */

Bool amdgpu_glamor_init(ScreenPtr screen)
{
	ScrnInfoPtr scrn = xf86ScreenToScrn(screen);
	AMDGPUInfoPtr info = AMDGPUPTR(scrn);
#ifdef RENDER
#ifdef HAVE_FBGLYPHS
	UnrealizeGlyphProcPtr SavedUnrealizeGlyph = NULL;
#endif
	PictureScreenPtr ps = NULL;

	if (info->shadow_primary) {
		ps = GetPictureScreenIfSet(screen);

		if (ps) {
#ifdef HAVE_FBGLYPHS
			SavedUnrealizeGlyph = ps->UnrealizeGlyph;
#endif
			info->glamor.SavedGlyphs = ps->Glyphs;
			info->glamor.SavedTriangles = ps->Triangles;
			info->glamor.SavedTrapezoids = ps->Trapezoids;
		}
	}
#endif /* RENDER */

	if (!glamor_init(screen, GLAMOR_USE_EGL_SCREEN | GLAMOR_USE_SCREEN |
			 GLAMOR_USE_PICTURE_SCREEN | GLAMOR_INVERTED_Y_AXIS |
			 GLAMOR_NO_DRI3)) {
		xf86DrvMsg(scrn->scrnIndex, X_ERROR,
			   "Failed to initialize glamor.\n");
		return FALSE;
	}

	if (!glamor_egl_init_textured_pixmap(screen)) {
		xf86DrvMsg(scrn->scrnIndex, X_ERROR,
			   "Failed to initialize textured pixmap of screen for glamor.\n");
		return FALSE;
	}
#if HAS_DIXREGISTERPRIVATEKEY
	if (!dixRegisterPrivateKey(&amdgpu_pixmap_index, PRIVATE_PIXMAP, 0))
#else
	if (!dixRequestPrivate(&amdgpu_pixmap_index, 0))
#endif
		return FALSE;

	if (info->shadow_primary)
		amdgpu_glamor_screen_init(screen);

#if defined(RENDER) && defined(HAVE_FBGLYPHS)
	/* For ShadowPrimary, we need fbUnrealizeGlyph instead of
	 * glamor_unrealize_glyph
	 */
	if (ps)
		ps->UnrealizeGlyph = SavedUnrealizeGlyph;
#endif

	screen->CreatePixmap = amdgpu_glamor_create_pixmap;
	screen->DestroyPixmap = amdgpu_glamor_destroy_pixmap;
#ifdef AMDGPU_PIXMAP_SHARING
	screen->SharePixmapBacking = amdgpu_glamor_share_pixmap_backing;
	screen->SetSharedPixmapBacking =
	    amdgpu_glamor_set_shared_pixmap_backing;
#endif

	xf86DrvMsg(scrn->scrnIndex, X_INFO, "Use GLAMOR acceleration.\n");
	return TRUE;
}

void amdgpu_glamor_flush(ScrnInfoPtr pScrn)
{
	AMDGPUInfoPtr info = AMDGPUPTR(pScrn);

	if (info->use_glamor) {
		glamor_block_handler(pScrn->pScreen);
		info->gpu_flushed++;
	}
}

void amdgpu_glamor_finish(ScrnInfoPtr pScrn)
{
	AMDGPUInfoPtr info = AMDGPUPTR(pScrn);

	if (info->use_glamor) {
		amdgpu_glamor_flush(pScrn);
		glFinish();
	}
}

XF86VideoAdaptorPtr amdgpu_glamor_xv_init(ScreenPtr pScreen, int num_adapt)
{
	return glamor_xv_init(pScreen, num_adapt);
}
