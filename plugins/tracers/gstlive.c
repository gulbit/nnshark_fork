/* GstShark - A Front End for GstTracer
 * Copyright (C) 2018 RidgeRun Engineering <michael.gruner@ridgerun.com>
 *
 * This file is part of GstShark.
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301  USA
 */

/**
 * SECTION:gstlive
 * @short_description: display live profiler
 *
 * A tracing module that uses the DOT libraries in order to show the pipeline executed liveally
 */

#include <unistd.h>
#include "gstlive.h"
#include "gstdot.h"
#include "gstctf.h"
#include "gstcpuusagecompute.h"
#include "gstgpuusagecompute.h"
#include "gstddrusagecompute.h"
#include "gstpwrusagecompute.h"
#include "gstperiodictracer.h"
#include "gstliveprofiler.h"

#include <stdio.h>
#include <sys/time.h>

GST_DEBUG_CATEGORY_STATIC (gst_live_debug);
#define GST_CAT_DEFAULT gst_live_debug
GST_DEBUG_CATEGORY_STATIC (GST_CAT_STATES);

extern long initial_tv_sec;
extern long initial_tv_usec;

struct _GstLiveTracer
{
  GstTracer parent;
  GstCPUUsage cpu_usage;
  GstGPUUsage gpu_usage;
  GstDDRUsage ddr_usage;
  GstPWRUsage pwr_usage;
  gboolean event_running;
};

#define _do_init \
    GST_DEBUG_CATEGORY_INIT (gst_live_debug, "live", 0, "live tracer"); \
    GST_DEBUG_CATEGORY_GET (GST_CAT_STATES, "GST_STATES");

G_DEFINE_TYPE_WITH_CODE (GstLiveTracer, gst_live_tracer,
    GST_SHARK_TYPE_TRACER, _do_init);

#define PERIODIC_INTERVAL 1

static gboolean
do_periodic (GObject * obj)
{
  GstLiveTracer *self = GST_LIVE_TRACER (obj);
  GstCPUUsage *cpu_usage;
  GstGPUUsage *gpu_usage;
  gfloat *cpu_load;
  gint cpu_load_len;
  gfloat *gpu_load;
  gint gpu_load_len;
  gchar **gpu_names;

  GstDDRUsage *ddr_usage;
  gfloat *ddr_load;
  gint ddr_load_len;
  gchar **ddr_names;

  GstPWRUsage *pwr_usage;
  gfloat *pwr_value;
  gint pwr_value_len;
  gchar **pwr_names;

  cpu_usage = &self->cpu_usage;

  cpu_load = CPU_USAGE_ARRAY (cpu_usage);
  cpu_load_len = CPU_USAGE_ARRAY_LENGTH (cpu_usage);

  //cpu_usage->cpu_array_sel = FALSE;
  gst_cpu_usage_compute (cpu_usage);

  update_cpuusage_event (cpu_load_len, cpu_load);

  gpu_usage = &self->gpu_usage;
  gpu_load = GPU_USAGE_ARRAY (gpu_usage);
  gpu_load_len = GPU_USAGE_ARRAY_LENGTH (gpu_usage);
  gpu_names = GPU_EVENT_NAME_ARRAY (gpu_usage);

  gst_gpu_usage_compute (gpu_usage);

  update_gpuusage_event (gpu_load_len, gpu_load, gpu_names);

  ddr_usage = &self->ddr_usage;
  ddr_load = DDR_USAGE_ARRAY (ddr_usage);
  ddr_load_len = DDR_USAGE_ARRAY_LENGTH (ddr_usage);
  ddr_names = DDR_EVENT_NAME_ARRAY (ddr_usage);

  gst_ddr_usage_compute (ddr_usage);
  update_ddrusage_event (ddr_load_len, ddr_load, ddr_names);


  pwr_usage = &self->pwr_usage;;
  pwr_value = PWR_USAGE_ARRAY (pwr_usage);
  pwr_value_len = PWR_USAGE_ARRAY_LENGTH (pwr_usage);;
  pwr_names = PWR_EVENT_NAME_ARRAY (pwr_usage);

  gst_pwr_usage_compute (pwr_usage);
  update_pwrusage_event (pwr_value_len, pwr_value, pwr_names);

  return TRUE;
}

static void
do_element_change_state_post (GObject * self, guint64 ts,
    GstElement * element, GstStateChange transition,
    GstStateChangeReturn result)
{
  GstLiveTracer *tracer = GST_LIVE_TRACER (self);

  if (FALSE == GST_IS_PIPELINE (element)) {
    return;
  }

  if (transition == GST_STATE_CHANGE_PAUSED_TO_PLAYING
      && result == GST_STATE_CHANGE_SUCCESS) {
    update_pipeline_init ((GstPipeline *) element);
    if (!tracer->event_running) {
      gst_cpu_usage_init (&(tracer->cpu_usage));
      gst_gpu_usage_init (&(tracer->gpu_usage));
      gst_ddr_usage_init (&(tracer->ddr_usage));
      gst_pwr_usage_init (&(tracer->pwr_usage));
      g_timeout_add_seconds (PERIODIC_INTERVAL,
          (GSourceFunc) do_periodic, (gpointer) tracer);
      tracer->event_running = TRUE;
    }
  } else if (transition == GST_STATE_CHANGE_PLAYING_TO_PAUSED) {
    update_pipeline_finalize ((GstPipeline *) element);
    tracer->event_running = FALSE;
  }
}

