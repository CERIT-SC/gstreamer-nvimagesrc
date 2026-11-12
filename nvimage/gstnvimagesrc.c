/* GStreamer
 *
 * Copyright (C) 2006 Zaheer Merali <zaheerabbas at merali dot org>
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

/**
 * SECTION:element-nvimagesrc
 * @title: nvimagesrc
 *
 * This element captures your X Display and creates raw RGB video.  It uses
 * the XDamage extension if available to only capture areas of the screen that
 * have changed since the last frame.  It uses the XFixes extension if
 * available to also capture your mouse pointer.  By default it will fixate to
 * 25 frames per second.
 *
 * ## Example pipelines
 * |[
 * gst-launch-1.0 nvimagesrc ! video/x-raw,framerate=5/1 ! videoconvert ! theoraenc ! oggmux ! filesink location=desktop.ogg
 * ]| Encodes your X display to an Ogg theora video at 5 frames per second.
 *
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif
#include "gstnvimagesrc.h"

#include <string.h>
#include <stdlib.h>
#include <unistd.h>

#include <X11/Xlib.h>
#include <X11/Xutil.h>

#include <gst/gst.h>
#include <gst/video/video.h>

#include "gst/glib-compat-private.h"

GST_DEBUG_CATEGORY (gst_debug_nvimage_src);
#define GST_CAT_DEFAULT gst_debug_nvimage_src

static GstStaticPadTemplate t =
GST_STATIC_PAD_TEMPLATE ("src", GST_PAD_SRC, GST_PAD_ALWAYS,
    GST_STATIC_CAPS ("video/x-h264, "  /* H264 */
        "framerate = (fraction) [ 0, MAX ], "
        "width = (int) [ 145, 4096 ], " "height = (int) [ 49, 4095 ], "
        "stream-format = (string) byte-stream, "
        "alignment = (string) au, "
        "profile = (string) high; "
        "video/x-h265, "  /* H265 */
        "framerate = (fraction) [ 0, MAX ], "
        "width = (int) [ 145, 4096 ], " "height = (int) [ 49, 4095 ], "
        "stream-format = (string) byte-stream, "
        "alignment = (string) au, "
        "profile = (string) main; "
        "video/x-av1, "  /* AV1 */
        "framerate = (fraction) [ 0, MAX ], "
        "width = (int) [ 192, 4096 ], " "height = (int) [ 128, 4095 ], "
        "stream-format = (string) obu-stream, "
        "alignment = (string) tu, "
        "profile = (string) main; "
        "video/x-raw, "  /* RAW */
        "format = (string) BGRx, "
        "framerate = (fraction) [ 0, MAX ], "
        "width = (int) [ 145, 4096 ], " "height = (int) [ 49, 4095 ]"));

enum
{
        PROP_0,
        PROP_DISPLAY_NAME,
        PROP_SHOW_POINTER,
        PROP_BITRATE,
        PROP_FPS,
};

#define gst_nvimage_src_parent_class parent_class
G_DEFINE_TYPE (GstNVimageSrc, gst_nvimage_src, GST_TYPE_PUSH_SRC);

static GstCaps *gst_nvimage_src_fixate (GstBaseSrc * bsrc, GstCaps * caps);

static gboolean
gst_nvimage_src_open_display (GstNVimageSrc * s, const gchar * name)
{
        g_return_val_if_fail (GST_IS_NVIMAGE_SRC (s), FALSE);

        if (s->xcontext != NULL)
                return TRUE;

        s->xcontext = nvimageutil_xcontext_get_r (GST_ELEMENT (s), name);
        if (s->xcontext == NULL) {
                return FALSE;
        }
        s->width = s->xcontext->width;
        s->height = s->xcontext->height;

        if (s->xcontext == NULL)
                return FALSE;

        return TRUE;
}

static gboolean
gst_nvimage_src_start (GstBaseSrc * basesrc)
{
        GstNVimageSrc *s = GST_NVIMAGE_SRC (basesrc);

        s->last_frame_no = -1;
        s->frame = 0;

        if (!nvimage_load_libraries ()) {
                GST_ELEMENT_ERROR (s, LIBRARY, INIT,
                                   ("NVIDIA driver libraries not found"),
                                   ("Could not load libnvidia-fbc.so.1 / libnvidia-encode.so.1"));
                return FALSE;
        }

        return gst_nvimage_src_open_display (s, s->display_name);
}

