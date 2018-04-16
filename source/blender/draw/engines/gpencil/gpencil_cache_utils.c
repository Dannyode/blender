/*
 * Copyright 2017, Blender Foundation.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software Foundation,
 * Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301, USA.
 *
 * Contributor(s): Antonio Vazquez
 *
 */

/** \file blender/draw/engines/gpencil/gpencil_cache_utils.c
 *  \ingroup draw
 */

#include "DRW_render.h"

#include "BKE_global.h"

#include "ED_gpencil.h"
#include "ED_view3d.h"

#include "DNA_gpencil_types.h"
#include "DNA_view3d_types.h"

#include "gpencil_engine.h"

#include "draw_cache_impl.h"

 /* allocate cache to store GP objects */
tGPencilObjectCache *gpencil_object_cache_allocate(tGPencilObjectCache *cache, int *gp_cache_size, int *gp_cache_used)
{
	tGPencilObjectCache *p = NULL;

	/* By default a cache is created with one block with a predefined number of free slots,
	if the size is not enough, the cache is reallocated adding a new block of free slots.
	This is done in order to keep cache small */
	if (*gp_cache_used + 1 > *gp_cache_size) {
		if ((*gp_cache_size == 0) || (cache == NULL)) {
			p = MEM_callocN(sizeof(struct tGPencilObjectCache) * GP_CACHE_BLOCK_SIZE, "tGPencilObjectCache");
			*gp_cache_size = GP_CACHE_BLOCK_SIZE;
		}
		else {
			*gp_cache_size += GP_CACHE_BLOCK_SIZE;
			p = MEM_recallocN(cache, sizeof(struct tGPencilObjectCache) * *gp_cache_size);
		}
		cache = p;
	}
	return cache;
}

/* add a gpencil object to cache to defer drawing */
void gpencil_object_cache_add(tGPencilObjectCache *cache_array, Object *ob, bool is_temp, int *gp_cache_used)
{
	const DRWContextState *draw_ctx = DRW_context_state_get();
	tGPencilObjectCache *cache = &cache_array[*gp_cache_used];
	RegionView3D *rv3d = draw_ctx->rv3d;
	
	/* zero out all vfx_* pointers */
	memset(cache, 0, sizeof(*cache));
	
	/* save object */
	cache->ob = ob;
	cache->temp_ob = is_temp;
	cache->idx = *gp_cache_used;

	cache->init_grp = 0;
	cache->end_grp = -1;
	
	/* calculate zdepth from point of view */
	float zdepth = 0.0;
	if (rv3d) {
		if (rv3d->is_persp) {
			zdepth = ED_view3d_calc_zfac(rv3d, ob->loc, NULL);
		}
		else {
			zdepth = -dot_v3v3(rv3d->viewinv[2], ob->loc);
		}
	}
	else {
		/* In render mode, rv3d is not available, so use the distance to camera.
		 * The real distance is not important, but the relative distance to the camera plane
		 * in order to sort by z_depth of the objects
		 */
		float vn[3] = { 0.0f, 0.0f, -1.0f }; /* always face down */
		float plane_cam[4];
		struct Object *camera = draw_ctx->scene->camera;
		if (camera) {
			mul_m4_v3(camera->obmat, vn);
			normalize_v3(vn);
			plane_from_point_normal_v3(plane_cam, camera->loc, vn);
			zdepth = dist_squared_to_plane_v3(ob->loc, plane_cam);
		}
	}
	cache->zdepth = zdepth;
	/* increase slots used in cache */
	(*gp_cache_used)++;
}

/* get current cache data */
static GpencilBatchCache *gpencil_batch_get_element(Object *ob)
{
	bGPdata *gpd = ob->data;
	if (gpd->batch_cache_data == NULL) {
		gpd->batch_cache_data = BLI_ghash_str_new("GP batch cache data");
		return NULL;
	}

	return (GpencilBatchCache *) BLI_ghash_lookup(gpd->batch_cache_data, ob->id.name);
}

/* verify if cache is valid */
static bool gpencil_batch_cache_valid(Object *ob, bGPdata *gpd, int cfra)
{
	GpencilBatchCache *cache = gpencil_batch_get_element(ob);

	if (cache == NULL) {
		return false;
	}

	cache->is_editmode = GPENCIL_ANY_EDIT_MODE(gpd);

	if (cfra != cache->cache_frame) {
		return false;
	}

	if (gpd->flag & GP_DATA_CACHE_IS_DIRTY) {
		return false;
	}

	if (cache->is_editmode) {
		return false;
	}

	if (cache->is_dirty) {
		return false;
	}

	return true;
}

/* resize the cache to the number of slots */
static void gpencil_batch_cache_resize(GpencilBatchCache *cache, int slots)
{
	cache->cache_size = slots;
	cache->batch_stroke = MEM_recallocN(cache->batch_stroke, sizeof(struct Gwn_Batch *) * slots);
	cache->batch_fill = MEM_recallocN(cache->batch_fill, sizeof(struct Gwn_Batch *) * slots);
	cache->batch_edit = MEM_recallocN(cache->batch_edit, sizeof(struct Gwn_Batch *) * slots);
	cache->batch_edlin = MEM_recallocN(cache->batch_edlin, sizeof(struct Gwn_Batch *) * slots);
}

