/* screenshotsrc: Screenshot plugin for GStreamer
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
 * Free Software Foundation, Inc., 51 Franklin St, Fifth Floor,
 * Boston, MA 02110-1301, USA.
 */

#ifndef __GST_NVIMAGE_SRC_H__
#define __GST_NVIMAGE_SRC_H__

#include <gst/gst.h>
#include <gst/base/gstpushsrc.h>
#include "nvimageutil.h"

G_BEGIN_DECLS

#define GST_TYPE_NVIMAGE_SRC (gst_nvimage_src_get_type())
#define GST_NVIMAGE_SRC(obj) (G_TYPE_CHECK_INSTANCE_CAST((obj),GST_TYPE_NVIMAGE_SRC,GstNVimageSrc))
#define GST_NVIMAGE_SRC_CLASS(klass) (G_TYPE_CHECK_CLASS_CAST((klass),GST_TYPE_NVIMAGE_SRC,GstNVimageSrcClass))
#define GST_IS_NVIMAGE_SRC(obj) (G_TYPE_CHECK_INSTANCE_TYPE((obj),GST_TYPE_NVIMAGE_SRC))
#define GST_IS_NVIMAGE_SRC_CLASS(klass) (G_TYPE_CHECK_CLASS_TYPE((klass),GST_TYPE_NVIMAGE_SRC))

typedef struct _GstNVimageSrc GstNVimageSrc;
typedef struct _GstNVimageSrcClass GstNVimageSrcClass;

GType gst_nvimage_src_get_type (void) G_GNUC_CONST;

struct _GstNVimageSrc
{
  GstPushSrc parent;

  /* Information on display */
  GstXContext *xcontext;
  gint x;
  gint y;
  gint width;
  gint height;

  gchar *display_name;

  GstNVimageCodec codec;

  /* Desired output framerate */
  gint fps_n;
  gint fps_d;

  /* for framerate sync */
  GstClockID clock_id;
  gint64 last_frame_no;
  gint64 frame;

  gboolean show_pointer;

  guint bitrate;
  gboolean keyframe;
};

struct _GstNVimageSrcClass
{
  GstPushSrcClass parent_class;
};

G_END_DECLS

#endif /* __GST_NVIMAGE_SRC_H__ */