static gboolean
gst_nvimage_src_stop (GstBaseSrc * basesrc)
{
        GstNVimageSrc *src = GST_NVIMAGE_SRC (basesrc);

        src->frame = 0;
        nvimageutil_xcontext_clear_r (src->xcontext);
        src->xcontext = NULL;
        return TRUE;
}

static gboolean
gst_nvimage_src_unlock (GstBaseSrc * basesrc)
{
        GstNVimageSrc *src = GST_NVIMAGE_SRC (basesrc);

        /* Awaken the create() func if it's waiting on the clock */
        GST_OBJECT_LOCK (src);
        if (src->clock_id) {
                GST_DEBUG_OBJECT (src, "Waking up waiting clock");
                gst_clock_id_unschedule (src->clock_id);
        }
        GST_OBJECT_UNLOCK (src);

        return TRUE;
}

static GstFlowReturn
gst_nvimage_src_create (GstPushSrc * bs, GstBuffer ** buf)
{
        GstNVimageSrc *s = GST_NVIMAGE_SRC (bs);
        GstBuffer *image;
        GstClockTime base_time;
        GstClockTime next_capture_ts, pts;
        GstClockTime dur;
        gint64 next_frame_no;
	gint32 _keyframe;

        GST_DEBUG_OBJECT (s, "Nvimage src create");

        if (s->fps_n <= 0 || s->fps_d <= 0) {
                GST_DEBUG_OBJECT (s, "Flow not negotiated, fps == 0");
                return GST_FLOW_NOT_NEGOTIATED;     /* FPS must be > 0 */
        }

        if (s->frame == 0) {
                sleep(2);
        }

        /* Now, we might need to wait for the next multiple of the fps
         * before capturing */

        GST_OBJECT_LOCK (s);
        if (GST_ELEMENT_CLOCK (s) == NULL) {
                GST_OBJECT_UNLOCK (s);
                GST_ELEMENT_ERROR (s, RESOURCE, FAILED,
                                        (("Cannot operate without a clock")), (NULL));
                return GST_FLOW_ERROR;
        }

        base_time = GST_ELEMENT_CAST (s)->base_time;
        pts = next_capture_ts = gst_clock_get_time (GST_ELEMENT_CLOCK (s));
        next_capture_ts -= base_time;

        /* Figure out which 'frame number' position we're at, based on the cur time
         * and frame rate */
        next_frame_no = gst_util_uint64_scale (next_capture_ts, s->fps_n, GST_SECOND * s->fps_d);
        if (next_frame_no == s->last_frame_no) {
                GstClockID id;
                GstClockReturn ret;

                /* Need to wait for the next frame */
                next_frame_no += 1;

                /* Figure out what the next frame time is */
                next_capture_ts = gst_util_uint64_scale (next_frame_no,
                        s->fps_d * GST_SECOND, s->fps_n);

                id = gst_clock_new_single_shot_id (GST_ELEMENT_CLOCK (s), next_capture_ts + base_time);
                s->clock_id = id;

                /* release the object lock while waiting */
                GST_OBJECT_UNLOCK (s);

                GST_DEBUG_OBJECT (s, "Waiting for next frame time %" G_GUINT64_FORMAT, next_capture_ts);
                ret = gst_clock_id_wait (id, NULL);
                GST_OBJECT_LOCK (s);

                gst_clock_id_unref (id);
                s->clock_id = NULL;
                if (ret == GST_CLOCK_UNSCHEDULED) {
                        /* Got woken up by the unlock function */
                        GST_OBJECT_UNLOCK (s);
                        return GST_FLOW_FLUSHING;
                }
                /* Duration is a complete 1/fps frame duration */
                dur = gst_util_uint64_scale_int (GST_SECOND, s->fps_d, s->fps_n);
        } else {
                GstClockTime next_frame_ts;

                GST_DEBUG_OBJECT (s, "No need to wait for next frame time %"
                        G_GUINT64_FORMAT " next frame = %" G_GINT64_FORMAT " prev = %"
                        G_GINT64_FORMAT, next_capture_ts, next_frame_no, s->last_frame_no);
                next_frame_ts = gst_util_uint64_scale (next_frame_no + 1, s->fps_d * GST_SECOND, s->fps_n);
                /* Frame duration is from now until the next expected capture time */
                dur = next_frame_ts - next_capture_ts;
        }
        //dur = gst_util_uint64_scale_int (GST_SECOND, s->fps_d, s->fps_n);
        s->last_frame_no = next_frame_no;
        GST_OBJECT_UNLOCK (s);

	_keyframe = s->keyframe;

        image = gst_nvimageutil_nvimage_new_r(s->xcontext, GST_ELEMENT(s), s->codec,
                                            s->fps_n, s->fps_d, s->bitrate, s->show_pointer, _keyframe, 
                                            next_frame_no, next_capture_ts);

        if(_keyframe) {
                s->keyframe = 0;
        }

        if (!image)
                return GST_FLOW_ERROR;

        if (s->xcontext->width != s->width || s->xcontext->height != s->height) {
                GST_INFO_OBJECT (s, "Screen size changed to %dx%d",
                        s->xcontext->width, s->xcontext->height);
                s->width = s->xcontext->width;
                s->height = s->xcontext->height;
                if (!gst_base_src_negotiate (GST_BASE_SRC (s))) {
                        gst_buffer_unref (image);
                        return GST_FLOW_NOT_NEGOTIATED;
                }
        }

        *buf = image;
        GST_BUFFER_DTS (*buf) = GST_CLOCK_TIME_NONE; //pts+s->last_frame_no;
        GST_BUFFER_PTS (*buf) = next_capture_ts; //pts+s->last_frame_no; // next_capture_ts;
        GST_BUFFER_DURATION (*buf) = dur;

        GST_DEBUG_OBJECT (s, "Sending frame time %"
                        GST_TIME_FORMAT " duration %ld next frame = %" G_GINT64_FORMAT " prev = %"
                        G_GINT64_FORMAT, GST_TIME_ARGS(pts+s->last_frame_no), dur, next_frame_no, s->last_frame_no);

        s->frame++;

        return GST_FLOW_OK;
}

