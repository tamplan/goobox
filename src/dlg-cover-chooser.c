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


#include <config.h>
#include <string.h>
#include <gtk/gtk.h>
#include "gio-utils.h"
#include "glib-utils.h"
#include "gtk-utils.h"
#include "goo-window.h"
#include "goo-stock.h"

#define BUFFER_SIZE 4096
#define COVER_BACKUP_FILENAME "original_cover.png"
#define MAX_IMAGES 20
#define GET_WIDGET(x) _gtk_builder_get_widget (data->builder, (x))


enum {
	URL_COLUMN,
	IMAGE_COLUMN,
	N_COLUMNS
};


typedef struct {
	GooWindow    *window;
	char         *artist;
	char         *album;
	GtkBuilder   *builder;
	GtkWidget    *dialog;
	GtkWidget    *icon_view;
	GdkPixbuf    *cover_backup;
	GList        *file_list;
	int           total_files;
	GList        *current_file;
	int           loaded_files;
	GCancellable *cancellable;
	gboolean      searching;
	gboolean      destroy;
} DialogData;


static GList *
make_file_list_from_search_result (void  *buffer,
				   gsize  count,
				   int    max_files)
{
	GList        *list = NULL;
	int           n_files = 0;
	gboolean      done = FALSE;
	GInputStream *stream;
	gssize        n;
	char          buf[BUFFER_SIZE];
	int           buf_offset = 0;
	GString      *partial_url;

	stream = g_memory_input_stream_new_from_data (buffer, count, NULL);
	partial_url = NULL;
	while ((n = g_input_stream_read (stream,
					 buf + buf_offset,
					 BUFFER_SIZE - buf_offset - 1,
					 NULL,
					 NULL)) > 0)
	{
		const char *prefix = "/images?q=tbn:";
		int         prefix_len = strlen (prefix);
		char       *url_start;
		gboolean    copy_tail = TRUE;

		buf[buf_offset+n] = 0;

		if (partial_url == NULL)
			url_start = strstr (buf, prefix);
		else
			url_start = buf;

		while (url_start != NULL) {
			char *url_end;

			url_end = strstr (url_start, "\"");

			if (url_end == NULL) {
				if (partial_url == NULL)
					partial_url = g_string_new (url_start);
				else
					g_string_append (partial_url, url_start);
				url_start = NULL;
				copy_tail = FALSE;
			}
			else {
				char *url_tail = g_strndup (url_start, url_end - url_start);
				char *url;
				char *complete_url;

				if (partial_url != NULL) {
					g_string_append (partial_url, url_tail);
					g_free (url_tail);
					url = partial_url->str;
					g_string_free (partial_url, FALSE);
					partial_url = NULL;
				}
				else
					url = url_tail;

				complete_url = g_strconcat ("http://images.google.com", url, NULL);
				g_free (url);

				list = g_list_prepend (list, complete_url);
				n_files++;
				if (n_files >= max_files) {
					done = TRUE;
					break;
				}

				url_start = strstr (url_end + 1, prefix);
			}
		}

		if (done)
			break;

		if (copy_tail) {
			prefix_len = MIN (prefix_len, buf_offset + n);
			strncpy (buf,
				 buf + buf_offset + n - prefix_len,
				 prefix_len);
			buf_offset = prefix_len;
		}
		else
			buf_offset = 0;
	}

	if (partial_url != NULL)
		g_string_free (partial_url, TRUE);

	g_object_unref (stream);

	return g_list_reverse (list);
}


static char *
get_query (const char *album,
	   const char *artist)
{
	char *s, *e, *q;

	s = g_strdup_printf ("%s %s", album, artist);
	e = g_uri_escape_string (s, G_URI_RESERVED_CHARS_ALLOWED_IN_PATH, FALSE);
	q = g_strconcat ("http://images.google.com/images?q=", e, NULL);

	g_free (e);
	g_free (s);

	return q;
}


/* -- dlg_cover_chooser -- */


static void
destroy_cb (GtkWidget  *widget,
	    DialogData *data)
{
	if (data->searching) {
		data->destroy = TRUE;
		g_cancellable_cancel (data->cancellable);
		return;
	}

	g_object_unref (data->cancellable);
	_g_string_list_free (data->file_list);
	_g_object_unref (data->cover_backup);
	g_object_unref (data->builder);
	g_free (data->album);
	g_free (data->artist);
	g_free (data);
}