static void
do_pad_push_pre (GstTracer * self, guint64 ts, GstPad * pad, GstBuffer * buffer)
{
  gchar *element_name = GST_OBJECT_NAME (GST_OBJECT_PARENT (pad));
  gchar *pad_name = GST_OBJECT_NAME (pad);
  long time_diff;
  long time_diff_usec;

  if (g_getenv ("LOG_ENABLED")) {
    char file_name[50];
    char text[50];
    struct timeval current_time;
    sprintf (file_name, "buffer-%s-%s", element_name, pad_name);
    gettimeofday (&current_time, NULL);

    // save time_diff for 0.0001s unit
    time_diff = current_time.tv_sec - initial_tv_sec;
    time_diff_usec = current_time.tv_usec - initial_tv_usec;
    if (time_diff_usec < 0) {
      time_diff -= 1;
      time_diff_usec += 1000000;
    }

    sprintf (text, "%ld.%06ld %ld", time_diff, time_diff_usec, buffer->offset);
    do_print_log (file_name, text);
  }

  element_push_buffer_pre (element_name, pad_name, ts, buffer);
}

static void
do_pad_push_post (GstTracer * self, guint64 ts, GstPad * pad)
{
  gchar *element_name = GST_OBJECT_NAME (GST_OBJECT_PARENT (pad));
  gchar *pad_name = GST_OBJECT_NAME (pad);
  element_push_buffer_post (element_name, pad_name, ts);
}

static void
do_pad_push_list_pre (GstTracer * self, guint64 ts, GstPad * pad)
{
  gchar *element_name = GST_OBJECT_NAME (GST_OBJECT_PARENT (pad));
  gchar *pad_name = GST_OBJECT_NAME (pad);
  element_push_buffer_list_pre (element_name, pad_name, ts);
}

static void
do_pad_push_list_post (GstTracer * self, guint64 ts, GstPad * pad)
{
  gchar *element_name = GST_OBJECT_NAME (GST_OBJECT_PARENT (pad));
  gchar *pad_name = GST_OBJECT_NAME (pad);
  element_push_buffer_list_post (element_name, pad_name, ts);
}

static void
do_pad_pull_range_pre (GstTracer * self, guint64 ts, GstPad * pad)
{
  gchar *element_name = GST_OBJECT_NAME (GST_OBJECT_PARENT (pad));
  gchar *pad_name = GST_OBJECT_NAME (pad);
  element_pull_range_pre (element_name, pad_name, ts);
}

static void
do_pad_pull_range_post (GstTracer * self, guint64 ts, GstPad * pad)
{
  gchar *element_name = GST_OBJECT_NAME (GST_OBJECT_PARENT (pad));
  gchar *pad_name = GST_OBJECT_NAME (pad);
  element_pull_range_post (element_name, pad_name, ts);
}

static void gst_live_tracer_finalize (GObject * obj);

/* tracer class */

static void
gst_live_tracer_finalize (GObject * obj)
{
  g_unsetenv ("LIVEPROFILER_ENABLED");
  gst_liveprofiler_finalize ();
  gst_ddr_usage_finalize ();
  gst_pwr_usage_finalize ();
  G_OBJECT_CLASS (gst_live_tracer_parent_class)->finalize (obj);
}

static void
gst_live_tracer_class_init (GstLiveTracerClass * klass)
{
  gint cpu_num;
  gint gpu_num;
  gint ddr_num;
  gint pwr_num;
  GObjectClass *g_obj_class = G_OBJECT_CLASS (klass);

  if ((cpu_num = sysconf (_SC_NPROCESSORS_CONF)) == -1) {
    GST_WARNING ("Failed to get numbers of cpus");
    cpu_num = 1;
  }

  gpu_num = gst_gpu_usage_get_ngpus ();
  ddr_num = gst_ddr_usage_get_nmeas ();
  pwr_num = gst_pwr_usage_get_nmeas ();

  gst_liveprofiler_init (cpu_num, gpu_num, ddr_num, pwr_num);
  g_setenv ("LIVEPROFILER_ENABLED", "TRUE", TRUE);
  g_obj_class->finalize = gst_live_tracer_finalize;
}

static void
gst_live_tracer_init (GstLiveTracer * self)
{
  GstTracer *tracer = GST_TRACER (self);
  GstCPUUsage *cpu_usage;
  GstGPUUsage *gpu_usage;
  GstDDRUsage *ddr_usage;

  cpu_usage = &self->cpu_usage;
  gst_cpu_usage_init (cpu_usage);
  cpu_usage->cpu_array_sel = FALSE;

  gpu_usage = &self->gpu_usage;
  gst_gpu_usage_init (gpu_usage);

  ddr_usage = &self->ddr_usage;
  gst_ddr_usage_init (ddr_usage);

  self->event_running = FALSE;


  gst_tracing_register_hook (tracer, "element-change-state-post",
      G_CALLBACK (do_element_change_state_post));
  gst_tracing_register_hook (tracer, "pad-push-pre",
      G_CALLBACK (do_pad_push_pre));
  gst_tracing_register_hook (tracer, "pad-push-post",
      G_CALLBACK (do_pad_push_post));
  gst_tracing_register_hook (tracer, "pad-push-list-pre",
      G_CALLBACK (do_pad_push_list_pre));
  gst_tracing_register_hook (tracer, "pad-push-list-post",
      G_CALLBACK (do_pad_push_list_post));
  gst_tracing_register_hook (tracer, "pad-pull-range-pre",
      G_CALLBACK (do_pad_pull_range_pre));
  gst_tracing_register_hook (tracer, "pad-pull-range-post",
      G_CALLBACK (do_pad_pull_range_post));
}
