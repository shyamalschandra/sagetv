/*****************************************************************************
 * x264_gtk.c: h264 gtk encoder frontend
 *****************************************************************************
 * Copyright (C) 2006 Vincent Torri
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02111, USA.
 *****************************************************************************/

#include <string.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <errno.h>
#include <math.h>
#if defined __FreeBSD__ || defined __OpenBSD__ || defined __NetBSD__ || defined __DragonFly__
#  include <inttypes.h>
#else
#  include <stdint.h>
#endif
#ifdef _WIN32
#  define _WIN32_IE   0x400
#  include <unistd.h>        /* for mkdir */
#  include <shlobj.h>        /* for SHGetSpecialFolderPath */
#endif

#include "../x264.h"

#include <gtk/gtk.h>

#include "x264_gtk.h"
#include "x264_gtk_i18n.h"
#include "x264_gtk_private.h"
#include "x264_gtk_enum.h"
#include "x264_gtk_bitrate.h"
#include "x264_gtk_rc.h"
#include "x264_gtk_mb.h"
#include "x264_gtk_more.h"
#include "x264_gtk_cqm.h"


#define CHECK_FLAG(a,flag) ((a) & (flag)) == (flag)
#define round(a) ( ((a)<0.0) ? (gint)(floor((a) - 0.5)) : (gint)(floor((a) + 0.5)) )
#define X264_MAX(a,b) ( (a)>(b) ? (a) : (b) )
#define X264_MIN(a,b) ( (a)<(b) ? (a) : (b) )

/* Callbacks */
static void x264_dialog_run (GtkDialog       *dialog,
                         gint             response,
                         X264_Gui_Config *gconfig,
                         X264_Gtk        *x264_gtk);


/* x264 config management */
static void x264_default_load (GtkButton *button, gpointer user_data);
static void x264_current_get (X264_Gui_Config *gconfig, X264_Gtk *x264_gtk);
static void x264_current_set (X264_Gui_Config *gconfig, X264_Gtk *x264_gtk);
static void x264_default_set (X264_Gtk *x264_gtk);


/* Result must be freed */
x264_param_t *x264_gtk_param_get (X264_Gtk *x264_gtk)
{
  x264_param_t *param;

  if (!x264_gtk)
    return NULL;

  param = (x264_param_t *)g_malloc (sizeof (x264_param_t));
  if (!param)
    return NULL;

  x264_param_default (param);

  /* rate control */
  param->rc.f_ip_factor = 1.0 + (double)x264_gtk->keyframe_boost / 100.0;
  param->rc.f_pb_factor = 1.0 + (double)x264_gtk->bframes_reduction / 100.0;
  param->rc.f_qcompress = (double)x264_gtk->bitrate_variability / 100.0;

  param->rc.i_qp_min = x264_gtk->min_qp;
  param->rc.i_qp_max = x264_gtk->max_qp;
  param->rc.i_qp_step = x264_gtk->max_qp_step;

  param->i_scenecut_threshold = x264_gtk->scene_cut_threshold;
  param->i_keyint_min = x264_gtk->min_idr_frame_interval;
  param->i_keyint_max = x264_gtk->max_idr_frame_interval;

  param->rc.i_vbv_max_bitrate = x264_gtk->vbv_max_bitrate;
  param->rc.i_vbv_buffer_size = x264_gtk->vbv_buffer_size;
  param->rc.f_vbv_buffer_init = x264_gtk->vbv_buffer_init;

  /* mb */
  param->analyse.b_transform_8x8 = x264_gtk->transform_8x8;
  param->analyse.inter = 0;
  if (x264_gtk->pframe_search_8)
    param->analyse.inter |= X264_ANALYSE_PSUB16x16;
  if (x264_gtk->bframe_search_8)
    param->analyse.inter |= X264_ANALYSE_BSUB16x16;
  if (x264_gtk->pframe_search_4)
    param->analyse.inter |= X264_ANALYSE_PSUB8x8;
  if (x264_gtk->inter_search_8)
    param->analyse.inter |= X264_ANALYSE_I8x8;
  if (x264_gtk->inter_search_4)
    param->analyse.inter |= X264_ANALYSE_I4x4;

  param->b_bframe_pyramid = x264_gtk->bframe_pyramid && x264_gtk->bframe;
  param->analyse.b_bidir_me = x264_gtk->bidir_me;
  param->b_bframe_adaptive = x264_gtk->bframe_adaptive;
  param->analyse.b_weighted_bipred = x264_gtk->weighted_bipred;
  param->i_bframe = x264_gtk->bframe;
  param->i_bframe_bias = x264_gtk->bframe_bias;
  param->analyse.i_direct_mv_pred = x264_gtk->direct_mode;

/*   param->b_bframe_pyramid = param->b_bframe_pyramid && (param->i_bframe > 1); */

  /* more */
  param->analyse.i_subpel_refine = x264_gtk->partition_decision + 1;
  param->analyse.b_bframe_rdo = x264_gtk->bframe_rdo;
  param->analyse.i_me_method = x264_gtk->me_method;
  param->analyse.i_me_range = x264_gtk->range;
  param->analyse.b_chroma_me = x264_gtk->chroma_me;
  param->analyse.i_trellis = x264_gtk->trellis;
  param->analyse.i_noise_reduction = x264_gtk->noise_reduction;
  param->i_frame_reference = x264_gtk->max_ref_frames;
  param->analyse.b_mixed_references = x264_gtk->mixed_refs;
  param->analyse.b_fast_pskip = x264_gtk->fast_pskip;
  param->analyse.b_dct_decimate = x264_gtk->dct_decimate;
  /* rdo : RD based mode decision for B-frames. Requires subme 6 */

  param->vui.i_sar_width = x264_gtk->sample_ar_x;
  param->vui.i_sar_height = x264_gtk->sample_ar_y;
  param->i_threads = x264_gtk->threads;
  param->b_cabac = x264_gtk->cabac;
  param->b_deblocking_filter = x264_gtk->deblocking_filter;
  param->i_deblocking_filter_alphac0 = x264_gtk->strength;
  param->i_deblocking_filter_beta = x264_gtk->threshold;

  param->i_log_level = x264_gtk->debug_method - 1;

  /* cqm */
  param->i_cqm_preset = x264_gtk->cqm_preset;
  if (x264_gtk->cqm_file && (x264_gtk->cqm_file[0] != '\0'))
    param->psz_cqm_file = x264_gtk->cqm_file;
  memcpy( param->cqm_4iy, x264_gtk->cqm_4iy, 16 );
  memcpy( param->cqm_4ic, x264_gtk->cqm_4ic, 16 );
  memcpy( param->cqm_4py, x264_gtk->cqm_4py, 16 );
  memcpy( param->cqm_4pc, x264_gtk->cqm_4pc, 16 );
  memcpy( param->cqm_8iy, x264_gtk->cqm_8iy, 64 );
  memcpy( param->cqm_8py, x264_gtk->cqm_8py, 64 );

  /* bitrate */
  switch (x264_gtk->pass) {
  case X264_PASS_SINGLE_BITRATE:
    param->rc.i_rc_method = X264_RC_ABR;
    param->rc.i_bitrate  = x264_gtk->average_bitrate;
    break;
  case X264_PASS_SINGLE_QUANTIZER:
    param->rc.i_rc_method = X264_RC_CQP;
    param->rc.i_qp_constant = x264_gtk->quantizer;
    break;
  case X264_PASS_MULTIPASS_1ST_FAST:
    param->analyse.i_subpel_refine = X264_MAX( X264_MIN( 3, param->analyse.i_subpel_refine - 1 ), 1 );
    param->i_frame_reference = ( param->i_frame_reference + 1 ) >> 1;
    param->analyse.inter &= ( ~X264_ANALYSE_PSUB8x8 );
    param->analyse.inter &= ( ~X264_ANALYSE_BSUB16x16 );
  case X264_PASS_MULTIPASS_1ST:
    param->rc.i_rc_method = X264_RC_ABR;
    param->rc.i_bitrate  = x264_gtk->average_bitrate;
    param->rc.f_rate_tolerance = 4.0;
    break;
  case X264_PASS_MULTIPASS_NTH:
    param->rc.i_rc_method = X264_RC_ABR;
    param->rc.i_bitrate  = x264_gtk->average_bitrate;
    param->rc.f_rate_tolerance = 1.0;
    break;
  }

  param->rc.b_stat_write = x264_gtk->stat_write;
  param->rc.b_stat_read = x264_gtk->stat_read;

  /* FIXME: potential mem leak... */
  param->rc.psz_stat_out = x264_gtk_path (x264_gtk->statsfile_name);

  return param;
}