static void
search_completed (DialogData *data)
{
	char *text;

	data->searching = FALSE;
	gtk_widget_set_sensitive (GET_WIDGET ("cancel_search_button"), FALSE);
	text = g_strdup_printf ("%u", data->total_files);
	gtk_label_set_text (GTK_LABEL (GET_WIDGET ("progress_label")), text);

	g_free (text);

	if (data->destroy)
		destroy_cb (NULL, data);
}


static void load_current_file (DialogData *data);


static void
search_image_data_ready_cb (void     *buffer,
			    gsize     count,
			    GError   *error,
			    gpointer  user_data)
{
	DialogData *data = user_data;

	if (g_error_matches (error, G_IO_ERROR, G_IO_ERROR_CANCELLED)) {
		search_completed (data);
		return;
	}

	if (error == NULL) {
		GInputStream *stream;
		GdkPixbuf    *image;

		stream = g_memory_input_stream_new_from_data (buffer, count, NULL);
		image = gdk_pixbuf_new_from_stream (stream, NULL, &error);
		if (image != NULL) {
			GtkTreeModel *model;
			GtkTreeIter   iter;
			char         *url;

			model = gtk_icon_view_get_model (GTK_ICON_VIEW (data->icon_view));
			gtk_list_store_append (GTK_LIST_STORE (model), &iter);
			url = (char *) data->current_file->data;
			gtk_list_store_set (GTK_LIST_STORE (model), &iter,
					    URL_COLUMN, url,
					    IMAGE_COLUMN, image,
					    -1);

			g_object_unref (image);
		}
	}

	data->loaded_files++;
	data->current_file = data->current_file->next;
	load_current_file (data);
}


static void
update_progress_label (DialogData *data)
{
	char *text;

	if (data->loaded_files < data->total_files)
		text = g_strdup_printf (_("%u, loading image: %u"),
					data->total_files,
					data->loaded_files + 1);
	else
		text = g_strdup_printf ("%u", data->total_files);
	gtk_label_set_text (GTK_LABEL (GET_WIDGET ("progress_label")), text);

	g_free (text);
}


static void
load_current_file (DialogData *data)
{
	char  *url;
	GFile *source;

	update_progress_label (data);

	if (data->current_file == NULL) {
		search_completed (data);
		return;
	}

	url = data->current_file->data;

	debug (DEBUG_INFO, "LOADING %s\n", url);

	source = g_file_new_for_uri (url);
	g_load_file_async (source,
			   G_PRIORITY_DEFAULT,
			   data->cancellable,
			   search_image_data_ready_cb,
			   data);

	g_object_unref (source);
}


static void
start_loading_files (DialogData *data)
{
	gtk_list_store_clear (GTK_LIST_STORE (gtk_icon_view_get_model (GTK_ICON_VIEW (data->icon_view))));
	data->total_files = g_list_length (data->file_list);
	data->current_file = data->file_list;
	data->loaded_files = 0;
	load_current_file (data);
}


static void
search_query_ready_cb (void     *buffer,
		       gsize     count,
		       GError   *error,
		       gpointer  user_data)
{
	DialogData *data = user_data;

	if (g_error_matches (error, G_IO_ERROR, G_IO_ERROR_CANCELLED)) {
		search_completed (data);
		return;
	}

	if (error != NULL) {
		_gtk_error_dialog_from_gerror_show (GTK_WINDOW (data->dialog),
						    _("Could not search for a cover on Internet"),
						    &error);
		search_completed (data);
		return;
	}

	data->file_list = make_file_list_from_search_result (buffer, count, MAX_IMAGES);
	start_loading_files (data);
}


static void
start_searching (DialogData *data)
{
	char  *query;
	GFile *file;

	data->searching = TRUE;
	g_cancellable_reset (data->cancellable);
	gtk_widget_set_sensitive (GET_WIDGET ("cancel_search_button"), TRUE);

	query = get_query (data->album, data->artist);
	file = g_file_new_for_uri (query);
	g_load_file_async (file,
			   G_PRIORITY_DEFAULT,
			   data->cancellable,
			   search_query_ready_cb,
			   data);

	g_object_unref (file);
	g_free (query);
}


