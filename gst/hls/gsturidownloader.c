/* GStreamer
 * Copyright (C) 2011 Andoni Morales Alastruey <ylatuya@gmail.com>
 *
 * gstfragment.c:
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

#include <glib.h>
#include "gstfragmented.h"
#include "gstfragment.h"
#include "gsturidownloader.h"

GST_DEBUG_CATEGORY_STATIC (uridownloader_debug);
#define GST_CAT_DEFAULT (uridownloader_debug)

#define GST_URI_DOWNLOADER_GET_PRIVATE(obj)  \
   (G_TYPE_INSTANCE_GET_PRIVATE ((obj), \
    GST_TYPE_URI_DOWNLOADER, GstUriDownloaderPrivate))

struct _GstUriDownloaderPrivate
{
  /* Fragments fetcher */
  GstElement *urisrc;
  GstBus *bus;
  GstPad *pad;
  GTimeVal *timeout;
  GstFragment *download;
  gint64 offset;
  gint64 length;
  gboolean cancelled;
  GMutex *usage_lock;
  GMutex *lock;
  GCond *cond;
};

static void gst_uri_downloader_finalize (GObject * object);
static void gst_uri_downloader_dispose (GObject * object);

static GstFlowReturn gst_uri_downloader_chain (GstPad * pad, GstBuffer * buf);
static gboolean gst_uri_downloader_sink_event (GstPad * pad, GstEvent * event);
static GstBusSyncReply gst_uri_downloader_bus_handler (GstBus * bus,
    GstMessage * message, gpointer data);

static GstStaticPadTemplate sinkpadtemplate = GST_STATIC_PAD_TEMPLATE ("sink",
    GST_PAD_SINK,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS_ANY);

#define _do_init \
{ \
  GST_DEBUG_CATEGORY_INIT (uridownloader_debug, "uridownloader", 0, "URI downloader"); \
}

G_DEFINE_TYPE_WITH_CODE (GstUriDownloader, gst_uri_downloader, GST_TYPE_OBJECT,
    _do_init);

static void
gst_uri_downloader_class_init (GstUriDownloaderClass * klass)
{
  GObjectClass *gobject_class;

  gobject_class = (GObjectClass *) klass;

  g_type_class_add_private (klass, sizeof (GstUriDownloaderPrivate));

  gobject_class->dispose = gst_uri_downloader_dispose;
  gobject_class->finalize = gst_uri_downloader_finalize;
}

static void
gst_uri_downloader_init (GstUriDownloader * downloader)
{
  downloader->priv = GST_URI_DOWNLOADER_GET_PRIVATE (downloader);

  /* Initialize the sink pad. This pad will be connected to the src pad of the
   * element created with gst_element_make_from_uri and will handle the download */
  downloader->priv->pad =
      gst_pad_new_from_static_template (&sinkpadtemplate, "sink");
  gst_pad_set_chain_function (downloader->priv->pad,
      GST_DEBUG_FUNCPTR (gst_uri_downloader_chain));
  gst_pad_set_event_function (downloader->priv->pad,
      GST_DEBUG_FUNCPTR (gst_uri_downloader_sink_event));
  gst_pad_set_element_private (downloader->priv->pad, downloader);
  gst_pad_set_active (downloader->priv->pad, TRUE);

  /* Create a bus to handle error and warning message from the source element */
  downloader->priv->bus = gst_bus_new ();

  downloader->priv->usage_lock = g_mutex_new ();
  downloader->priv->lock = g_mutex_new ();
  downloader->priv->cond = g_cond_new ();
}

static void
gst_uri_downloader_dispose (GObject * object)
{
  GstUriDownloader *downloader = GST_URI_DOWNLOADER (object);

  if (downloader->priv->urisrc != NULL) {
    gst_element_set_state (downloader->priv->urisrc, GST_STATE_NULL);
    gst_object_unref (downloader->priv->urisrc);
    downloader->priv->urisrc = NULL;
  }

  if (downloader->priv->bus != NULL) {
    gst_object_unref (downloader->priv->bus);
    downloader->priv->bus = NULL;
  }

  if (downloader->priv->pad) {
    gst_object_unref (downloader->priv->pad);
    downloader->priv->pad = NULL;
  }

  if (downloader->priv->download) {
    g_object_unref (downloader->priv->download);
    downloader->priv->download = NULL;
  }

  G_OBJECT_CLASS (gst_uri_downloader_parent_class)->dispose (object);
}