/* Result must be freed */
X264_Gtk *
x264_gtk_load (void)
{
  X264_Gtk     *x264_gtk;
  GIOChannel   *file;
  GError       *error = NULL;
  gchar        *filename;

  x264_gtk = (X264_Gtk *)g_malloc0 (sizeof (X264_Gtk));
  if (!x264_gtk)
    return NULL;

  filename = x264_gtk_path ("x264.cfg");
  file = g_io_channel_new_file (filename, "r", &error);
  if (error) {
    g_print (_("x264.cfg: %s\n"), error->message);
    g_print (_("Loading default configuration\n"));
    x264_default_set (x264_gtk);
  }
  else {
    GIOStatus status;
    gchar    *data = NULL;
    gsize     length;

    g_print (_("Loading configuration from %s\n"), filename);
    g_io_channel_set_encoding (file, NULL, NULL);
    status = g_io_channel_read_to_end (file, &data, &length, &error);
    if ((status == G_IO_STATUS_NORMAL) &&
        (length == sizeof (X264_Gtk))) {
      memcpy (x264_gtk, data, length);
    }
    g_io_channel_shutdown (file, TRUE, NULL);
    g_io_channel_unref (file);
  }
  g_free (filename);

  return x264_gtk;
}

GtkWidget *
x264_gtk_window_create (GtkWidget *parent)
{
  GtkWidget       *win_x264_gtk;
  GtkWidget       *notebook;
  GtkWidget       *page;
  GtkWidget       *button;
  GtkWidget       *label;
  X264_Gui_Config *gconfig;
  X264_Gtk        *x264_gtk;
  gint             result;
  GtkDialogFlags   flags = 0;

  gconfig = (X264_Gui_Config *)g_malloc (sizeof (X264_Gui_Config));
  if (!gconfig)
    return NULL;

  x264_gtk = x264_gtk_load ();

  if (parent)
    flags = GTK_DIALOG_MODAL |GTK_DIALOG_DESTROY_WITH_PARENT;
  win_x264_gtk = gtk_dialog_new_with_buttons (_("X264 Configuration"),
                                              GTK_WINDOW (parent),
                                              flags,
                                              NULL);

  button = gtk_button_new_with_label (_("Default"));
  gtk_box_pack_start (GTK_BOX (GTK_DIALOG (win_x264_gtk)->action_area), button, FALSE, TRUE, 6);
  g_signal_connect (G_OBJECT (button),
                    "clicked",
                    G_CALLBACK (x264_default_load),
                    gconfig);
  gtk_widget_show (button);

  gtk_dialog_add_buttons (GTK_DIALOG (win_x264_gtk),
                          GTK_STOCK_CANCEL, GTK_RESPONSE_CANCEL,
                          GTK_STOCK_OK, GTK_RESPONSE_OK,
                          NULL);

  g_object_set_data (G_OBJECT (win_x264_gtk), "x264-gui-config", gconfig);
  g_object_set_data (G_OBJECT (win_x264_gtk), "x264-config", x264_gtk);
  gtk_window_set_resizable (GTK_WINDOW (win_x264_gtk), FALSE);

  notebook = gtk_notebook_new ();
  gtk_container_add (GTK_CONTAINER (GTK_DIALOG (win_x264_gtk)->vbox), notebook);
  gtk_widget_show (notebook);

  label = gtk_label_new (_("Bitrate"));
  gtk_widget_show (label);

  page = x264_bitrate_page (gconfig);
  gtk_notebook_append_page (GTK_NOTEBOOK (notebook), page, label);
  gtk_widget_show (page);

  label = gtk_label_new (_("Rate Control"));
  gtk_widget_show (label);

  page = x264_rate_control_page (gconfig);
  gtk_notebook_append_page (GTK_NOTEBOOK (notebook), page, label);
  gtk_widget_show (page);

  label = gtk_label_new (_("MB & Frames"));
  gtk_widget_show (label);

  page = x264_mb_page (gconfig);
  gtk_notebook_append_page (GTK_NOTEBOOK (notebook), page, label);
  gtk_widget_show (page);

  label = gtk_label_new (_("More..."));
  gtk_widget_show (label);

  page = x264_more_page (gconfig);
  gtk_notebook_append_page (GTK_NOTEBOOK (notebook), page, label);
  gtk_widget_show (page);

  label = gtk_label_new (_("Quantization matrices"));
  gtk_widget_show (label);

  page = x264_cqm_page (gconfig);
  gtk_notebook_append_page (GTK_NOTEBOOK (notebook), page, label);
  gtk_widget_show (page);

  x264_current_set (gconfig, x264_gtk);

  result = gtk_dialog_run (GTK_DIALOG (win_x264_gtk));
  x264_dialog_run (GTK_DIALOG (win_x264_gtk), result, gconfig, x264_gtk);

  return win_x264_gtk;
}

void
x264_gtk_shutdown (GtkWidget *dialog)
{
  X264_Gui_Config *gconfig;

  gconfig = g_object_get_data (G_OBJECT (dialog), "x264-gui-config");
  gtk_widget_destroy (dialog);
  if (gconfig)
    g_free (gconfig);
}

void
x264_gtk_free (X264_Gtk *x264_gtk)
{
  if (!x264_gtk)
    return;

  g_free (x264_gtk);
}


/* Notebook pages */

/* Callbacks */

static void
x264_dialog_run (GtkDialog       *dialog UNUSED,
             gint             response,
             X264_Gui_Config *gconfig,
             X264_Gtk        *x264_gtk)
{
  if (response == GTK_RESPONSE_OK)
    {
      GIOChannel *file;
      gchar      *filename;
      gchar      *dir;
      gsize       length;
      gint        res;
#ifndef _WIN32
      mode_t      mode;
#endif

      dir = x264_gtk_path (NULL);

#ifdef _WIN32
      res = mkdir (dir);
#else
      mode =
        S_IRUSR | S_IXUSR | S_IWUSR |
        S_IRGRP | S_IXGRP | S_IWGRP |
        S_IROTH | S_IXOTH | S_IWOTH;
      res = mkdir (dir, mode);
#endif /* _WIN32 */
      if (res != 0 && errno != EEXIST)
        {
          g_free (dir);

          return;
        }

      filename = x264_gtk_path ("x264.cfg");
      g_print (_("Writing configuration to %s\n"), filename);
      file = g_io_channel_new_file (filename, "w+", NULL);
      if (file)
        {
          x264_current_get (gconfig, x264_gtk);
          g_io_channel_set_encoding (file, NULL, NULL);
          g_io_channel_write_chars (file, (const gchar *)x264_gtk,
                                    sizeof (X264_Gtk), &length, NULL);
          g_io_channel_unref (file);
        }
      g_free (filename);
      g_free (dir);
    }
}