static void
help_button_clicked_cb (GtkWidget  *widget,
			DialogData *data)
{
	show_help_dialog (GTK_WINDOW (data->window), "search_cover_on_internet");
}


static void
revert_button_clicked_cb (GtkWidget  *widget,
			  DialogData *data)
{
	goo_window_set_cover_image_from_pixbuf (data->window, data->cover_backup);
}


static void
ok_button_clicked_cb (GtkWidget  *widget,
		      DialogData *data)
{
	GList *list;

	list = gtk_icon_view_get_selected_items (GTK_ICON_VIEW (data->icon_view));
	if (list != NULL) {
		GtkTreePath  *path;
		GtkTreeModel *model;
		GtkTreeIter   iter;

		path = list->data;
		model = gtk_icon_view_get_model (GTK_ICON_VIEW (data->icon_view));
		if (gtk_tree_model_get_iter (model, &iter, path)) {
			GdkPixbuf *image;

			gtk_tree_model_get (model, &iter, IMAGE_COLUMN, &image, -1);
			goo_window_set_cover_image_from_pixbuf (data->window, image);

			g_object_unref (image);
		}

		g_list_foreach (list, (GFunc) gtk_tree_path_free, NULL);
		g_list_free (list);
	}
}


static void
icon_view_selection_changed_cb (GtkIconView *icon_view,
				DialogData  *data)
{
	GList *list;

	list = gtk_icon_view_get_selected_items (GTK_ICON_VIEW (data->icon_view));
	gtk_widget_set_sensitive (GET_WIDGET ("ok_button"), list != NULL);

	g_list_foreach (list, (GFunc) gtk_tree_path_free, NULL);
	g_list_free (list);
}


static void
icon_view_item_activated_cb (GtkIconView *icon_view,
			     GtkTreePath *path,
			     DialogData  *data)
{
	ok_button_clicked_cb (NULL, data);
}


static void
cancel_search_button_clicked_cb (GtkWidget  *widget,
       				 DialogData *data)
{
	g_cancellable_cancel (data->cancellable);
}


static void
backup_cover_image (DialogData *data)
{
	char *cover_filename;

	cover_filename = goo_window_get_cover_filename (data->window);
	gtk_widget_set_sensitive (GET_WIDGET ("revert_button"), cover_filename != NULL);
	if (cover_filename != NULL)
		data->cover_backup = gdk_pixbuf_new_from_file (cover_filename, NULL);

	g_free (cover_filename);
}