static void
gst_uri_downloader_finalize (GObject * object)
{
  GstUriDownloader *downloader = GST_URI_DOWNLOADER (object);

  g_mutex_free (downloader->priv->usage_lock);
  g_mutex_free (downloader->priv->lock);
  g_cond_free (downloader->priv->cond);

  G_OBJECT_CLASS (gst_uri_downloader_parent_class)->finalize (object);
}

GstUriDownloader *
gst_uri_downloader_new (void)
{
  return g_object_new (GST_TYPE_URI_DOWNLOADER, NULL);
}

static gboolean
gst_uri_downloader_sink_event (GstPad * pad, GstEvent * event)
{
  gboolean ret = FALSE;
  GstUriDownloader *downloader;

  downloader = GST_URI_DOWNLOADER (gst_pad_get_element_private (pad));

  switch (event->type) {
    case GST_EVENT_EOS:{
      GST_OBJECT_LOCK (downloader);
      GST_DEBUG_OBJECT (downloader, "Got EOS on the fetcher pad");
      if (downloader->priv->download != NULL) {
        /* signal we have fetched the URI */
        downloader->priv->download->completed = TRUE;
        downloader->priv->download->download_stop_time =
            gst_util_get_timestamp ();
        GST_OBJECT_UNLOCK (downloader);
        GST_DEBUG_OBJECT (downloader, "Signaling chain funtion");
        g_mutex_lock (downloader->priv->lock);
        g_cond_signal (downloader->priv->cond);
        g_mutex_unlock (downloader->priv->lock);
      } else {
        GST_OBJECT_UNLOCK (downloader);
      }
      gst_event_unref (event);
      break;
    }
    default:
      ret = gst_pad_event_default (pad, event);
      break;
  }

  return ret;
}

static GstBusSyncReply
gst_uri_downloader_bus_handler (GstBus * bus,
    GstMessage * message, gpointer data)
{
  GstUriDownloader *downloader = (GstUriDownloader *) (data);

  if (GST_MESSAGE_TYPE (message) == GST_MESSAGE_ERROR ||
      GST_MESSAGE_TYPE (message) == GST_MESSAGE_WARNING) {
    GError *err = NULL;
    gchar *dbg_info = NULL;

    gst_message_parse_error (message, &err, &dbg_info);
    GST_WARNING_OBJECT (downloader,
        "Received error: %s from %s, the download will be cancelled",
        GST_OBJECT_NAME (message->src), err->message);
    GST_DEBUG ("Debugging info: %s\n", (dbg_info) ? dbg_info : "none");
    g_error_free (err);
    g_free (dbg_info);

    /* remove the sync handler to avoid duplicated messages */
    gst_bus_set_sync_handler (downloader->priv->bus, NULL, NULL);
    gst_uri_downloader_cancel (downloader);
  }

  gst_message_unref (message);
  return GST_BUS_DROP;
}

static GstFlowReturn
gst_uri_downloader_chain (GstPad * pad, GstBuffer * buf)
{
  GstUriDownloader *downloader;
  GstBuffer *frag_buf;
  gsize sfrag, sbuf;
  gboolean eos = FALSE;

  downloader = GST_URI_DOWNLOADER (gst_pad_get_element_private (pad));

  /* HTML errors (404, 500, etc...) are also pushed through this pad as
   * response but the source element will also post a warning or error message
   * in the bus, which is handled synchronously cancelling the download.
   */
  GST_OBJECT_LOCK (downloader);
  if (downloader->priv->download == NULL
      || downloader->priv->download->completed) {
    /* Download cancelled, quit */
    GST_OBJECT_UNLOCK (downloader);
    goto done;
  }

  if (downloader->priv->download->download_start_time == GST_CLOCK_TIME_NONE)
    downloader->priv->download->download_start_time = gst_util_get_timestamp ();

  if (downloader->priv->offset != -1 &&
      GST_BUFFER_OFFSET (buf) < downloader->priv->offset) {
    GST_DEBUG_OBJECT (downloader, "Skipping buffer out of range");
    GST_OBJECT_UNLOCK (downloader);
    goto done;
  }

  frag_buf = buf;
  sbuf = GST_BUFFER_SIZE (buf);
  GST_LOG_OBJECT (downloader, "The uri fetcher received a new buffer "
      "of size %u", sbuf);

  if (downloader->priv->length != -1) {
    sfrag = gst_fragment_get_total_size (downloader->priv->download);
    GST_LOG_OBJECT (downloader, "Fragment size is %u/%u", sfrag,
        (gsize) downloader->priv->length);
    if (sfrag + sbuf > downloader->priv->length) {
      frag_buf = gst_buffer_create_sub (buf, 0,
          downloader->priv->length - sfrag);
      gst_buffer_unref (buf);
      eos = TRUE;
    }
  }

  if (!gst_fragment_add_buffer (downloader->priv->download, frag_buf))
    GST_WARNING_OBJECT (downloader, "Could not add buffer to fragment");
  GST_OBJECT_UNLOCK (downloader);

  if (eos)
    gst_pad_send_event (downloader->priv->pad, gst_event_new_eos ());

done:
  {
    return GST_FLOW_OK;
  }
}