/* x264 config management */
static void
x264_default_load (GtkButton *button UNUSED, gpointer user_data)
{
  gchar            buf[64];
  X264_Gui_Config *config;
  x264_param_t     param;
  gint             i;

  if (!user_data)
    return;

  config = (X264_Gui_Config *)user_data;

  x264_param_default (&param);

  /* bitrate */
  gtk_combo_box_set_active (GTK_COMBO_BOX (config->bitrate.pass), 1);
  g_snprintf (buf, 64, "%d", param.rc.i_bitrate);
  gtk_entry_set_text (GTK_ENTRY (config->bitrate.w_average_bitrate), buf);
  gtk_entry_set_text (GTK_ENTRY (config->bitrate.w_target_bitrate), buf);
  gtk_range_set_range (GTK_RANGE (config->bitrate.w_quantizer),
                       0.0,
                       51.0);
  gtk_range_set_value (GTK_RANGE (config->bitrate.w_quantizer),
                       (gdouble)param.rc.i_qp_constant);
  gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (config->bitrate.update_statfile), FALSE);
  gtk_entry_set_text (GTK_ENTRY (config->bitrate.statsfile_name), "x264.stats");
  gtk_widget_set_sensitive (config->bitrate.statsfile_name, FALSE);
  gtk_widget_set_sensitive (config->bitrate.update_statfile, FALSE);

  /* rate control */
  g_snprintf (buf, 64, "%d", round((param.rc.f_ip_factor - 1.0) * 100));
  gtk_entry_set_text (GTK_ENTRY (config->rate_control.bitrate.keyframe_boost), buf);
  g_snprintf (buf, 64, "%d", round((param.rc.f_pb_factor - 1.0) * 100));
  gtk_entry_set_text (GTK_ENTRY (config->rate_control.bitrate.bframes_reduction), buf);
  g_snprintf (buf, 64, "%d", (gint)(param.rc.f_qcompress * 100));
  gtk_entry_set_text (GTK_ENTRY (config->rate_control.bitrate.bitrate_variability), buf);

  g_snprintf (buf, 64, "%d", param.rc.i_qp_min);
  gtk_entry_set_text (GTK_ENTRY (config->rate_control.quantization_limits.min_qp), buf);
  g_snprintf (buf, 64, "%d", param.rc.i_qp_max);
  gtk_entry_set_text (GTK_ENTRY (config->rate_control.quantization_limits.max_qp), buf);
  g_snprintf (buf, 64, "%d", param.rc.i_qp_step);
  gtk_entry_set_text (GTK_ENTRY (config->rate_control.quantization_limits.max_qp_step), buf);

  g_snprintf (buf, 64, "%d", param.i_scenecut_threshold);
  gtk_entry_set_text (GTK_ENTRY (config->rate_control.scene_cuts.scene_cut_threshold), buf);
  g_snprintf (buf, 64, "%d", param.i_keyint_min);
  gtk_entry_set_text (GTK_ENTRY (config->rate_control.scene_cuts.min_idr_frame_interval), buf);
  g_snprintf (buf, 64, "%d", param.i_keyint_max);
  gtk_entry_set_text (GTK_ENTRY (config->rate_control.scene_cuts.max_idr_frame_interval), buf);

  g_snprintf (buf, 64, "%d", param.rc.i_vbv_max_bitrate);
  gtk_entry_set_text (GTK_ENTRY (config->rate_control.vbv.vbv_max_bitrate), buf);
  g_snprintf (buf, 64, "%d", param.rc.i_vbv_buffer_size);
  gtk_entry_set_text (GTK_ENTRY (config->rate_control.vbv.vbv_buffer_size), buf);
  g_snprintf (buf, 64, "%.1f", param.rc.f_vbv_buffer_init);
  gtk_entry_set_text (GTK_ENTRY (config->rate_control.vbv.vbv_buffer_init), buf);

  /* mb */
  gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (config->mb.partitions.transform_8x8), param.analyse.b_transform_8x8);
  gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (config->mb.partitions.pframe_search_8), CHECK_FLAG(param.analyse.inter, X264_ANALYSE_PSUB16x16));
  gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (config->mb.partitions.bframe_search_8), CHECK_FLAG(param.analyse.inter, X264_ANALYSE_BSUB16x16));
  gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (config->mb.partitions.pframe_search_4), CHECK_FLAG(param.analyse.inter, X264_ANALYSE_PSUB8x8));
  gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (config->mb.partitions.inter_search_8), CHECK_FLAG(param.analyse.inter, X264_ANALYSE_I8x8));
  gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (config->mb.partitions.inter_search_4), CHECK_FLAG(param.analyse.inter, X264_ANALYSE_I4x4));

  gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (config->mb.bframes.bframe_pyramid), param.b_bframe_pyramid);
  gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (config->mb.bframes.bidir_me), param.analyse.b_bidir_me);
  gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (config->mb.bframes.bframe_adaptive), param.b_bframe_adaptive);
  gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (config->mb.bframes.weighted_bipred), param.analyse.b_weighted_bipred);
  g_snprintf (buf, 64, "%d", param.i_bframe);
  gtk_entry_set_text (GTK_ENTRY (config->mb.bframes.bframe), buf);
  gtk_range_set_value (GTK_RANGE (config->mb.bframes.bframe_bias), (gdouble)param.i_bframe_bias);
  gtk_combo_box_set_active (GTK_COMBO_BOX (config->mb.bframes.direct_mode), param.analyse.i_direct_mv_pred);

  /* more */
  if (param.analyse.b_bframe_rdo)
    gtk_combo_box_set_active (GTK_COMBO_BOX (config->more.motion_estimation.partition_decision), X264_PD_6b);
  else
    gtk_combo_box_set_active (GTK_COMBO_BOX (config->more.motion_estimation.partition_decision), param.analyse.i_subpel_refine - 1);
  gtk_combo_box_set_active (GTK_COMBO_BOX (config->more.motion_estimation.method), param.analyse.i_me_method);
  g_snprintf (buf, 64, "%d", param.analyse.i_me_range);
  gtk_entry_set_text (GTK_ENTRY (config->more.motion_estimation.range), buf);
  gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (config->more.motion_estimation.chroma_me), param.analyse.b_chroma_me);
  gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (config->more.motion_estimation.mixed_refs), param.analyse.b_mixed_references);
  g_snprintf (buf, 64, "%d", param.i_frame_reference);
  gtk_entry_set_text (GTK_ENTRY (config->more.motion_estimation.max_ref_frames), buf);
  gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (config->more.motion_estimation.fast_pskip), param.analyse.b_fast_pskip);
  gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (config->more.motion_estimation.dct_decimate), param.analyse.b_dct_decimate);

  g_snprintf (buf, 64, "%d", param.vui.i_sar_width);
  gtk_entry_set_text (GTK_ENTRY (config->more.misc.sample_ar_x), buf);
  g_snprintf (buf, 64, "%d", param.vui.i_sar_height);
  gtk_entry_set_text (GTK_ENTRY (config->more.misc.sample_ar_y), buf);
  gtk_spin_button_set_value (GTK_SPIN_BUTTON (config->more.misc.threads), param.i_threads);
  gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (config->more.misc.cabac), param.b_cabac);
  gtk_combo_box_set_active (GTK_COMBO_BOX (config->more.misc.trellis), param.analyse.i_trellis);
  g_snprintf (buf, 64, "%d", param.analyse.i_noise_reduction);
  gtk_entry_set_text (GTK_ENTRY (config->more.misc.noise_reduction), buf);
  gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (config->more.misc.df.deblocking_filter), param.b_deblocking_filter);
  gtk_range_set_value (GTK_RANGE (config->more.misc.df.strength), (gdouble)param.i_deblocking_filter_alphac0);
  gtk_range_set_value (GTK_RANGE (config->more.misc.df.threshold), (gdouble)param.i_deblocking_filter_beta);

  gtk_combo_box_set_active (GTK_COMBO_BOX (config->more.debug.log_level), param.i_log_level + 1);
  gtk_entry_set_text (GTK_ENTRY (config->more.debug.fourcc), "H264");

  /* cqm */
  switch (param.i_cqm_preset) {
  case X264_CQM_FLAT:
    // workaround: gtk fails to update the matrix entries if we activate the button
    // that was already active.
    gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (config->cqm.radio_jvt), TRUE);
    gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (config->cqm.radio_flat), TRUE);
    break;
  case X264_CQM_JVT:
    gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (config->cqm.radio_jvt), TRUE);
    break;
  case X264_CQM_CUSTOM:
    gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (config->cqm.radio_custom), TRUE);
    break;
  }
  if (param.psz_cqm_file && (param.psz_cqm_file[0] != '\0'))
    gtk_file_chooser_set_filename (GTK_FILE_CHOOSER (config->cqm.cqm_file), param.psz_cqm_file);
  for (i = 0; i < 16; i++) {
    gchar buf[4];

    g_snprintf (buf, 4, "%d", param.cqm_4iy[i]);
    gtk_entry_set_text (GTK_ENTRY (config->cqm.cqm_4iy[i]), buf);
    g_snprintf (buf, 4, "%d", param.cqm_4ic[i]);
    gtk_entry_set_text (GTK_ENTRY (config->cqm.cqm_4ic[i]), buf);
    g_snprintf (buf, 4, "%d", param.cqm_4py[i]);
    gtk_entry_set_text (GTK_ENTRY (config->cqm.cqm_4py[i]), buf);
    g_snprintf (buf, 4, "%d", param.cqm_4pc[i]);
    gtk_entry_set_text (GTK_ENTRY (config->cqm.cqm_4pc[i]), buf);
  }
  for (i = 0; i < 64; i++) {
    gchar buf[4];

    g_snprintf (buf, 4, "%d", param.cqm_8iy[i]);
    gtk_entry_set_text (GTK_ENTRY (config->cqm.cqm_8iy[i]), buf);
    g_snprintf (buf, 4, "%d", param.cqm_8py[i]);
    gtk_entry_set_text (GTK_ENTRY (config->cqm.cqm_8py[i]), buf);
  }
}