static void
gst_nvimage_src_set_property (GObject * object, guint prop_id, const GValue * value, GParamSpec * pspec)
{
        GstNVimageSrc *src = GST_NVIMAGE_SRC (object);
        gdouble fps;

        switch (prop_id) {
                case PROP_DISPLAY_NAME:
                        g_free (src->display_name);
                        src->display_name = g_strdup (g_value_get_string (value));
                        break;
                case PROP_SHOW_POINTER:
                        src->show_pointer = g_value_get_boolean (value);
                        break;
                case PROP_BITRATE:
                        src->bitrate = g_value_get_uint (value);
                        break;
                case PROP_FPS:
                        fps = g_value_get_double(value);
                        if (fps == (guint)fps) {
                                src->fps_n = (guint)fps;
                                src->fps_d = 1;
                        } else {
                                src->fps_n = fps*1000;
                                src->fps_d = 1000;
                        }
                        break;
                default:
                        G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
                        break;
        }
}

static void
gst_nvimage_src_get_property (GObject * object, guint prop_id, GValue * value, GParamSpec * pspec)
{
        GstNVimageSrc *src = GST_NVIMAGE_SRC (object);

        switch (prop_id) {
                case PROP_DISPLAY_NAME:
                        if (src->xcontext)
                                g_value_set_string (value, DisplayString (src->xcontext->disp));
                        else
                                g_value_set_string (value, src->display_name);

                        break;
                case PROP_SHOW_POINTER:
                        g_value_set_boolean (value, src->show_pointer);
                        break;
                case PROP_BITRATE:
                        g_value_set_uint (value, src->bitrate);
                        break;
                case PROP_FPS:
                        g_value_set_double(value, ((double)src->fps_n) / src->fps_d);
                        break;
                default:
                        G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
                        break;
        }
}

static void
gst_nvimage_src_dispose (GObject * object)
{
        G_OBJECT_CLASS (parent_class)->dispose (object);
}

static void
gst_nvimage_src_finalize (GObject * object)
{
        GstNVimageSrc *src = GST_NVIMAGE_SRC (object);

        if (src->xcontext)
                nvimageutil_xcontext_clear_r (src->xcontext);

        G_OBJECT_CLASS (parent_class)->finalize (object);
}

