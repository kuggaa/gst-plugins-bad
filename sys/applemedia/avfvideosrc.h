/*
 * Copyright (C) 2010 Ole André Vadla Ravnås <oravnas@cisco.com>
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

#ifndef __GST_AVF_VIDEO_SRC_H__
#define __GST_AVF_VIDEO_SRC_H__

#include <gst/base/gstpushsrc.h>

G_BEGIN_DECLS

#define GST_TYPE_AVF_VIDEO_SRC \
  (gst_avf_video_src_get_type ())
#define GST_AVF_VIDEO_SRC(obj) \
  (G_TYPE_CHECK_INSTANCE_CAST ((obj), GST_TYPE_AVF_VIDEO_SRC, GstAVFVideoSrc))
#define GST_AVF_VIDEO_SRC_CAST(obj) \
  ((GstAVFVideoSrc *) (obj))
#define GST_AVF_VIDEO_SRC_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_CAST ((klass), GST_TYPE_AVF_VIDEO_SRC, \
  GstAVFVideoSrcClass))
#define GST_AVF_VIDEO_SRC_IMPL(obj) \
  ((GstAVFVideoSrcImpl *) GST_AVF_VIDEO_SRC_CAST (obj)->impl)
#define GST_IS_AVF_VIDEO_SRC(obj) \
  (G_TYPE_CHECK_INSTANCE_TYPE ((obj), GST_TYPE_AVF_VIDEO_SRC))
#define GST_IS_AVF_VIDEO_SRC_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_TYPE ((klass), GST_TYPE_AVF_VIDEO_SRC))

typedef struct _GstAVFVideoSrc GstAVFVideoSrc;
typedef struct _GstAVFVideoSrcClass GstAVFVideoSrcClass;

struct _GstAVFVideoSrc
{
  GstPushSrc push_src;

  gpointer impl;
};

struct _GstAVFVideoSrcClass
{
  GstPushSrcClass parent_class;
};

GType gst_avf_video_src_get_type (void);

G_END_DECLS

#endif /* __GST_AVF_VIDEO_SRC_H__ */