static void
x264_default_set (X264_Gtk *x264_gtk)
{
  x264_param_t param;

  x264_param_default (&param);

  /* bitrate */
  x264_gtk->pass = X264_PASS_SINGLE_QUANTIZER;
  x264_gtk->average_bitrate = param.rc.i_bitrate;
  x264_gtk->target_bitrate = param.rc.i_bitrate;
  x264_gtk->quantizer = param.rc.i_qp_constant;
  x264_gtk->stat_write = param.rc.b_stat_write;
  x264_gtk->stat_read = param.rc.b_stat_read;
  x264_gtk->update_statfile = 0;
  x264_gtk->statsfile_length = strlen (param.rc.psz_stat_out);
  memcpy (x264_gtk->statsfile_name, param.rc.psz_stat_out, x264_gtk->statsfile_length + 1);

  /* rate control */
  x264_gtk->keyframe_boost = round((param.rc.f_ip_factor - 1.0) * 100);
  x264_gtk->bframes_reduction = round((param.rc.f_pb_factor - 1.0) * 100);
  x264_gtk->bitrate_variability = round(param.rc.f_qcompress * 100);

  x264_gtk->min_qp = param.rc.i_qp_min;
  x264_gtk->max_qp = param.rc.i_qp_max;
  x264_gtk->max_qp_step = param.rc.i_qp_step;

  x264_gtk->scene_cut_threshold = param.i_scenecut_threshold;
  x264_gtk->min_idr_frame_interval = param.i_keyint_min;
  x264_gtk->max_idr_frame_interval = param.i_keyint_max;

  x264_gtk->vbv_max_bitrate = param.rc.i_vbv_max_bitrate;
  x264_gtk->vbv_buffer_size = param.rc.i_vbv_buffer_size;
  x264_gtk->vbv_buffer_init = param.rc.f_vbv_buffer_init;

  /* mb */
  x264_gtk->transform_8x8 = param.analyse.b_transform_8x8;

  if (CHECK_FLAG(param.analyse.inter, X264_ANALYSE_PSUB16x16))
    x264_gtk->pframe_search_8 = 1;
  else
    x264_gtk->pframe_search_8 = 0;
  if (CHECK_FLAG(param.analyse.inter, X264_ANALYSE_BSUB16x16))
    x264_gtk->bframe_search_8 = 1;
  else
    x264_gtk->bframe_search_8 = 0;
  if (CHECK_FLAG(param.analyse.inter, X264_ANALYSE_PSUB8x8))
    x264_gtk->pframe_search_4 = 1;
  else
    x264_gtk->pframe_search_4 = 0;
  x264_gtk->inter_search_8 = CHECK_FLAG(param.analyse.inter, X264_ANALYSE_I8x8);
  x264_gtk->inter_search_4 = CHECK_FLAG(param.analyse.inter, X264_ANALYSE_I4x4);

  x264_gtk->bframe_pyramid = param.b_bframe_pyramid;
  x264_gtk->bidir_me = param.analyse.b_bidir_me;
  x264_gtk->bframe_adaptive = param.b_bframe_adaptive;
  x264_gtk->weighted_bipred = param.analyse.b_weighted_bipred;
  x264_gtk->bframe = param.i_bframe;
  x264_gtk->bframe_bias = param.i_bframe_bias;
  x264_gtk->direct_mode = param.analyse.i_direct_mv_pred;

  /* more */
  x264_gtk->bframe_rdo = param.analyse.b_bframe_rdo;
  x264_gtk->partition_decision = param.analyse.i_subpel_refine - 1;
  x264_gtk->me_method = param.analyse.i_me_method;
  x264_gtk->range = param.analyse.i_me_range;
  x264_gtk->chroma_me = param.analyse.b_chroma_me;
  x264_gtk->max_ref_frames = param.i_frame_reference;
  x264_gtk->mixed_refs = param.analyse.b_mixed_references;
  x264_gtk->fast_pskip = param.analyse.b_fast_pskip;
  x264_gtk->dct_decimate = param.analyse.b_dct_decimate;

  x264_gtk->sample_ar_x = param.vui.i_sar_width;
  x264_gtk->sample_ar_y = param.vui.i_sar_height;
  x264_gtk->threads = param.i_threads;
  x264_gtk->cabac = param.b_cabac;
  x264_gtk->trellis = param.analyse.i_trellis;
  x264_gtk->noise_reduction = param.analyse.i_noise_reduction;
  x264_gtk->deblocking_filter = param.b_deblocking_filter;
  x264_gtk->strength = param.i_deblocking_filter_alphac0;
  x264_gtk->threshold = param.i_deblocking_filter_beta;

  x264_gtk->debug_method = param.i_log_level + 1;
  memcpy (x264_gtk->fourcc, "H264", 5);

  /* cqm */
  x264_gtk->cqm_preset = param.i_cqm_preset;
  if (param.psz_cqm_file && (param.psz_cqm_file[0] != '\0'))
    memcpy (x264_gtk->cqm_file, param.psz_cqm_file,  strlen (param.psz_cqm_file) + 1);
  memcpy (x264_gtk->cqm_4iy, param.cqm_4iy, 16);
  memcpy (x264_gtk->cqm_4ic, param.cqm_4ic, 16);
  memcpy (x264_gtk->cqm_4py, param.cqm_4py, 16);
  memcpy (x264_gtk->cqm_4pc, param.cqm_4pc, 16);
  memcpy (x264_gtk->cqm_8iy, param.cqm_8iy, 64);
  memcpy (x264_gtk->cqm_8py, param.cqm_8py, 64);
}