static void
gst_uri_downloader_stop (GstUriDownloader * downloader)
{
  GST_DEBUG_OBJECT (downloader, "Stopping source element");

  /* set the element state to NULL */
  gst_element_set_state (downloader->priv->urisrc, GST_STATE_READY);
  gst_element_get_state (downloader->priv->urisrc, NULL, NULL,
      GST_CLOCK_TIME_NONE);
}

void
gst_uri_downloader_cancel (GstUriDownloader * downloader)
{
  GST_OBJECT_LOCK (downloader);
  if (downloader->priv->download != NULL) {
    GST_DEBUG_OBJECT (downloader, "Cancelling download");
    g_object_unref (downloader->priv->download);
    downloader->priv->download = NULL;
    GST_OBJECT_UNLOCK (downloader);
    GST_DEBUG_OBJECT (downloader, "Signaling chain funtion");
    g_mutex_lock (downloader->priv->lock);
    g_cond_signal (downloader->priv->cond);
    g_mutex_unlock (downloader->priv->lock);
  } else {
    downloader->priv->cancelled = TRUE;
    GST_OBJECT_UNLOCK (downloader);
    downloader->priv->cancelled = FALSE;
  }
}

static gboolean
gst_uri_downloader_set_uri (GstUriDownloader * downloader, const gchar * uri)
{
  GstPad *pad;
  GstElement *source;
  gboolean new_element = FALSE;

  if (!gst_uri_is_valid (uri))
    return FALSE;

  source = gst_element_make_from_uri (GST_URI_SRC, uri, NULL);
  if (source == NULL)
    return FALSE;

  /* Make sure we can reuse the same source element for this new URI */
  if (downloader->priv->urisrc != NULL) {
    if (gst_element_get_factory (source) !=
        gst_element_get_factory (downloader->priv->urisrc)) {
      gst_object_unref (downloader->priv->urisrc);
      downloader->priv->urisrc = source;
      new_element = TRUE;
    } else {
      gst_object_unref (source);
    }
  } else {
    downloader->priv->urisrc = source;
    new_element = TRUE;
  }

  if (new_element) {
    GST_DEBUG_OBJECT (downloader, "Creating source element for the URI:%s",
        uri);
    downloader->priv->urisrc = source;

    /* Use keep-alive when possible */
    if (g_object_class_find_property (G_OBJECT_GET_CLASS (downloader->priv->
                urisrc), "keep-alive")) {
      g_object_set (downloader->priv->urisrc, "keep-alive", TRUE, NULL);
    }
    /* add a sync handler for the bus messages to detect errors in the download */
    gst_element_set_bus (GST_ELEMENT (downloader->priv->urisrc),
        downloader->priv->bus);

    pad = gst_element_get_static_pad (downloader->priv->urisrc, "src");
    if (!pad)
      return FALSE;
    gst_pad_link (pad, downloader->priv->pad);
    gst_object_unref (pad);
  }
  gst_bus_set_sync_handler (downloader->priv->bus,
      gst_uri_downloader_bus_handler, downloader);
  gst_uri_handler_set_uri (GST_URI_HANDLER (downloader->priv->urisrc), uri);

  return TRUE;
}

