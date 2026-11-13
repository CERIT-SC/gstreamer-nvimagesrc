#!/bin/bash
set -e

OPT="-O2"

cc -I. -I/src/gstreamer/subprojects/gst-plugins-base/gst-libs -I/opt/gstreamer/include/gstreamer-1.0 -I/usr/include/glib-2.0 -I/usr/lib/x86_64-linux-gnu/glib-2.0/include -fdiagnostics-color=always -D_FILE_OFFSET_BITS=64 -Wall -Winvalid-pch $OPT -g -fvisibility=hidden -fno-strict-aliasing -DG_DISABLE_DEPRECATED -Wmissing-declarations -Wredundant-decls -Wwrite-strings -Winit-self -Wmissing-include-dirs -Wno-multichar -Wvla -Wpointer-arith -Wmissing-prototypes -Wdeclaration-after-statement -Wold-style-definition -Waggregate-return -fPIC -pthread -DHAVE_CONFIG_H -MD -MQ nvimageutil.c.o -MF nvimageutil.c.o.d -o nvimageutil.c.o -c nvimageutil.c

cc -I. -I/src/gstreamer/subprojects/gst-plugins-base/gst-libs -I/opt/gstreamer/include/gstreamer-1.0 -I/usr/include/glib-2.0 -I/usr/lib/x86_64-linux-gnu/glib-2.0/include -fdiagnostics-color=always -D_FILE_OFFSET_BITS=64 -Wall -Winvalid-pch $OPT -g -fvisibility=hidden -fno-strict-aliasing -DG_DISABLE_DEPRECATED -Wmissing-declarations -Wredundant-decls -Wwrite-strings -Winit-self -Wmissing-include-dirs -Wno-multichar -Wvla -Wpointer-arith -Wmissing-prototypes -Wdeclaration-after-statement -Wold-style-definition -Waggregate-return -fPIC -pthread -DHAVE_CONFIG_H -MD -MQ gstnvimagesrc.c.o -MF gstnvimagesrc.c.o.d -o gstnvimagesrc.c.o -c gstnvimagesrc.c

cc  -o libgstnvimagesrc.so gstnvimagesrc.c.o nvimageutil.c.o -Wl,--as-needed -Wl,--no-undefined -shared -fPIC -Wl,--start-group -Wl,-soname,libgstnvimagesrc.so -Wl,-Bsymbolic-functions /opt/gstreamer/lib/x86_64-linux-gnu/libgstbase-1.0.so /opt/gstreamer/lib/x86_64-linux-gnu/libgstreamer-1.0.so /usr/lib/x86_64-linux-gnu/libgobject-2.0.so /usr/lib/x86_64-linux-gnu/libglib-2.0.so /opt/gstreamer/lib/x86_64-linux-gnu/libgstvideo-1.0.so /usr/lib/x86_64-linux-gnu/libX11.so /usr/lib/x86_64-linux-gnu/libgmodule-2.0.so -lGL -lpthread -Wl,--end-group