static void
x264_current_set (X264_Gui_Config *config, X264_Gtk *x264_gtk)
{
  gchar buf[4096];
  gint  i;

  if (!config)
    return;

  /* bitrate */
  gtk_combo_box_set_active (GTK_COMBO_BOX (config->bitrate.pass), x264_gtk->pass);
  g_snprintf (buf, 5, "%d", x264_gtk->average_bitrate);
  gtk_entry_set_text (GTK_ENTRY (config->bitrate.w_average_bitrate), buf);
  gtk_range_set_range (GTK_RANGE (config->bitrate.w_quantizer),
                       0.0,
                       51.0);
  gtk_range_set_value (GTK_RANGE (config->bitrate.w_quantizer), x264_gtk->quantizer);
  g_snprintf (buf, 5, "%d", x264_gtk->target_bitrate);
  gtk_entry_set_text (GTK_ENTRY (config->bitrate.w_target_bitrate), buf);
  switch (gtk_combo_box_get_active (GTK_COMBO_BOX (config->bitrate.pass)))
  {
  case 0:
  case 1:
    gtk_widget_set_sensitive (config->bitrate.update_statfile, FALSE);
    gtk_widget_set_sensitive (config->bitrate.statsfile_name, FALSE);
    break;
  case 2:
  case 3:
  case 4:
  default:
    gtk_widget_set_sensitive (config->bitrate.update_statfile, TRUE);
    break;
  }
  gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (config->bitrate.update_statfile), x264_gtk->update_statfile);
  gtk_entry_set_text (GTK_ENTRY (config->bitrate.statsfile_name), x264_gtk->statsfile_name);
  if (x264_gtk->update_statfile)
    gtk_widget_set_sensitive (config->bitrate.statsfile_name, TRUE);
  else
    gtk_widget_set_sensitive (config->bitrate.statsfile_name, FALSE);

  /* rate control */
  g_snprintf (buf, 5, "%d", x264_gtk->keyframe_boost);
  gtk_entry_set_text (GTK_ENTRY (config->rate_control.bitrate.keyframe_boost), buf);
  g_snprintf (buf, 5, "%d", x264_gtk->bframes_reduction);
  gtk_entry_set_text (GTK_ENTRY (config->rate_control.bitrate.bframes_reduction), buf);
  g_snprintf (buf, 5, "%d", x264_gtk->bitrate_variability);
  gtk_entry_set_text (GTK_ENTRY (config->rate_control.bitrate.bitrate_variability), buf);

  g_snprintf (buf, 5, "%d", x264_gtk->min_qp);
  gtk_entry_set_text (GTK_ENTRY (config->rate_control.quantization_limits.min_qp), buf);
  g_snprintf (buf, 5, "%d", x264_gtk->max_qp);
  gtk_entry_set_text (GTK_ENTRY (config->rate_control.quantization_limits.max_qp), buf);
  g_snprintf (buf, 5, "%d", x264_gtk->max_qp_step);
  gtk_entry_set_text (GTK_ENTRY (config->rate_control.quantization_limits.max_qp_step), buf);

  g_snprintf (buf, 5, "%d", x264_gtk->scene_cut_threshold);
  gtk_entry_set_text (GTK_ENTRY (config->rate_control.scene_cuts.scene_cut_threshold), buf);
  g_snprintf (buf, 5, "%d", x264_gtk->min_idr_frame_interval);
  gtk_entry_set_text (GTK_ENTRY (config->rate_control.scene_cuts.min_idr_frame_interval), buf);
  g_snprintf (buf, 5, "%d", x264_gtk->max_idr_frame_interval);
  gtk_entry_set_text (GTK_ENTRY (config->rate_control.scene_cuts.max_idr_frame_interval), buf);

  g_snprintf (buf, 5, "%d", x264_gtk->vbv_max_bitrate);
  gtk_entry_set_text (GTK_ENTRY (config->rate_control.vbv.vbv_max_bitrate), buf);
  g_snprintf (buf, 5, "%d", x264_gtk->vbv_buffer_size);
  gtk_entry_set_text (GTK_ENTRY (config->rate_control.vbv.vbv_buffer_size), buf);
  g_snprintf (buf, 5, "%.1f", x264_gtk->vbv_buffer_init);
  gtk_entry_set_text (GTK_ENTRY (config->rate_control.vbv.vbv_buffer_init), buf);

  /* mb */
  if (x264_gtk->transform_8x8)
    gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (config->mb.partitions.transform_8x8), TRUE);
  else
    gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (config->mb.partitions.transform_8x8), FALSE);
  if (x264_gtk->pframe_search_8)
    gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (config->mb.partitions.pframe_search_8), TRUE);
  else
    gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (config->mb.partitions.pframe_search_8), FALSE);
  if (x264_gtk->bframe_search_8)
    gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (config->mb.partitions.bframe_search_8), TRUE);
  else
    gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (config->mb.partitions.bframe_search_8), FALSE);
  if (x264_gtk->pframe_search_4)
    gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (config->mb.partitions.pframe_search_4), TRUE);
  else
    gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (config->mb.partitions.pframe_search_4), FALSE);
  if (x264_gtk->inter_search_8)
    gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (config->mb.partitions.inter_search_8), TRUE);
  else
    gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (config->mb.partitions.inter_search_8), FALSE);
  if (x264_gtk->inter_search_4)
    gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (config->mb.partitions.inter_search_4), TRUE);
  else
    gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (config->mb.partitions.inter_search_4), FALSE);

  /* mb - bframes */
  if (x264_gtk->bframe_pyramid)
    gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (config->mb.bframes.bframe_pyramid), TRUE);
  else
    gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (config->mb.bframes.bframe_pyramid), FALSE);
  if (x264_gtk->bidir_me)
    gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (config->mb.bframes.bidir_me), TRUE);
  else
    gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (config->mb.bframes.bidir_me), FALSE);
  if (x264_gtk->bframe_adaptive)
    gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (config->mb.bframes.bframe_adaptive), TRUE);
  else
    gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (config->mb.bframes.bframe_adaptive), FALSE);
  if (x264_gtk->weighted_bipred)
    gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (config->mb.bframes.weighted_bipred), TRUE);
  else
    gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (config->mb.bframes.weighted_bipred), FALSE);
  g_snprintf (buf, 5, "%d", x264_gtk->bframe);
  gtk_entry_set_text (GTK_ENTRY (config->mb.bframes.bframe), buf);
  gtk_range_set_value (GTK_RANGE (config->mb.bframes.bframe_bias), (gdouble)x264_gtk->bframe_bias);
  gtk_combo_box_set_active (GTK_COMBO_BOX (config->mb.bframes.direct_mode), x264_gtk->direct_mode);

  /* more */
  if (x264_gtk->bframe_rdo)
    gtk_combo_box_set_active (GTK_COMBO_BOX (config->more.motion_estimation.partition_decision), X264_PD_6b);
  else
    gtk_combo_box_set_active (GTK_COMBO_BOX (config->more.motion_estimation.partition_decision), x264_gtk->partition_decision);
  gtk_combo_box_set_active (GTK_COMBO_BOX (config->more.motion_estimation.method), x264_gtk->me_method);
  g_snprintf (buf, 5, "%d", x264_gtk->range);
  gtk_entry_set_text (GTK_ENTRY (config->more.motion_estimation.range), buf);
  if (x264_gtk->chroma_me)
    gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (config->more.motion_estimation.chroma_me), TRUE);
  else
    gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (config->more.motion_estimation.chroma_me), FALSE);
  g_snprintf (buf, 5, "%d", x264_gtk->max_ref_frames);
  gtk_entry_set_text (GTK_ENTRY (config->more.motion_estimation.max_ref_frames), buf);
  if (x264_gtk->mixed_refs && (x264_gtk->max_ref_frames >= 2))
    gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (config->more.motion_estimation.mixed_refs), TRUE);
  else
    gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (config->more.motion_estimation.mixed_refs), FALSE);
  if (x264_gtk->fast_pskip)
    gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (config->more.motion_estimation.fast_pskip), TRUE);
  else
    gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (config->more.motion_estimation.fast_pskip), FALSE);
  if (x264_gtk->dct_decimate)
    gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (config->more.motion_estimation.dct_decimate), TRUE);
  else
    gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (config->more.motion_estimation.dct_decimate), FALSE);

  g_snprintf (buf, 5, "%d", x264_gtk->sample_ar_x);
  gtk_entry_set_text (GTK_ENTRY (config->more.misc.sample_ar_x), buf);
  g_snprintf (buf, 5, "%d", x264_gtk->sample_ar_y);
  gtk_entry_set_text (GTK_ENTRY (config->more.misc.sample_ar_y), buf);
  gtk_spin_button_set_value (GTK_SPIN_BUTTON (config->more.misc.threads), x264_gtk->threads);
  if (x264_gtk->cabac)
    gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (config->more.misc.cabac), TRUE);
  else
    gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (config->more.misc.cabac), FALSE);
  gtk_combo_box_set_active (GTK_COMBO_BOX (config->more.misc.trellis), x264_gtk->trellis);
  g_snprintf (buf, 64, "%d", x264_gtk->noise_reduction);
  gtk_entry_set_text (GTK_ENTRY (config->more.misc.noise_reduction), buf);
  if (x264_gtk->deblocking_filter)
    gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (config->more.misc.df.deblocking_filter), TRUE);
  else
    gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (config->more.misc.df.deblocking_filter), FALSE);
  gtk_range_set_value (GTK_RANGE (config->more.misc.df.strength), (gdouble)x264_gtk->strength);
  gtk_range_set_value (GTK_RANGE (config->more.misc.df.threshold), (gdouble)x264_gtk->threshold);

  gtk_combo_box_set_active (GTK_COMBO_BOX (config->more.debug.log_level), x264_gtk->debug_method);
  gtk_entry_set_text (GTK_ENTRY (config->more.debug.fourcc), x264_gtk->fourcc);

  /* cqm */
  switch (x264_gtk->cqm_preset) {
  case X264_CQM_PRESET_FLAT:
    // workaround: gtk fails to update the matrix entries if we activate the button
    // that was already active.
    gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (config->cqm.radio_jvt), TRUE);
    gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (config->cqm.radio_flat), TRUE);
    break;
  case X264_CQM_PRESET_JVT:
    gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (config->cqm.radio_jvt), TRUE);
    break;
  case X264_CQM_PRESET_CUSTOM:
    gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (config->cqm.radio_custom), TRUE);
    break;
  }
  if (x264_gtk->cqm_file && (x264_gtk->cqm_file[0] != '\0'))
    gtk_file_chooser_set_filename (GTK_FILE_CHOOSER (config->cqm.cqm_file), x264_gtk->cqm_file);
  for (i = 0; i < 16; i++) {
    gchar buf[4];

    g_snprintf (buf, 4, "%d", x264_gtk->cqm_4iy[i]);
    gtk_entry_set_text (GTK_ENTRY (config->cqm.cqm_4iy[i]), buf);
    g_snprintf (buf, 4, "%d", x264_gtk->cqm_4ic[i]);
    gtk_entry_set_text (GTK_ENTRY (config->cqm.cqm_4ic[i]), buf);
    g_snprintf (buf, 4, "%d", x264_gtk->cqm_4py[i]);
    gtk_entry_set_text (GTK_ENTRY (config->cqm.cqm_4py[i]), buf);
    g_snprintf (buf, 4, "%d", x264_gtk->cqm_4pc[i]);
    gtk_entry_set_text (GTK_ENTRY (config->cqm.cqm_4pc[i]), buf);
  }
  for (i = 0; i < 64; i++) {
    gchar buf[4];

    g_snprintf (buf, 4, "%d", x264_gtk->cqm_8iy[i]);
    gtk_entry_set_text (GTK_ENTRY (config->cqm.cqm_8iy[i]), buf);
    g_snprintf (buf, 4, "%d", x264_gtk->cqm_8py[i]);
    gtk_entry_set_text (GTK_ENTRY (config->cqm.cqm_8py[i]), buf);
  }
}