GstFragment *
gst_uri_downloader_fetch_fragment (GstUriDownloader * downloader,
    GstFragment * fragment)
{
  GstStateChangeReturn ret;
  GstFragment *download = NULL;
  gint64 offset, length;

  g_mutex_lock (downloader->priv->usage_lock);

  GST_DEBUG_OBJECT (downloader, "Fetching new URI %s", fragment->name);

  GST_OBJECT_LOCK (downloader);
  if (downloader->priv->cancelled) {
    downloader->priv->cancelled = FALSE;
    GST_OBJECT_UNLOCK (downloader);
    goto quit;
  }
  downloader->priv->download = fragment;
  downloader->priv->download->download_start_time = GST_CLOCK_TIME_NONE;
  downloader->priv->length = length = fragment->length;
  downloader->priv->offset = offset = fragment->offset;

  if (!gst_uri_downloader_set_uri (downloader, fragment->name)) {
    GST_OBJECT_UNLOCK (downloader);
    goto quit;
  }
  GST_OBJECT_UNLOCK (downloader);

  ret = gst_element_set_state (downloader->priv->urisrc, GST_STATE_PAUSED);

  if (ret == GST_STATE_CHANGE_FAILURE) {
    if (downloader->priv->download != NULL)
      g_object_unref (downloader->priv->download);
    downloader->priv->download = NULL;
    goto quit;
  }

  g_mutex_lock (downloader->priv->lock);

  /* Check if we were cancelled while setting the URI */
  if (downloader->priv->download == NULL) {
    g_mutex_unlock (downloader->priv->lock);
    goto quit;
  }

  if (offset != -1 && length != -1) {
    GST_INFO_OBJECT (downloader, "range request offset:%" G_GUINT64_FORMAT
        " length:%" G_GUINT64_FORMAT, offset, length);
    if (!gst_element_seek (downloader->priv->urisrc, 1, GST_FORMAT_BYTES,
            GST_SEEK_FLAG_ACCURATE, GST_SEEK_TYPE_SET, offset,
            GST_SEEK_TYPE_SET, offset + length)) {
      GST_INFO_OBJECT (downloader->priv->urisrc,
          "This server does not support range requests");
    }
  }

  ret = gst_element_set_state (downloader->priv->urisrc, GST_STATE_PLAYING);
  if (ret == GST_STATE_CHANGE_FAILURE) {
    g_object_unref (downloader->priv->download);
    downloader->priv->download = NULL;
    g_mutex_unlock (downloader->priv->lock);
    goto quit;
  }

  /* wait until:
   *   - the download succeed (EOS in the src pad)
   *   - the download failed (Error message on the fetcher bus)
   *   - the download was canceled
   */
  GST_DEBUG_OBJECT (downloader, "Waiting to fetch the URI");
  g_cond_wait (downloader->priv->cond, downloader->priv->lock);
  g_mutex_unlock (downloader->priv->lock);

  GST_OBJECT_LOCK (downloader);
  download = downloader->priv->download;
  downloader->priv->download = NULL;
  GST_OBJECT_UNLOCK (downloader);

  if (download != NULL)
    GST_INFO_OBJECT (downloader, "URI fetched successfully");
  else
    GST_INFO_OBJECT (downloader, "Error fetching URI");

quit:
  {
    gst_bus_set_sync_handler (downloader->priv->bus, NULL, NULL);
    gst_uri_downloader_stop (downloader);
    g_mutex_unlock (downloader->priv->usage_lock);
    return download;
  }
}

GstFragment *
gst_uri_downloader_fetch_uri_range (GstUriDownloader * downloader,
    const gchar * uri, gint64 offset, gint64 length)
{
  GstFragment *fragment;

  fragment = gst_fragment_new ();
  g_free (fragment->name);
  fragment->name = g_strdup (uri);
  fragment->offset = offset;
  fragment->length = length;

  return gst_uri_downloader_fetch_fragment (downloader, fragment);
}

GstFragment *
gst_uri_downloader_fetch_uri (GstUriDownloader * downloader, const gchar * uri)
{
  return gst_uri_downloader_fetch_uri_range (downloader, uri, -1, -1);
}
