/* GStreamer
 * Copyright (C) <2005> Luca Ognibene <luogni@tin.it>
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

#ifndef __GST_NVIMAGEUTIL_H__
#define __GST_NVIMAGEUTIL_H__

#include <stdio.h>

#include <gmodule.h>

#include <gst/gst.h>

#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include <GL/gl.h>
#include <GL/glx.h>
#include <pthread.h>


#include "NvFBC.h"
#include "NvFBCUtils.h"
#include "nvEncodeAPI.h"

G_BEGIN_DECLS

typedef enum { GST_NVIMAGE_CODEC_H264, GST_NVIMAGE_CODEC_H265, GST_NVIMAGE_CODEC_RAW } GstNVimageCodec;
typedef struct _GstXContext GstXContext;
typedef struct _GstNVimage GstNVimage;
typedef struct _GstMetaNVimage GstMetaNVimage;

typedef struct {
        int function;
        union {
          GstElement * parent;
          const gchar * display_name;
          GstNVimageCodec codec;
          uint fps_n; 
          guint fps_d; 
          gint bitrate;
          gboolean show_pointer;
          gint forcekeyframe;
          gint64 frame; 
          gint64 ts;
        } args[10];       
        union {
           gboolean b;
           GstBuffer *buf;
        } retval;
        gboolean retvalid;
        gboolean inputvalid;
} GstXThreadCall;

/* Global X Context stuff */
/**
 * GstXContext:
 * @disp: the X11 Display of this context
 * @screen: the default Screen of Display @disp
 * @visual: the default Visual of Screen @screen
 * @root: the root Window of Display @disp
 * @white: the value of a white pixel on Screen @screen
 * @black: the value of a black pixel on Screen @screen
 * @depth: the color depth of Display @disp
 * @bpp: the number of bits per pixel on Display @disp
 * @endianness: the endianness of image bytes on Display @disp
 * @width: the width in pixels of Display @disp
 * @height: the height in pixels of Display @disp
 * @widthmm: the width in millimeters of Display @disp
 * @heightmm: the height in millimeters of Display @disp
 * @par_n: the pixel aspect ratio numerator calculated from @width, @widthmm
 * and @height,
 * @par_d: the pixel aspect ratio denumerator calculated from @width, @widthmm
 * and @height,
 * @heightmm ratio
 * @use_xshm: used to known wether of not XShm extension is usable or not even
 * if the Extension is present
 * @caps: the #GstCaps that Display @disp can accept
 *
 * Structure used to store various information collected/calculated for a
 * Display.
 */
struct _GstXContext {
  Display *disp;

  Screen *screen;

  gint width, height;

  GstNVimageCodec codec;
  void *sysBuffer;              /* NVFBC system-memory frame, raw mode only */

  guint fps_n;                  
  guint fps_d;                 
  gint goplen;
  guint bitrate;
  gboolean show_pointer;

  GLXContext glxctx;
  Pixmap pixmap;
  GLXPixmap glxpixmap;
  GLXFBConfig fbconfig;

  NVFBC_API_FUNCTION_LIST pFn;
  NVFBC_SESSION_HANDLE fbcHandle;
  NV_ENCODE_API_FUNCTION_LIST pEncFn;
  void *encoder;

  NV_ENC_MAP_INPUT_RESOURCE mapParams;
  NV_ENC_OUTPUT_PTR outputBuffer;
  NV_ENC_PIC_PARAMS encParams;
  NVFBC_TOGL_SETUP_PARAMS setupParams;
  NV_ENC_REGISTERED_PTR registeredResources[NVFBC_TOGL_TEXTURES_MAX];

  pthread_t worker_tid;
  gboolean finish;
  pthread_mutex_t mutex_in;
  pthread_mutex_t mutex_out;
  pthread_cond_t cond_in;
  pthread_cond_t cond_out;
  GstXThreadCall funcdata;

  FILE *out;
};

gboolean nvimage_load_libraries(void);

GstXContext *nvimageutil_xcontext_get_r (GstElement *parent, const gchar *display_name);
void nvimageutil_xcontext_clear_r (GstXContext *xcontext);

/* custom nvimagesrc buffer, copied from nvimagesink */

/**
 * GstMetaNVimage:
 * @parent: a reference to the element we belong to
 * @nvimage: the NVimage of this buffer
 * @width: the width in pixels of NVimage @nvimage
 * @height: the height in pixels of NVimage @nvimage
 * @size: the size in bytes of NVimage @nvimage
 *
 * Extra data attached to buffers containing additional information about an NVimage.
 */
struct _GstMetaNVimage {
  GstMeta meta;

  /* Reference to the nvimagesrc we belong to */
  GstElement *parent;
  void *data;
  gint width, height;
  size_t size;
};

GType gst_meta_nvimage_api_get_type (void);
const GstMetaInfo * gst_meta_nvimage_get_info (void);
#define GST_META_NVIMAGE_GET(buf) ((GstMetaNVimage *)gst_buffer_get_meta(buf,gst_meta_nvimage_api_get_type()))
#define GST_META_NVIMAGE_ADD(buf) ((GstMetaNVimage *)gst_buffer_add_meta(buf,gst_meta_nvimage_get_info(),NULL))

GstBuffer * gst_nvimageutil_nvimage_new_r (GstXContext * xcontext, GstElement * parent,
  GstNVimageCodec codec, guint fps_n, guint fps_d, gint bitrate, gboolean show_pointer, gint forcekeyframe, gint64 frame, gint64 ts);

void gst_nvimageutil_nvimage_destroy (GstXContext * xcontext, GstBuffer * nvimage);


G_END_DECLS 

#endif /* __GST_NVIMAGEUTIL_H__ */