static void
x264_current_get (X264_Gui_Config *gconfig, X264_Gtk *x264_gtk)
{
  const gchar *text;
  gint         i;

  if (!gconfig)
    return;

  if (!x264_gtk)
    g_print (_("problem...\n"));

  /* bitrate */
  switch (gtk_combo_box_get_active (GTK_COMBO_BOX (gconfig->bitrate.pass)))
  {
  case 0:
    x264_gtk->pass = X264_PASS_SINGLE_BITRATE;
    text = gtk_entry_get_text (GTK_ENTRY (gconfig->bitrate.w_average_bitrate));
    x264_gtk->average_bitrate = (gint)g_ascii_strtoull (text, NULL, 10);
    x264_gtk->stat_write = 0;
    x264_gtk->stat_read = 0;
    break;
  case 1:
    x264_gtk->pass = X264_PASS_SINGLE_QUANTIZER;
    x264_gtk->quantizer = (gint)gtk_range_get_value (GTK_RANGE (gconfig->bitrate.w_quantizer));
    x264_gtk->stat_write = 0;
    x264_gtk->stat_read = 0;
    break;
  case 2:
    x264_gtk->pass = X264_PASS_MULTIPASS_1ST;
    text = gtk_entry_get_text (GTK_ENTRY (gconfig->bitrate.w_target_bitrate));
    x264_gtk->target_bitrate = (gint)g_ascii_strtoull (text, NULL, 10);
    x264_gtk->stat_write = 1;
    break;
  case 3:
    x264_gtk->pass = X264_PASS_MULTIPASS_1ST_FAST;
    text = gtk_entry_get_text (GTK_ENTRY (gconfig->bitrate.w_target_bitrate));
    x264_gtk->target_bitrate = (gint)g_ascii_strtoull (text, NULL, 10);
    x264_gtk->stat_write = 1;
    break;
  case 4:
  default:
    x264_gtk->pass = X264_PASS_MULTIPASS_NTH;
    text = gtk_entry_get_text (GTK_ENTRY (gconfig->bitrate.w_target_bitrate));
    x264_gtk->target_bitrate = (gint)g_ascii_strtoull (text, NULL, 10);
    x264_gtk->stat_read = 1;
    if (gtk_toggle_button_get_active (GTK_TOGGLE_BUTTON (gconfig->bitrate.update_statfile)))
      x264_gtk->stat_write = 1;
    break;
  }
  if (gtk_toggle_button_get_active (GTK_TOGGLE_BUTTON (gconfig->bitrate.update_statfile)))
    x264_gtk->update_statfile = 1;
  else
    x264_gtk->update_statfile = 0;

  text = gtk_entry_get_text (GTK_ENTRY (gconfig->bitrate.statsfile_name));
  x264_gtk->statsfile_length = strlen (text);
  memcpy (x264_gtk->statsfile_name,
          text,
          x264_gtk->statsfile_length + 1);

  /* rate control */
  text = gtk_entry_get_text (GTK_ENTRY (gconfig->rate_control.bitrate.keyframe_boost));
  x264_gtk->keyframe_boost = (gint)g_ascii_strtoull (text, NULL, 10);

  text = gtk_entry_get_text (GTK_ENTRY (gconfig->rate_control.bitrate.bframes_reduction));
  x264_gtk->bframes_reduction = (gint)g_ascii_strtoull (text, NULL, 10);
  text = gtk_entry_get_text (GTK_ENTRY (gconfig->rate_control.bitrate.bitrate_variability));
  x264_gtk->bitrate_variability = (gint)g_ascii_strtoull (text, NULL, 10);

  text = gtk_entry_get_text (GTK_ENTRY (gconfig->rate_control.quantization_limits.min_qp));
  x264_gtk->min_qp = (gint)g_ascii_strtoull (text, NULL, 10);
  text = gtk_entry_get_text (GTK_ENTRY (gconfig->rate_control.quantization_limits.max_qp));
  x264_gtk->max_qp = (gint)g_ascii_strtoull (text, NULL, 10);
  text = gtk_entry_get_text (GTK_ENTRY (gconfig->rate_control.quantization_limits.max_qp_step));
  x264_gtk->max_qp_step = (gint)g_ascii_strtoull (text, NULL, 10);

  text = gtk_entry_get_text (GTK_ENTRY (gconfig->rate_control.scene_cuts.scene_cut_threshold));
  x264_gtk->scene_cut_threshold = (gint)g_ascii_strtoull (text, NULL, 10);
  text = gtk_entry_get_text (GTK_ENTRY (gconfig->rate_control.scene_cuts.min_idr_frame_interval));
  x264_gtk->min_idr_frame_interval = (gint)g_ascii_strtoull (text, NULL, 10);
  text = gtk_entry_get_text (GTK_ENTRY (gconfig->rate_control.scene_cuts.max_idr_frame_interval));
  x264_gtk->max_idr_frame_interval = (gint)g_ascii_strtoull (text, NULL, 10);

  text = gtk_entry_get_text (GTK_ENTRY (gconfig->rate_control.vbv.vbv_max_bitrate));
  x264_gtk->vbv_max_bitrate = (gint)g_ascii_strtoull (text, NULL, 10);
  text = gtk_entry_get_text (GTK_ENTRY (gconfig->rate_control.vbv.vbv_buffer_size));
  x264_gtk->vbv_buffer_size = (gint)g_ascii_strtoull (text, NULL, 10);
  text = gtk_entry_get_text (GTK_ENTRY (gconfig->rate_control.vbv.vbv_buffer_init));
  x264_gtk->vbv_buffer_init = (gint)g_ascii_strtoull (text, NULL, 10);

  /* mb */
  if (gtk_toggle_button_get_active (GTK_TOGGLE_BUTTON (gconfig->mb.partitions.transform_8x8)))
    x264_gtk->transform_8x8 = 1;
  else
    x264_gtk->transform_8x8 = 0;
  if (gtk_toggle_button_get_active (GTK_TOGGLE_BUTTON (gconfig->mb.partitions.pframe_search_8)))
    x264_gtk->pframe_search_8 = 1;
  else
    x264_gtk->pframe_search_8 = 0;
  if (gtk_toggle_button_get_active (GTK_TOGGLE_BUTTON (gconfig->mb.partitions.bframe_search_8)))
    x264_gtk->bframe_search_8 = 1;
  else
    x264_gtk->bframe_search_8 = 0;
  if (gtk_toggle_button_get_active (GTK_TOGGLE_BUTTON (gconfig->mb.partitions.pframe_search_4)))
    x264_gtk->pframe_search_4 = 1;
  else
    x264_gtk->pframe_search_4 = 0;
  if (gtk_toggle_button_get_active (GTK_TOGGLE_BUTTON (gconfig->mb.partitions.inter_search_8)))
    x264_gtk->inter_search_8 = 1;
  else
    x264_gtk->inter_search_8 = 0;
  if (gtk_toggle_button_get_active (GTK_TOGGLE_BUTTON (gconfig->mb.partitions.inter_search_4)))
    x264_gtk->inter_search_4 = 1;
  else
    x264_gtk->inter_search_4 = 0;

  /* mb - bframes */
  if (gtk_toggle_button_get_active (GTK_TOGGLE_BUTTON (gconfig->mb.bframes.bframe_pyramid)))
    x264_gtk->bframe_pyramid = 1;
  else
    x264_gtk->bframe_pyramid = 0;
  if (gtk_toggle_button_get_active (GTK_TOGGLE_BUTTON (gconfig->mb.bframes.bidir_me)))
    x264_gtk->bidir_me = 1;
  else
    x264_gtk->bidir_me = 0;
  if (gtk_toggle_button_get_active (GTK_TOGGLE_BUTTON (gconfig->mb.bframes.bframe_adaptive)))
    x264_gtk->bframe_adaptive = 1;
  else
    x264_gtk->bframe_adaptive = 0;
  if (gtk_toggle_button_get_active (GTK_TOGGLE_BUTTON (gconfig->mb.bframes.weighted_bipred)))
    x264_gtk->weighted_bipred = 1;
  else
    x264_gtk->weighted_bipred = 0;
  text = gtk_entry_get_text (GTK_ENTRY (gconfig->mb.bframes.bframe));
  x264_gtk->bframe = (gint)g_ascii_strtoull (text, NULL, 10);
  x264_gtk->bframe_bias = (gint)gtk_range_get_value (GTK_RANGE (gconfig->mb.bframes.bframe_bias));

  switch (gtk_combo_box_get_active (GTK_COMBO_BOX (gconfig->mb.bframes.direct_mode)))
    {
    case 0:
      x264_gtk->direct_mode = X264_NONE;
    case 1:
      x264_gtk->direct_mode = X264_SPATIAL;
      break;
    case 2:
      x264_gtk->direct_mode = X264_TEMPORAL;
      break;
    default:
      x264_gtk->direct_mode = X264_AUTO;
      break;
    }

  /* more */
  switch (gtk_combo_box_get_active (GTK_COMBO_BOX (gconfig->more.motion_estimation.partition_decision)))
    {
    case 0:
      x264_gtk->partition_decision = X264_PD_1;
      break;
    case 1:
      x264_gtk->partition_decision = X264_PD_2;
      break;
    case 2:
      x264_gtk->partition_decision = X264_PD_3;
      break;
    case 3:
      x264_gtk->partition_decision = X264_PD_4;
      break;
    case 4:
      x264_gtk->partition_decision = X264_PD_5;
      break;
    case 5:
      x264_gtk->partition_decision = X264_PD_6;
      break;
    default:
      x264_gtk->partition_decision = X264_PD_6;
      x264_gtk->bframe_rdo = 1;
      break;
    }
  switch (gtk_combo_box_get_active (GTK_COMBO_BOX (gconfig->more.motion_estimation.method)))
    {
    case 0:
      x264_gtk->me_method = X264_ME_METHOD_DIAMOND;
      break;
    case 1:
      x264_gtk->me_method = X264_ME_METHOD_HEXAGONAL;
      break;
    case 2:
      x264_gtk->me_method = X264_ME_METHOD_UNEVEN_MULTIHEXA;
      break;
    default:
      x264_gtk->me_method = X264_ME_METHOD_EXHAUSTIVE;
      break;
    }
  text = gtk_entry_get_text (GTK_ENTRY (gconfig->more.motion_estimation.range));
  x264_gtk->range = (gint)g_ascii_strtoull (text, NULL, 10);
  if (gtk_toggle_button_get_active (GTK_TOGGLE_BUTTON (gconfig->more.motion_estimation.chroma_me)))
    x264_gtk->chroma_me = 1;
  else
    x264_gtk->chroma_me = 0;
  text = gtk_entry_get_text (GTK_ENTRY (gconfig->more.motion_estimation.max_ref_frames));
  x264_gtk->max_ref_frames = (gint)g_ascii_strtoull (text, NULL, 10);
  if (x264_gtk->max_ref_frames <= 0)
    x264_gtk->max_ref_frames = 1;
  if (gtk_toggle_button_get_active (GTK_TOGGLE_BUTTON (gconfig->more.motion_estimation.mixed_refs)) &&
      (x264_gtk->max_ref_frames >= 2))
    x264_gtk->mixed_refs = 1;
  else
    x264_gtk->mixed_refs = 0;
  if (gtk_toggle_button_get_active (GTK_TOGGLE_BUTTON (gconfig->more.motion_estimation.fast_pskip)))
    x264_gtk->fast_pskip = 1;
  else
    x264_gtk->fast_pskip = 0;
  if (gtk_toggle_button_get_active (GTK_TOGGLE_BUTTON (gconfig->more.motion_estimation.dct_decimate)))
    x264_gtk->dct_decimate = 1;
  else
    x264_gtk->dct_decimate = 0;


  text = gtk_entry_get_text (GTK_ENTRY (gconfig->more.misc.sample_ar_x));
  x264_gtk->sample_ar_x = (gint)g_ascii_strtoull (text, NULL, 10);
  text = gtk_entry_get_text (GTK_ENTRY (gconfig->more.misc.sample_ar_y));
  x264_gtk->sample_ar_y = (gint)g_ascii_strtoull (text, NULL, 10);
  x264_gtk->threads = (gint)gtk_spin_button_get_value (GTK_SPIN_BUTTON (gconfig->more.misc.threads));
  if (gtk_toggle_button_get_active (GTK_TOGGLE_BUTTON (gconfig->more.misc.cabac)))
    x264_gtk->cabac = 1;
  else
    x264_gtk->cabac = 0;
  x264_gtk->trellis = gtk_combo_box_get_active (GTK_COMBO_BOX (gconfig->more.misc.trellis));
  text = gtk_entry_get_text (GTK_ENTRY (gconfig->more.misc.noise_reduction));
  x264_gtk->noise_reduction = (gint)g_ascii_strtoull (text, NULL, 10);
  if (gtk_toggle_button_get_active (GTK_TOGGLE_BUTTON (gconfig->more.misc.df.deblocking_filter)))
    x264_gtk->deblocking_filter = 1;
  else
    x264_gtk->deblocking_filter = 0;
  x264_gtk->strength = (gint)gtk_range_get_value (GTK_RANGE (gconfig->more.misc.df.strength));
  x264_gtk->threshold = (gint)gtk_range_get_value (GTK_RANGE (gconfig->more.misc.df.threshold));

  switch (gtk_combo_box_get_active (GTK_COMBO_BOX (gconfig->more.debug.log_level)))
    {
    case 0:
      x264_gtk->debug_method = X264_DEBUG_METHOD_NONE;
      break;
    case 1:
      x264_gtk->debug_method = X264_DEBUG_METHOD_ERROR;
      break;
    case 2:
      x264_gtk->debug_method = X264_DEBUG_METHOD_WARNING;
      break;
    case 3:
      x264_gtk->debug_method = X264_DEBUG_METHOD_INFO;
      break;
    default:
      x264_gtk->debug_method = X264_DEBUG_METHOD_DEBUG;
      break;
    }
  text = gtk_entry_get_text (GTK_ENTRY (gconfig->more.debug.fourcc));
  memcpy (x264_gtk->fourcc, text, strlen (text) + 1);

  /* cqm */
  if (gtk_toggle_button_get_active (GTK_TOGGLE_BUTTON (gconfig->cqm.radio_flat)))
    x264_gtk->cqm_preset = X264_CQM_PRESET_FLAT;
  if (gtk_toggle_button_get_active (GTK_TOGGLE_BUTTON (gconfig->cqm.radio_jvt)))
    x264_gtk->cqm_preset = X264_CQM_PRESET_JVT;
  if (gtk_toggle_button_get_active (GTK_TOGGLE_BUTTON (gconfig->cqm.radio_custom)))
    x264_gtk->cqm_preset = X264_CQM_PRESET_CUSTOM;
  text = gtk_file_chooser_get_filename (GTK_FILE_CHOOSER (gconfig->cqm.cqm_file));
  if (text && (text[0] != '\0'))
    memcpy (x264_gtk->cqm_file, text, strlen (text) + 1);
  for (i = 0; i < 16; i++) {
    text = gtk_entry_get_text (GTK_ENTRY (gconfig->cqm.cqm_4iy[i]));
    x264_gtk->cqm_4iy[i] = (gint)g_ascii_strtoull (text, NULL, 10);
    text = gtk_entry_get_text (GTK_ENTRY (gconfig->cqm.cqm_4ic[i]));
    x264_gtk->cqm_4ic[i] = (gint)g_ascii_strtoull (text, NULL, 10);
    text = gtk_entry_get_text (GTK_ENTRY (gconfig->cqm.cqm_4py[i]));
    x264_gtk->cqm_4py[i] = (gint)g_ascii_strtoull (text, NULL, 10);
    text = gtk_entry_get_text (GTK_ENTRY (gconfig->cqm.cqm_4pc[i]));
    x264_gtk->cqm_4pc[i] = (gint)g_ascii_strtoull (text, NULL, 10);
  }
  for (i = 0; i < 64; i++) {
    text = gtk_entry_get_text (GTK_ENTRY (gconfig->cqm.cqm_8iy[i]));
    x264_gtk->cqm_8iy[i] = (gint)g_ascii_strtoull (text, NULL, 10);
    text = gtk_entry_get_text (GTK_ENTRY (gconfig->cqm.cqm_8py[i]));
    x264_gtk->cqm_8py[i] = (gint)g_ascii_strtoull (text, NULL, 10);
  }
}

gchar*
x264_gtk_path(const char* more)
{
#ifdef _WIN32
  gchar szPath[MAX_PATH];

  // "Documents and Settings\user" is CSIDL_PROFILE
  szPath[0] = 0;

  SHGetSpecialFolderPath(NULL, szPath, CSIDL_APPDATA, FALSE);
  if (more)
    return g_build_filename(szPath, "x264", more, NULL);
  else
    return g_build_filename(szPath, "x264", NULL);
#else
  if (more)
    return g_build_filename (g_get_home_dir (), ".x264", more, NULL);
  else
    return g_build_filename (g_get_home_dir (), ".x264", NULL);
#endif
}
