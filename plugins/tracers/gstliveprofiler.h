#include <gst/gst.h>
#include "gstliveunit.h"

gboolean gst_liveprofiler_init (gint cpu_num, gint gpu_num, gint ddr_num);
gboolean gst_liveprofiler_finalize (void);

void print_element (gpointer key, gpointer value, gpointer user_data);
void print_connection (gpointer key, gpointer value, gpointer user_data);

void update_cpuusage_event (guint32 cpunum, gfloat * cpuload);
void update_gpuusage_event (guint32 gpunum, gfloat * gpuload, gchar **names);
void update_ddrusage_event (guint32 ddrnum, gfloat * gpuload, gchar **names);

void update_pipeline_init (GstPipeline * element);
void update_pipeline_finalize (GstPipeline * element);

void update_proctime (ElementUnit * element, ElementUnit * peerElement,
    guint64 ts, guint64 offset);
void update_queue_level (ElementUnit * element);
void update_datatrate (PadUnit * pad, PadUnit * peerPad, guint64 ts);
void update_buffer_size (PadUnit * pad, PadUnit * peerPad, guint64 size);

void element_push_buffer_pre (gchar * elementname, gchar * padname, guint64 ts,
    GstBuffer * buffer);
void element_push_buffer_post (gchar * elementname, gchar * padname,
    guint64 ts);
void element_push_buffer_list_pre (gchar * elementname, gchar * padname,
    guint64 ts);
void element_push_buffer_list_post (gchar * elementname, gchar * padname,
    guint64 ts);
void element_pull_range_pre (gchar * elementname, gchar * padname, guint64 ts);
void element_pull_range_post (gchar * elementname, gchar * padname, guint64 ts);

void generate_meta_data_pad (gpointer key, gpointer value, gpointer user_data);
void generate_meta_data (gpointer key, gpointer value, gpointer user_data);
void add_children_recursively (GstElement * element, GHashTable * table);

G_BEGIN_DECLS G_END_DECLS
