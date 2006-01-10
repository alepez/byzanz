/* desktop session recorder
 * Copyright (C) 2005 Benjamin Otte <otte@gnome.org
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Library General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Library General Public License for more details.
 *
 * You should have received a copy of the GNU Library General Public
 * License along with this library; if not, write to the
 * Free Software Foundation, Inc., 59 Temple Place - Suite 330,
 * Boston, MA 02111-1307, USA.
 */

#ifdef HAVE_CONFIG_H
#  include "config.h"
#endif

#include "byzanzselect.h"
#include "i18n.h"

/*** SELECT AREA ***/

typedef struct {
  GtkWidget *window;
  GdkImage *root; /* only used without XComposite, NULL otherwise */
  GMainLoop *loop;
  gint x0;
  gint y0;
  gint x1;
  gint y1;
} WindowData;

/* define for SLOW selection mechanism */
#undef TARGET_LINE

static gboolean
expose_cb (GtkWidget *widget, GdkEventExpose *event, gpointer datap)
{
  cairo_t *cr;
  WindowData *data = datap;
#ifdef TARGET_LINE
  static double dashes[] = { 1.0, 2.0 };
#endif

  cr = gdk_cairo_create (widget->window);
  cairo_clip (cr);
  if (data->root) {
    gdk_draw_image (widget->window, widget->style->black_gc, data->root,
	event->area.x, event->area.y, event->area.x, event->area.y,
	event->area.width, event->area.height);
  } else {
    cairo_rectangle (cr, event->area.x, event->area.y, event->area.width, event->area.height);
    cairo_set_operator (cr, CAIRO_OPERATOR_SOURCE);
    cairo_set_source_rgba (cr, 0.0, 0.0, 0.0, 0.0);
    cairo_paint (cr);
  }
  /* FIXME: make colors use theme */
  cairo_set_line_width (cr, 1.0);
#ifdef TARGET_LINE
  cairo_set_source_rgba (cr, 1.0, 0.0, 0.0, 1.0);
  cairo_set_dash (cr, dashes, G_N_ELEMENTS (dashes), 0.0);
  cairo_move_to (cr, data->x1 - 0.5, 0.0);
  cairo_line_to (cr, data->x1 - 0.5, event->area.y + event->area.height); /* end of screen really */
  cairo_move_to (cr, 0.0, data->y1 - 0.5);
  cairo_line_to (cr, event->area.x + event->area.width, data->y1 - 0.5); /* end of screen really */
  cairo_stroke (cr);
#endif
  if (data->x0 >= 0) {
    double x, y, w, h;
    x = MIN (data->x0, data->x1);
    y = MIN (data->y0, data->y1);
    w = MAX (data->x0, data->x1) - x;
    h = MAX (data->y0, data->y1) - y;
    g_print ("%g %g %g %g\n", x, y, w, h);
    cairo_set_source_rgba (cr, 0.0, 0.0, 0.5, 0.2);
    cairo_set_dash (cr, NULL, 0, 0.0);
    cairo_rectangle (cr, x, y, w, h);
    cairo_fill (cr);
    cairo_set_source_rgba (cr, 0.0, 0.0, 0.5, 0.5);
    cairo_rectangle (cr, x + 0.5, y + 0.5, w - 1, h - 1);
    cairo_stroke (cr);
  }
  if (cairo_status (cr) != CAIRO_STATUS_SUCCESS)
    g_warning ("cairo error: %s\n", cairo_status_to_string (cairo_status (cr)));
  cairo_destroy (cr);
  return FALSE;
}

static void
byzanz_select_area_stop (WindowData *data)
{
  gtk_widget_destroy (data->window);
  g_main_loop_quit (data->loop);
}

static gboolean
button_pressed_cb (GtkWidget *widget, GdkEventButton *event, gpointer datap)
{
  WindowData *data = datap;

  if (event->button != 1) {
    data->x0 = data->y0 = -1;
    byzanz_select_area_stop (data);
    return TRUE;
  }
  g_print ("clicked %g %g\n", event->x, event->y);
  data->x0 = event->x;
  data->y0 = event->y;

  gtk_widget_queue_draw (widget);

  return TRUE;
}

static gboolean
button_released_cb (GtkWidget *widget, GdkEventButton *event, gpointer datap)
{
  WindowData *data = datap;
  
  if (event->button == 1 && data->x0 >= 0) {
    data->x1 = event->x + 1;
    data->y1 = event->y + 1;
    byzanz_select_area_stop (data);
  }
  
  return TRUE;
}