void
dlg_cover_chooser (GooWindow  *window,
		   const char *album,
		   const char *artist)
{
	DialogData      *data;
	GtkListStore    *model;
	GtkCellRenderer *renderer;
	GtkWidget       *image;

	data = g_new0 (DialogData, 1);
	data->window = window;
	data->builder = _gtk_builder_new_from_file ("cover-chooser.ui", "");
	data->album = g_strdup (album);
	data->artist = g_strdup (artist);
	data->cancellable = g_cancellable_new ();

	/* Get the widgets. */

	data->dialog = GET_WIDGET ("cover_chooser_dialog");

	model = gtk_list_store_new (N_COLUMNS,
				    G_TYPE_STRING,
				    GDK_TYPE_PIXBUF);
	data->icon_view = gtk_icon_view_new_with_model (GTK_TREE_MODEL (model));
	g_object_unref (model);

	renderer = gtk_cell_renderer_pixbuf_new ();
	g_object_set (renderer, "follow-state", TRUE, NULL);
	gtk_cell_layout_pack_start (GTK_CELL_LAYOUT (data->icon_view),
				    renderer,
				    TRUE);
	gtk_cell_layout_set_attributes (GTK_CELL_LAYOUT (data->icon_view),
					renderer,
					"pixbuf", IMAGE_COLUMN,
					NULL);

	gtk_widget_show (data->icon_view);
	gtk_container_add (GTK_CONTAINER (GET_WIDGET ("icon_view_scrolledwindow")), data->icon_view);

	/* Set widgets data. */

	backup_cover_image (data);

	gtk_widget_set_sensitive (GET_WIDGET ("ok_button"), FALSE);

	image = gtk_image_new_from_stock (GOO_STOCK_RESET, GTK_ICON_SIZE_BUTTON);
	g_object_set (GET_WIDGET ("revert_button"),
		      "use_stock", TRUE,
		      "label", GOO_STOCK_RESET,
		      "image", image,
		      NULL);

	/* Set the signals handlers. */

	g_signal_connect (G_OBJECT (data->dialog),
			  "destroy",
			  G_CALLBACK (destroy_cb),
			  data);
	g_signal_connect_swapped (GET_WIDGET ("cancel_button"),
				  "clicked",
				  G_CALLBACK (gtk_widget_destroy),
				  G_OBJECT (data->dialog));
	g_signal_connect (GET_WIDGET ("help_button"),
			  "clicked",
			  G_CALLBACK (help_button_clicked_cb),
			  data);
	g_signal_connect (GET_WIDGET ("ok_button"),
			  "clicked",
			  G_CALLBACK (ok_button_clicked_cb),
			  data);
	g_signal_connect (GET_WIDGET ("revert_button"),
			  "clicked",
			  G_CALLBACK (revert_button_clicked_cb),
			  data);
	g_signal_connect (G_OBJECT (data->icon_view),
			  "selection-changed",
			  G_CALLBACK (icon_view_selection_changed_cb),
			  data);
	g_signal_connect (G_OBJECT (data->icon_view),
			  "item-activated",
			  G_CALLBACK (icon_view_item_activated_cb),
			  data);
	g_signal_connect (GET_WIDGET ("cancel_search_button"),
			  "clicked",
			  G_CALLBACK (cancel_search_button_clicked_cb),
			  data);

	/* run dialog. */

	gtk_window_set_transient_for (GTK_WINDOW (data->dialog), GTK_WINDOW (window));
	gtk_window_set_modal (GTK_WINDOW (data->dialog), FALSE);
	gtk_widget_show (data->dialog);

	start_searching (data);
}


/* -- auto fetch functions -- */


typedef struct {
	GooWindow *window;
} FetchData;


static void
fetch_data_free (FetchData *data)
{
	g_free (data);
}


static void
fetch_image_data_ready_cb (void     *buffer,
			   gsize     count,
			   GError   *error,
			   gpointer  user_data)
{
	FetchData *data = user_data;

	if (error == NULL)
		goo_window_set_cover_image_from_data (data->window, buffer, count);
	fetch_data_free (data);
}


static void
query_ready_cb (void     *buffer,
		gsize     count,
		GError   *error,
		gpointer  user_data)
{
	FetchData *data = user_data;
	GList     *list;

	if (error != NULL) {
		fetch_data_free (data);
		return;
	}

	list = make_file_list_from_search_result (buffer, count, 1);
	if (list != NULL) {
		GFile *file;

		file = g_file_new_for_uri ((char *) list->data);
		g_load_file_async (file,
				   G_PRIORITY_DEFAULT,
				   NULL,
				   fetch_image_data_ready_cb,
				   data);

		g_object_unref (file);
	}
	else
		fetch_data_free (data);

	_g_string_list_free (list);
}


void
fetch_cover_image_from_name (GooWindow  *window,
		             const char *album,
		             const char *artist)
{
	FetchData *data;
	char      *url;
	GFile     *file;

	data = g_new0 (FetchData, 1);
	data->window = window;

	url = get_query (album, artist);
	file = g_file_new_for_uri (url);
	g_load_file_async (file,
			   G_PRIORITY_DEFAULT,
			   NULL,
			   query_ready_cb,
			   data);

	g_object_unref (file);
	g_free (url);
}


void
fetch_cover_image_from_asin (GooWindow  *window,
		             const char *asin)
{
	FetchData *data;
	char      *url;
	GFile     *file;

	data = g_new0 (FetchData, 1);
	data->window = window;
	url = g_strdup_printf ("http://images.amazon.com/images/P/%s.01._SCLZZZZZZZ_.jpg", asin);
	file = g_file_new_for_uri (url);
	g_load_file_async (file,
			   G_PRIORITY_DEFAULT,
			   NULL,
			   fetch_image_data_ready_cb,
			   data);

	g_object_unref (file);
	g_free (url);
}