static GstCaps *
gst_nvimage_src_get_caps (GstBaseSrc * bs, GstCaps * filter)
{
        GstNVimageSrc *s = GST_NVIMAGE_SRC (bs);
        GstCaps *caps;
        gint width, height;

        if ((!s->xcontext) || (!gst_nvimage_src_open_display (s, s->display_name)))
                return gst_pad_get_pad_template_caps (GST_BASE_SRC (s)->srcpad);

        width = s->xcontext->width;
        height = s->xcontext->height;

        GST_DEBUG ("width = %d, height=%d", width, height);

        caps = gst_caps_new_empty ();

        if (nvimageutil_codec_supported (s->xcontext, GST_NVIMAGE_CODEC_H264)) {
                gst_caps_append_structure (caps, gst_structure_new ("video/x-h264",
                        "width", G_TYPE_INT, width,
                        "height", G_TYPE_INT, height,
                        "framerate", GST_TYPE_FRACTION_RANGE, 1, G_MAXINT, G_MAXINT, 1,
                        "stream-format", G_TYPE_STRING, "byte-stream",
                        "alignment", G_TYPE_STRING, "au",
                        "profile", G_TYPE_STRING, "high",
                        NULL));
        }
        if (nvimageutil_codec_supported (s->xcontext, GST_NVIMAGE_CODEC_H265)) {
                gst_caps_append_structure (caps, gst_structure_new ("video/x-h265",
                        "width", G_TYPE_INT, width,
                        "height", G_TYPE_INT, height,
                        "framerate", GST_TYPE_FRACTION_RANGE, 1, G_MAXINT, G_MAXINT, 1,
                        "stream-format", G_TYPE_STRING, "byte-stream",
                        "alignment", G_TYPE_STRING, "au",
                        "profile", G_TYPE_STRING, "main",
                        NULL));
        }
        if (nvimageutil_codec_supported (s->xcontext, GST_NVIMAGE_CODEC_AV1)) {
                gst_caps_append_structure (caps, gst_structure_new ("video/x-av1",
                        "width", G_TYPE_INT, width,
                        "height", G_TYPE_INT, height,
                        "framerate", GST_TYPE_FRACTION_RANGE, 1, G_MAXINT, G_MAXINT, 1,
                        "stream-format", G_TYPE_STRING, "obu-stream",
                        "alignment", G_TYPE_STRING, "tu",
                        "profile", G_TYPE_STRING, "main",
                        NULL));
        }
        if (nvimageutil_codec_supported (s->xcontext, GST_NVIMAGE_CODEC_RAW)) {
                gst_caps_append_structure (caps, gst_structure_new ("video/x-raw",
                        "format", G_TYPE_STRING, "BGRx",
                        "width", G_TYPE_INT, width,
                        "height", G_TYPE_INT, height,
                        "framerate", GST_TYPE_FRACTION_RANGE, 1, G_MAXINT, G_MAXINT, 1,
                        NULL));
        }

        if (filter) {
                GstCaps *tmp = gst_caps_intersect_full (filter, caps, GST_CAPS_INTERSECT_FIRST);
                gst_caps_unref (caps);
                caps = tmp;
        }

        return caps;
}

static gboolean
gst_nvimage_src_set_caps (GstBaseSrc * bs, GstCaps * caps)
{
        GstNVimageSrc *s = GST_NVIMAGE_SRC (bs);
        GstStructure *structure;
        const GValue *new_fps;

        /* If not yet opened, disallow setcaps until later */
        if (!s->xcontext)
                return FALSE;

        /* The only thing that can change is the framerate downstream wants */
        structure = gst_caps_get_structure (caps, 0);
        new_fps = gst_structure_get_value (structure, "framerate");
        if (!new_fps)
                return FALSE;

        /* Store this FPS for use when generating buffers */
        s->fps_n = gst_value_get_fraction_numerator (new_fps);
        s->fps_d = gst_value_get_fraction_denominator (new_fps);

        GST_DEBUG_OBJECT (s, "peer wants %d/%d fps", s->fps_n, s->fps_d);

        if (gst_structure_has_name (structure, "video/x-raw"))
                s->codec = GST_NVIMAGE_CODEC_RAW;
        else if (gst_structure_has_name (structure, "video/x-av1"))
                s->codec = GST_NVIMAGE_CODEC_AV1;
        else if (gst_structure_has_name (structure, "video/x-h265"))
                s->codec = GST_NVIMAGE_CODEC_H265;
        else
                s->codec = GST_NVIMAGE_CODEC_H264;

        GST_DEBUG_OBJECT (s, "negotiated %s", gst_structure_get_name (structure));

        return TRUE;
}

