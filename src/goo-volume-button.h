/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */

/*
 *  Goo
 *
 *  Copyright (C) 2004 Free Software Foundation, Inc.
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 59 Temple Street #330, Boston, MA 02111-1307, USA.
 */

#ifndef GOO_VOLUME_BUTTON_H
#define GOO_VOLUME_BUTTON_H

#include <glib.h>
#include <glib-object.h>
#include <gtk/gtktogglebutton.h>

#define GOO_TYPE_VOLUME_BUTTON              (goo_volume_button_get_type ())
#define GOO_VOLUME_BUTTON(obj)              (G_TYPE_CHECK_INSTANCE_CAST ((obj), GOO_TYPE_VOLUME_BUTTON, GooVolumeButton))
#define GOO_VOLUME_BUTTON_CLASS(klass)      (G_TYPE_CHECK_CLASS_CAST ((klass), GOO_TYPE_VOLUME_BUTTON, GooVolumeButtonClass))
#define GOO_IS_VOLUME_BUTTON(obj)           (G_TYPE_CHECK_INSTANCE_TYPE ((obj), GOO_TYPE_VOLUME_BUTTON))
#define GOO_IS_VOLUME_BUTTON_CLASS(klass)   (G_TYPE_CHECK_CLASS_TYPE ((klass), GOO_TYPE_VOLUME_BUTTON))
#define GOO_VOLUME_BUTTON_GET_CLASS(obj)    (G_TYPE_INSTANCE_GET_CLASS((obj), GOO_TYPE_VOLUME_BUTTON, GooVolumeButtonClass))

typedef struct _GooVolumeButton            GooVolumeButton;
typedef struct _GooVolumeButtonClass       GooVolumeButtonClass;
typedef struct _GooVolumeButtonPrivateData GooVolumeButtonPrivateData;

struct _GooVolumeButton
{
	GtkToggleButton __parent;
	GooVolumeButtonPrivateData *priv;
};

struct _GooVolumeButtonClass
{
	GtkToggleButtonClass __parent_class;

	/*<signals>*/

	void (*changed) (GooVolumeButton *player);
};

GType            goo_volume_button_get_type        (void);
GtkWidget *      goo_volume_button_new             (double from_value,
						    double to_value,
						    double step);
double           goo_volume_button_get_volume      (GooVolumeButton *button);
void             goo_volume_button_set_volume      (GooVolumeButton *button,
						    double           value,
						    gboolean         notify);

#endif /* GOO_VOLUME_BUTTON_H */
