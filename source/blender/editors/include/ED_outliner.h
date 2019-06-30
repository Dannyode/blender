/*
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
 * The Original Code is Copyright (C) 2015, Blender Foundation
 */

/** \file
 * \ingroup editors
 */

#ifndef __ED_OUTLINER_H__
#define __ED_OUTLINER_H__

struct ListBase;
struct bContext;

/* Flag to indicate if a sync is to extend or replace the outliner selection */
extern short sync_select_dirty_flag;

/* sync_select_dirty_flag types */
enum {
  SYNC_SELECT_NONE = 0,
  SYNC_SELECT_REPLACE = 1,
  SYNC_SELECT_EXTEND = 2,
};

bool ED_outliner_collections_editor_poll(struct bContext *C);

void ED_outliner_selected_objects_get(const struct bContext *C, struct ListBase *objects);

Base *ED_outliner_give_base_under_cursor(struct bContext *C, const int mval[2]);

#endif /*  __ED_OUTLINER_H__ */