static gboolean
motion_notify_cb (GtkWidget *widget, GdkEventMotion *event, gpointer datap)
{
  WindowData *data = datap;
  
#ifdef TARGET_LINE
  gtk_widget_queue_draw (widget);
#else
  if (data->x0 >= 0) {
    GdkRectangle rect;
    rect.x = MIN (data->x0, MIN (data->x1, event->x + 1));
    rect.width = MAX (data->x0, MAX (data->x1, event->x + 1)) - rect.x;
    rect.y = MIN (data->y0, MIN (data->y1, event->y + 1));
    rect.height = MAX (data->y0, MAX (data->y1, event->y + 1)) - rect.y;
    gtk_widget_queue_draw_area (widget, rect.x, rect.y, rect.width, rect.height);
  }
#endif
  data->x1 = event->x + 1;
  data->y1 = event->y + 1;

  return TRUE;
}

static void
realize_cb (GtkWidget *widget, gpointer datap)
{
  GdkWindow *window = widget->window;
  GdkCursor *cursor;

  gdk_window_set_events (window, gdk_window_get_events (window) |
      GDK_BUTTON_PRESS_MASK |
      GDK_BUTTON_RELEASE_MASK |
      GDK_POINTER_MOTION_MASK);
  cursor = gdk_cursor_new (GDK_CROSSHAIR);
  gdk_window_set_cursor (window, cursor);
  gdk_cursor_unref (cursor);
}

static GdkWindow *
byzanz_select_area (GdkRectangle *rect)
{
  GdkColormap *rgba;
  WindowData *data;
  GdkWindow *ret = NULL;
  
  rgba = gdk_screen_get_rgba_colormap (gdk_screen_get_default ());
  if (!rgba) {
    g_warning ("No RGBA colormap available\n");
    return NULL;
  }
  data = g_new0 (WindowData, 1);
  data->window = gtk_window_new (GTK_WINDOW_TOPLEVEL);
  data->loop = g_main_loop_new (NULL, FALSE);
  data->x0 = data->y0 = -1;
  if (rgba) {
    gtk_widget_set_colormap (data->window, rgba);
  } else {
    GdkWindow *root = gdk_get_default_root_window ();
    gint width, height;
    gdk_drawable_get_size (root, &width, &height);
    data->root = gdk_drawable_get_image (root, 0, 0, width, height);
  }
  gtk_widget_set_app_paintable (data->window, TRUE);
  gtk_window_fullscreen (GTK_WINDOW (data->window));
  g_signal_connect (data->window, "expose-event", G_CALLBACK (expose_cb), data);
  g_signal_connect (data->window, "button-press-event", G_CALLBACK (button_pressed_cb), data);
  g_signal_connect (data->window, "button-release-event", G_CALLBACK (button_released_cb), data);
  g_signal_connect (data->window, "motion-notify-event", G_CALLBACK (motion_notify_cb), data);
  g_signal_connect (data->window, "delete-event", G_CALLBACK (gtk_main_quit), data);
  g_signal_connect_after (data->window, "realize", G_CALLBACK (realize_cb), data);
  gtk_widget_show_all (data->window);

  g_main_loop_run (data->loop);
  
  if (data->x0 >= 0) {
    rect->x = MIN (data->x1, data->x0);
    rect->y = MIN (data->y1, data->y0);
    rect->width = MAX (data->x1, data->x0) - rect->x;
    rect->height = MAX (data->y1, data->y0) - rect->y;
    ret = gdk_get_default_root_window ();
    /* stupid hack to get around a recorder recording the selection screen */
    gdk_display_sync (gdk_display_get_default ());
    g_usleep (G_USEC_PER_SEC);
  }
  g_main_loop_unref (data->loop);
  if (data->root)
    g_object_unref (data->root);
  g_free (data);
  return ret;
}

/*** API ***/

static const struct {
  char * description;
  GdkWindow * (* select) (GdkRectangle *rect);
} methods [] = {
  { N_("Select area"), byzanz_select_area }
};
#define BYZANZ_METHOD_COUNT G_N_ELEMENTS(methods)

guint
byzanz_select_get_method_count (void)
{
  return BYZANZ_METHOD_COUNT;
}
const gchar *
byzanz_select_method_describe (guint method)
{
  g_return_val_if_fail (method < BYZANZ_METHOD_COUNT, NULL);

  return _(methods[method].description);
}

GdkWindow *
byzanz_select_method_select (guint method, GdkRectangle *rect)
{
  g_return_val_if_fail (method < BYZANZ_METHOD_COUNT, NULL);
  g_return_val_if_fail (rect != NULL, NULL);

  g_assert (methods[method].select != NULL);

  return methods[method].select (rect);
}