/* check size and increase if no free slots */
void gpencil_batch_cache_check_free_slots(Object *ob)
{
	GpencilBatchCache *cache = gpencil_batch_get_element(ob);

	/* the memory is reallocated by chunks, not for one slot only to improve speed */
	if (cache->cache_idx >= cache->cache_size) {
		cache->cache_size += GPENCIL_MIN_BATCH_SLOTS_CHUNK;
		gpencil_batch_cache_resize(cache, cache->cache_size);
	}
}

/* cache init */
static void gpencil_batch_cache_init(Object *ob, int cfra)
{
	GpencilBatchCache *cache = gpencil_batch_get_element(ob);
	bGPdata *gpd = ob->data;

	if (G.debug_value >= 664) {
		printf("gpencil_batch_cache_init: %s\n", ob->id.name);
	}

	if (!cache) {
		cache = MEM_callocN(sizeof(*cache), __func__);
		BLI_ghash_insert(gpd->batch_cache_data, ob->id.name, cache);
	}
	else {
		memset(cache, 0, sizeof(*cache));
	}

	cache->cache_size = GPENCIL_MIN_BATCH_SLOTS_CHUNK;
	cache->batch_stroke = MEM_callocN(sizeof(struct Gwn_Batch *) * cache->cache_size, "Gpencil_Batch_Stroke");
	cache->batch_fill = MEM_callocN(sizeof(struct Gwn_Batch *) * cache->cache_size, "Gpencil_Batch_Fill");
	cache->batch_edit = MEM_callocN(sizeof(struct Gwn_Batch *) * cache->cache_size, "Gpencil_Batch_Edit");
	cache->batch_edlin = MEM_callocN(sizeof(struct Gwn_Batch *) * cache->cache_size, "Gpencil_Batch_Edlin");

	cache->is_editmode = GPENCIL_ANY_EDIT_MODE(gpd);
	gpd->flag &= ~GP_DATA_CACHE_IS_DIRTY;

	cache->cache_idx = 0;
	cache->is_dirty = true;
	cache->cache_frame = cfra;
}

/* clear cache */
static void gpencil_batch_cache_clear(GpencilBatchCache *cache, bGPdata *gpd)
{
	if (!cache) {
		return;
	}

	if (cache->cache_size == 0) {
		return;
	}

	if (G.debug_value >= 664) {
		printf("gpencil_batch_cache_clear: %s\n", gpd->id.name);
	}

	if (cache->cache_size > 0) {
		for (int i = 0; i < cache->cache_size; i++) {
			GWN_BATCH_DISCARD_SAFE(cache->batch_stroke[i]);
			GWN_BATCH_DISCARD_SAFE(cache->batch_fill[i]);
			GWN_BATCH_DISCARD_SAFE(cache->batch_edit[i]);
			GWN_BATCH_DISCARD_SAFE(cache->batch_edlin[i]);
		}
		MEM_SAFE_FREE(cache->batch_stroke);
		MEM_SAFE_FREE(cache->batch_fill);
		MEM_SAFE_FREE(cache->batch_edit);
		MEM_SAFE_FREE(cache->batch_edlin);
	}

	MEM_SAFE_FREE(cache);
}

/* get cache */
GpencilBatchCache *gpencil_batch_cache_get(Object *ob, int cfra)
{
	bGPdata *gpd = ob->data;

	if (!gpencil_batch_cache_valid(ob, gpd, cfra)) {
		if (G.debug_value >= 664) {
			printf("gpencil_batch_cache: %s\n", gpd->id.name);
		}

		GpencilBatchCache *cache = gpencil_batch_get_element(ob);
		if (cache) {
			gpencil_batch_cache_clear(cache, gpd);
			BLI_ghash_remove(gpd->batch_cache_data, ob->id.name, NULL, NULL);
		}
		gpencil_batch_cache_init(ob, cfra);
	}

	return gpencil_batch_get_element(ob);
}

/* set cache as dirty */
void DRW_gpencil_batch_cache_dirty(bGPdata *gpd)
{
	if (gpd->batch_cache_data == NULL) {
		return;
	}

	GHashIterator *ihash = BLI_ghashIterator_new(gpd->batch_cache_data);
	while (!BLI_ghashIterator_done(ihash)) {
		GpencilBatchCache *cache = (GpencilBatchCache *)BLI_ghashIterator_getValue(ihash);
		if (cache) {
			cache->is_dirty = true;
		}
		BLI_ghashIterator_step(ihash);
	}
	BLI_ghashIterator_free(ihash);
}

/* free batch cache */
void DRW_gpencil_batch_cache_free(bGPdata *gpd)
{
	if (gpd->batch_cache_data == NULL) {
		return;
	}

	GHashIterator *ihash = BLI_ghashIterator_new(gpd->batch_cache_data);
	while (!BLI_ghashIterator_done(ihash)) {
		GpencilBatchCache *cache = (GpencilBatchCache *)BLI_ghashIterator_getValue(ihash);
		if (cache) {
			gpencil_batch_cache_clear(cache, gpd);
		}
		BLI_ghashIterator_step(ihash);
	}
	BLI_ghashIterator_free(ihash);

	/* free hash */
	if (gpd->batch_cache_data) {
		BLI_ghash_free(gpd->batch_cache_data, NULL, NULL);
		gpd->batch_cache_data = NULL;
	}
}