static GstCaps *
gst_nvimage_src_fixate (GstBaseSrc * bsrc, GstCaps * caps)
{
        gint i;
        GstStructure *structure;

        caps = gst_caps_make_writable (caps);

        for (i = 0; i < gst_caps_get_size (caps); ++i) {
                structure = gst_caps_get_structure (caps, i);

                gst_structure_fixate_field_nearest_fraction (structure, "framerate", 25, 1);
        }
        caps = GST_BASE_SRC_CLASS (parent_class)->fixate (bsrc, caps);

        return caps;
}

static gboolean
gst_nvimage_src_event (GstBaseSrc * bsrc, GstEvent * event) {
        GstNVimageSrc *nvs = GST_NVIMAGE_SRC (bsrc);

        if (gst_video_event_is_force_key_unit (event)) {
                GST_DEBUG_OBJECT (nvs, "Forcing keyframe");
                nvs->keyframe = 1;
        }
        return TRUE;
}

static void
gst_nvimage_src_class_init (GstNVimageSrcClass * klass)
{
        GObjectClass *gc = G_OBJECT_CLASS (klass);
        GstElementClass *ec = GST_ELEMENT_CLASS (klass);
        GstBaseSrcClass *bc = GST_BASE_SRC_CLASS (klass);
        GstPushSrcClass *push_class = GST_PUSH_SRC_CLASS (klass);

        gc->set_property = gst_nvimage_src_set_property;
        gc->get_property = gst_nvimage_src_get_property;
        gc->dispose = gst_nvimage_src_dispose;
        gc->finalize = gst_nvimage_src_finalize;

        g_object_class_install_property (gc, PROP_DISPLAY_NAME,
                                                g_param_spec_string ("display-name", "Display", "X Display Name",
                                                NULL, G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

        g_object_class_install_property (gc, PROP_SHOW_POINTER,
                                                g_param_spec_boolean ("show-pointer", "Show Mouse Pointer",
                                                "Show mouse pointer (if XFixes extension enabled)", TRUE,
                                                G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

        g_object_class_install_property (gc, PROP_BITRATE,
                                                g_param_spec_uint ("bitrate", "videobitrate", "Desired video bitrate",
                                                0, G_MAXINT, 0, G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

        g_object_class_install_property (gc, PROP_FPS,
                                                g_param_spec_double ("fps", "fps", "Desired grabbing fps",
                                                0, 1000, 0, G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

        gst_element_class_set_static_metadata (ec, "NVimage video source",
                                              "Source/Video",
                                              "Creates a screenshot video stream as raw, H.264, H.265 or AV1 video",
                                              "Lukas Hejtmanek <xhejtman@gmail.com>");
        gst_element_class_add_static_pad_template (ec, &t);

        bc->fixate = gst_nvimage_src_fixate;
        bc->get_caps = gst_nvimage_src_get_caps;
        bc->set_caps = gst_nvimage_src_set_caps;
        bc->start = gst_nvimage_src_start;
        bc->stop = gst_nvimage_src_stop;
        bc->unlock = gst_nvimage_src_unlock;
        bc->event = gst_nvimage_src_event;
        push_class->create = gst_nvimage_src_create;
}

static void
gst_nvimage_src_init (GstNVimageSrc * nvimagesrc)
{
        gst_base_src_set_format (GST_BASE_SRC (nvimagesrc), GST_FORMAT_TIME);
        gst_base_src_set_live (GST_BASE_SRC (nvimagesrc), TRUE);

        nvimagesrc->codec = GST_NVIMAGE_CODEC_H264;
        nvimagesrc->show_pointer = TRUE;
        nvimagesrc->bitrate = 2000000;
        nvimagesrc->keyframe = TRUE;
        nvimagesrc->frame = 0;
}

static gboolean
plugin_init (GstPlugin * plugin)
{
        gboolean ret;

        GST_DEBUG_CATEGORY_INIT (gst_debug_nvimage_src, "nvimagesrc", 0,
                                        "nvimagesrc element debug");

        ret = gst_element_register (plugin, "nvimagesrc", GST_RANK_NONE, GST_TYPE_NVIMAGE_SRC);

        return ret;
}

GST_PLUGIN_DEFINE (GST_VERSION_MAJOR,
    GST_VERSION_MINOR,
    nvimagesrc,
    "X11 video input plugin using standard NVIDIA FBC calls",
    plugin_init, VERSION, GST_LICENSE, GST_PACKAGE_NAME, GST_PACKAGE_ORIGIN);
