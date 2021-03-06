/**
 * @addtogroup Optional_Look
 * @{
 *
 * @defgroup Module_Shot Screenshot
 *
 * Takes screen shots and can submit them to http://enlightenment.org
 *
 * @}
 */
#include "e.h"
#ifdef HAVE_WAYLAND
# include "screenshooter-client-protocol.h"
#endif
#include <time.h>
#include <sys/mman.h>

static E_Module *shot_module = NULL;

static E_Action *border_act = NULL, *act = NULL;
static E_Int_Menu_Augmentation *maug = NULL;
static Ecore_Timer *timer, *border_timer = NULL;
static Evas_Object *win = NULL;
E_Confirm_Dialog *cd = NULL;
static Evas_Object *o_bg = NULL, *o_box = NULL, *o_content = NULL;
static Evas_Object *o_event = NULL, *o_img = NULL, *o_hlist = NULL;
static int quality = 90;
static int screen = -1;
#define MAXZONES 64
static Evas_Object *o_rectdim[MAXZONES] = { NULL };
static Evas_Object *o_radio_all = NULL;
static Evas_Object *o_radio[MAXZONES] = { NULL };
static Evas_Object *o_fsel = NULL;
static Evas_Object *o_label = NULL;
static Evas_Object *o_entry = NULL;
static unsigned char *fdata = NULL;
static int fsize = 0;
static Ecore_Con_Url *url_up = NULL;
static Eina_List *handlers = NULL;
static char *url_ret = NULL;
static E_Dialog *fsel_dia = NULL;
static E_Client_Menu_Hook *border_hook = NULL;

#ifdef HAVE_WAYLAND
Eina_Bool copy_done = EINA_FALSE;
static struct screenshooter *_wl_screenshooter;
static Eina_List *_outputs;
struct screenshooter_output
{
   struct wl_output *output;
   struct wl_buffer *buffer;
   int x, y, w, h;
   uint32_t id;
   void *data;
};
#endif

static void _file_select_ok_cb(void *data EINA_UNUSED, E_Dialog *dia);
static void _file_select_cancel_cb(void *data EINA_UNUSED, E_Dialog *dia);

static void
_win_cancel_cb(void *data EINA_UNUSED, void *data2 EINA_UNUSED)
{
   E_FREE_FUNC(win, evas_object_del);
}

#ifdef HAVE_WAYLAND
static void
_wl_cb_screenshot_done(void *data EINA_UNUSED, struct screenshooter *screenshooter EINA_UNUSED)
{
   copy_done = EINA_TRUE;
}

static const struct screenshooter_listener _screenshooter_listener =
{
   _wl_cb_screenshot_done
};

static void
_wl_output_cb_handle_geometry(void *data EINA_UNUSED, struct wl_output *wl_output, int x, int y, int pw EINA_UNUSED, int ph EINA_UNUSED, int subpixel EINA_UNUSED, const char *make EINA_UNUSED, const char *model EINA_UNUSED, int transform EINA_UNUSED)
{
   struct screenshooter_output *output;

   output = wl_output_get_user_data(wl_output);
   if ((output) && (wl_output == output->output))
     {
        output->x = x;
        output->y = y;
     }
}

static void
_wl_output_cb_handle_mode(void *data EINA_UNUSED, struct wl_output *wl_output, uint32_t flags, int w, int h, int refresh EINA_UNUSED)
{
   struct screenshooter_output *output;

   output = wl_output_get_user_data(wl_output);
   if ((output) &&
       ((wl_output == output->output) && (flags & WL_OUTPUT_MODE_CURRENT)))
     {
        output->w = w;
        output->h = h;
     }
}

static void
_wl_output_cb_handle_done(void *data EINA_UNUSED, struct wl_output *output EINA_UNUSED)
{

}

static void
_wl_output_cb_handle_scale(void *data EINA_UNUSED, struct wl_output *output EINA_UNUSED, int32_t scale EINA_UNUSED)
{

}

static const struct wl_output_listener _output_listener =
{
   _wl_output_cb_handle_geometry,
   _wl_output_cb_handle_mode,
   _wl_output_cb_handle_done,
   _wl_output_cb_handle_scale
};
#endif

static void
_win_delete_cb()
{
   win = NULL;
}

static void
_on_focus_cb(void *data EINA_UNUSED, Evas_Object *obj)
{
   if (obj == o_content) e_widget_focused_object_clear(o_box);
   else if (o_content) e_widget_focused_object_clear(o_content);
}

static void
_key_down_cb(void *data EINA_UNUSED, Evas *e EINA_UNUSED, Evas_Object *obj EINA_UNUSED, void *event)
{
   Evas_Event_Key_Down *ev = event;

   if (!strcmp(ev->key, "Tab"))
     {
        if (evas_key_modifier_is_set(evas_key_modifier_get(evas_object_evas_get(win)), "Shift"))
          {
             if (e_widget_focus_get(o_box))
               {
                  if (!e_widget_focus_jump(o_box, 0))
                    {
                       e_widget_focus_set(o_content, 0);
                       if (!e_widget_focus_get(o_content))
                          e_widget_focus_set(o_box, 0);
                    }
               }
             else
               {
                  if (!e_widget_focus_jump(o_content, 0))
                     e_widget_focus_set(o_box, 0);
               }
          }
        else
          {
             if (e_widget_focus_get(o_box))
               {
                  if (!e_widget_focus_jump(o_box, 1))
                    {
                       e_widget_focus_set(o_content, 1);
                       if (!e_widget_focus_get(o_content))
                          e_widget_focus_set(o_box, 1);
                    }
               }
             else
               {
                  if (!e_widget_focus_jump(o_content, 1))
                     e_widget_focus_set(o_box, 1);
               }
          }
     }
   else if (((!strcmp(ev->key, "Return")) ||
             (!strcmp(ev->key, "KP_Enter")) ||
             (!strcmp(ev->key, "space"))))
     {
        Evas_Object *o = NULL;

        if ((o_content) && (e_widget_focus_get(o_content)))
          o = e_widget_focused_object_get(o_content);
        else
          o = e_widget_focused_object_get(o_box);
        if (o) e_widget_activate(o);
     }
   else if (!strcmp(ev->key, "Escape"))
     _win_cancel_cb(NULL, NULL);
}

static void
_save_key_down_cb(void *data EINA_UNUSED, Evas *e EINA_UNUSED, Evas_Object *obj EINA_UNUSED, void *event)
{
   Evas_Event_Key_Down *ev = event;

   if ((!strcmp(ev->key, "Return")) || (!strcmp(ev->key, "KP_Enter")))
     _file_select_ok_cb(NULL, fsel_dia);
   else if (!strcmp(ev->key, "Escape"))
     _file_select_cancel_cb(NULL, fsel_dia);
}

static void
_screen_change_cb(void *data EINA_UNUSED, Evas_Object *obj EINA_UNUSED, void *event_info EINA_UNUSED)
{
   Eina_List *l;
   E_Zone *z;

   EINA_LIST_FOREACH(e_comp->zones, l, z)
     {
        if (screen == -1)
          evas_object_color_set(o_rectdim[z->num], 0, 0, 0, 0);
        else if (screen == (int)z->num)
          evas_object_color_set(o_rectdim[z->num], 0, 0, 0, 0);
        else
          evas_object_color_set(o_rectdim[z->num], 0, 0, 0, 200);
     }
}

static void
_save_to(const char *file)
{
   char opts[256];

   if (eina_str_has_extension(file, ".png"))
     snprintf(opts, sizeof(opts), "compress=%i", 9);
   else
     snprintf(opts, sizeof(opts), "quality=%i", quality);
   if (screen == -1)
     {
        if (o_img)
          {
             if (!evas_object_image_save(o_img, file, NULL, opts))
               e_util_dialog_show(_("Error saving screenshot file"),
                                  _("Path: %s"), file);
          }
     }
   else
     {
        Evas_Object *o;
        Eina_List *l;
        E_Zone *z = NULL;

        EINA_LIST_FOREACH(e_comp->zones, l, z)
          {
             if (screen == (int)z->num) break;
             z = NULL;
          }
        if (z)
          {
             unsigned char *src, *dst, *s, *d;
             int sstd, dstd, y;

             o = evas_object_image_add(evas_object_evas_get(o_img));
             evas_object_image_colorspace_set(o, EVAS_COLORSPACE_ARGB8888);
             evas_object_image_alpha_set(o, EINA_FALSE);
             evas_object_image_size_set(o, z->w, z->h);
             dstd = evas_object_image_stride_get(o);
             src = evas_object_image_data_get(o_img, EINA_FALSE);
             sstd = evas_object_image_stride_get(o_img);
             dst = evas_object_image_data_get(o, EINA_TRUE);
             if ((dstd > 0) && (sstd > 0) && (src) && (dst))
               {
                  d = dst;
                  for (y = z->y; y < z->y + z->h; y++)
                    {
                       s = src + (sstd * y) + (z->x * 4);
                       memcpy(d, s, z->w * 4);
                       d += dstd;
                    }
                  if (!evas_object_image_save(o, file, NULL, opts))
                    e_util_dialog_show(_("Error saving screenshot file"),
                                       _("Path: %s"), file);
               }

             evas_object_del(o);
          }
     }
}

static void
_file_select_ok_cb(void *data EINA_UNUSED, E_Dialog *dia)
{
   const char *file;

   dia = fsel_dia;
   file = e_widget_fsel_selection_path_get(o_fsel);
   if ((!file) || (!file[0]) ||
       ((!eina_str_has_extension(file, ".jpg")) &&
           (!eina_str_has_extension(file, ".png"))))
     {
        e_util_dialog_show
        (_("Error - Unknown format"),
            _("File has an unspecified extension.<br>"
              "Please use '.jpg' or '.png' extensions<br>"
              "only as other formats are not<br>"
              "supported currently."));
        return;
     }
   _save_to(file);
   if (dia) e_util_defer_object_del(E_OBJECT(dia));
   E_FREE_FUNC(win, evas_object_del);
   fsel_dia = NULL;
}

static void
_file_select_cancel_cb(void *data EINA_UNUSED, E_Dialog *dia)
{
   if (dia) e_util_defer_object_del(E_OBJECT(dia));
   fsel_dia = NULL;
}

static void
_file_select_del_cb(void *d EINA_UNUSED)
{
   fsel_dia = NULL;
}

static void
_win_save_cb(void *data EINA_UNUSED, void *data2 EINA_UNUSED)
{ 
   E_Dialog *dia;
   Evas_Object *o;
   Evas_Coord mw, mh;
   int mask = 0;
   time_t tt;
   struct tm *tm;
   char buf[PATH_MAX];

   time(&tt);
   tm = localtime(&tt);
   if (quality == 100)
     strftime(buf, sizeof(buf), "shot-%Y-%m-%d_%H-%M-%S.png", tm);
   else
     strftime(buf, sizeof(buf), "shot-%Y-%m-%d_%H-%M-%S.jpg", tm);
   fsel_dia = dia = e_dialog_new(NULL, "E", "_e_shot_fsel");
   e_dialog_resizable_set(dia, EINA_TRUE);
   e_dialog_title_set(dia, _("Select screenshot save location"));
   o = e_widget_fsel_add(evas_object_evas_get(dia->win), "desktop", "/", 
                         buf, NULL, NULL, NULL, NULL, NULL, 1);
   e_object_del_attach_func_set(E_OBJECT(dia), _file_select_del_cb);
   e_widget_fsel_window_set(o, dia->win);
   o_fsel = o;
   evas_object_show(o);
   e_widget_size_min_get(o, &mw, &mh);
   e_dialog_content_set(dia, o, mw, mh);
   e_dialog_button_add(dia, _("Save"), NULL,
                       _file_select_ok_cb, NULL);
   e_dialog_button_add(dia, _("Cancel"), NULL,
                       _file_select_cancel_cb, NULL);
   elm_win_center(dia->win, 1, 1);
   o = evas_object_rectangle_add(evas_object_evas_get(dia->win));
   if (!evas_object_key_grab(o, "Return", mask, ~mask, 0))
     printf("grab err\n");
   mask = 0;
   if (!evas_object_key_grab(o, "KP_Enter", mask, ~mask, 0))
     printf("grab err\n");
   mask = 0;
   if (!evas_object_key_grab(o, "Escape", mask, ~mask, 0))
     printf("grab err\n");
   evas_object_event_callback_add(o, EVAS_CALLBACK_KEY_DOWN,
                                  _save_key_down_cb, NULL);
   e_dialog_show(dia);
}

static void
_share_done(void)
{
   E_FREE_LIST(handlers, ecore_event_handler_del);
   o_label = NULL;
   E_FREE(url_ret);
   E_FREE_FUNC(url_up, ecore_con_url_free);
   url_up = NULL;
}

static void
_upload_ok_cb(void *data EINA_UNUSED, E_Dialog *dia)
{
   // ok just hides dialog and does background upload
   o_label = NULL;
   if (dia) e_util_defer_object_del(E_OBJECT(dia));
   if (!win) return;
   E_FREE_FUNC(win, evas_object_del);
}

static void
_upload_cancel_cb(void *data EINA_UNUSED, E_Dialog *dia)
{
   o_label = NULL;
   if (dia) e_util_defer_object_del(E_OBJECT(dia));
   E_FREE_FUNC(win, evas_object_del);
   _share_done();
}

static Eina_Bool
_upload_data_cb(void *data EINA_UNUSED, int ev_type EINA_UNUSED, void *event)
{
   Ecore_Con_Event_Url_Data *ev = event;

   if (ev->url_con != url_up) return EINA_TRUE;
   if ((o_label) && (ev->size < 1024))
     {
        char *txt = alloca(ev->size + 1);

        memcpy(txt, ev->data, ev->size);
        txt[ev->size] = 0;
/*
        printf("GOT %i bytes: '%s'\n", ev->size, txt);
        int i;
        for (i = 0; i < ev->size; i++) printf("%02x.", ev->data[i]);
        printf("\n");
 */
        if (!url_ret) url_ret = strdup(txt);
        else
          {
             char *n;

             n = malloc(strlen(url_ret) + ev->size + 1);
             if (n)
               {
                  strcpy(n, url_ret);
                  free(url_ret);
                  strcat(n, txt);
                  url_ret = n;
               }
          }
     }
   return EINA_FALSE;
}

static Eina_Bool
_upload_progress_cb(void *data EINA_UNUSED, int ev_type EINA_UNUSED, void *event)
{
   size_t total, current;
   Ecore_Con_Event_Url_Progress *ev = event;

   if (ev->url_con != url_up) return ECORE_CALLBACK_RENEW;
   total = ev->up.total;
   current = ev->up.now;
   if (o_label)
     {
        char buf[1024];
        char *buf_now, *buf_total;

        buf_now = e_util_size_string_get(current);
        buf_total = e_util_size_string_get(total);
        snprintf(buf, sizeof(buf), _("Uploaded %s / %s"), buf_now, buf_total);
        E_FREE(buf_now);
        E_FREE(buf_total);
        e_widget_label_text_set(o_label, buf);
     }
   return ECORE_CALLBACK_RENEW;
}

static Eina_Bool
_upload_complete_cb(void *data, int ev_type EINA_UNUSED, void *event)
{
   int status;
   Ecore_Con_Event_Url_Complete *ev = event;

   if (ev->url_con != url_up) return ECORE_CALLBACK_RENEW;
   status = ev->status;

   if (data)
     e_widget_disabled_set(data, 1);
   if (status != 200)
     {
        e_util_dialog_show(_("Error - Upload Failed"),
                           _("Upload failed with status code:<br>"
                             "%i"), status);
        _share_done();
        return EINA_FALSE;
     }
   if ((o_entry) && (url_ret))
      e_widget_entry_text_set(o_entry, url_ret);
   _share_done();
   return ECORE_CALLBACK_RENEW;
}

static void
_win_share_del(void *data EINA_UNUSED)
{
   if (handlers)
     ecore_event_handler_data_set(eina_list_last_data_get(handlers), NULL);
   _upload_cancel_cb(NULL, NULL);
   if (cd) e_object_del(E_OBJECT(cd));
}

static void
_win_share_cb(void *data EINA_UNUSED, void *data2 EINA_UNUSED)
{
   E_Dialog *dia;
   Evas_Object *o, *ol;
   Evas_Coord mw, mh;
   char buf[PATH_MAX];
   FILE *f;
   int i, fd = -1;

   srand(time(NULL));
   for (i = 0; i < 10240; i++)
     {
        int v = rand();

        if (quality == 100)
          snprintf(buf, sizeof(buf), "/tmp/e-shot-%x.png", v);
        else
          snprintf(buf, sizeof(buf), "/tmp/e-shot-%x.jpg", v);
        fd = open(buf, O_RDWR | O_CREAT | O_EXCL, S_IRUSR | S_IWUSR);
        if (fd >= 0) break;
     }
   if (fd < 0)
     {
        e_util_dialog_show(_("Error - Can't create file"),
                           _("Cannot create temporary file '%s': %s"),
                           buf, strerror(errno));
        E_FREE_FUNC(win, evas_object_del);
        return;
     }
   _save_to(buf);
   E_FREE_FUNC(win, evas_object_del);
   f = fdopen(fd, "rb");
   if (!f)
     {
        e_util_dialog_show(_("Error - Can't open file"),
                           _("Cannot open temporary file '%s': %s"),
                           buf, strerror(errno));
        return;
     }
   fseek(f, 0, SEEK_END);
   fsize = ftell(f);
   if (fsize < 1)
     {
        e_util_dialog_show(_("Error - Bad size"),
                           _("Cannot get size of file '%s'"),
                           buf);
        fclose(f);
        return;
     }
   rewind(f);
   free(fdata);
   fdata = malloc(fsize);
   if (!fdata)
     {
        e_util_dialog_show(_("Error - Can't allocate memory"),
                           _("Cannot allocate memory for picture: %s"),
                           strerror(errno));
        fclose(f);
        return;
     }
   if (fread(fdata, fsize, 1, f) != 1)
     {
        e_util_dialog_show(_("Error - Can't read picture"),
                           _("Cannot read picture"));
        E_FREE(fdata);
        fclose(f);
        return;
     }
   fclose(f);
   ecore_file_unlink(buf);

   _share_done();

   E_LIST_HANDLER_APPEND(handlers, ECORE_CON_EVENT_URL_DATA,
                         _upload_data_cb, NULL);
   E_LIST_HANDLER_APPEND(handlers, ECORE_CON_EVENT_URL_PROGRESS,
                         _upload_progress_cb, NULL);

   url_up = ecore_con_url_new("https://www.enlightenment.org/shot.php");
   // why use http 1.1? proxies like squid don't handle 1.1 posts with expect
   // like curl uses by default, so go to 1.0 and this all works dandily
   // out of the box
   ecore_con_url_http_version_set(url_up, ECORE_CON_URL_HTTP_VERSION_1_0);
   ecore_con_url_post(url_up, fdata, fsize, "application/x-e-shot");
   dia = e_dialog_new(NULL, "E", "_e_shot_share");
   e_dialog_resizable_set(dia, EINA_TRUE);
   e_dialog_title_set(dia, _("Uploading screenshot"));

   o = e_widget_list_add(evas_object_evas_get(dia->win), 0, 0);
   ol = o;

   o = e_widget_label_add(evas_object_evas_get(dia->win), _("Uploading ..."));
   o_label = o;
   e_widget_list_object_append(ol, o, 0, 0, 0.5);

   o = e_widget_label_add(evas_object_evas_get(dia->win), 
                          _("Screenshot is available at this location:"));
   e_widget_list_object_append(ol, o, 0, 0, 0.5);

   o = e_widget_entry_add(dia->win, NULL, NULL, NULL, NULL);
   o_entry = o;
   e_widget_list_object_append(ol, o, 1, 0, 0.5);

   e_widget_size_min_get(ol, &mw, &mh);
   e_dialog_content_set(dia, ol, mw, mh);
   e_dialog_button_add(dia, _("Hide"), NULL, _upload_ok_cb, NULL);
   e_dialog_button_add(dia, _("Cancel"), NULL, _upload_cancel_cb, NULL);
   e_object_del_attach_func_set(E_OBJECT(dia), _win_share_del);
   E_LIST_HANDLER_APPEND(handlers, ECORE_CON_EVENT_URL_COMPLETE,
                         _upload_complete_cb,
                         eina_list_last_data_get(dia->buttons));
   elm_win_center(dia->win, 1, 1);
   e_dialog_show(dia);
}

static void
_win_share_confirm_del(void *d EINA_UNUSED)
{
   cd = NULL;
}

static void
_win_share_confirm_yes(void *d EINA_UNUSED)
{
   _win_share_cb(NULL, NULL);
}

static void
_win_share_confirm_cb(void *d EINA_UNUSED, void *d2 EINA_UNUSED)
{
   if (cd) return;
   cd = e_confirm_dialog_show(_("Confirm Share"), NULL,
                              _("This image will be uploaded<br>"
                                "to enlightenment.org. It will be publicly visible."),
                              _("Confirm"), _("Cancel"),
                              _win_share_confirm_yes, NULL,
                              NULL, NULL, _win_share_confirm_del, NULL);
}

static void
_rect_down_cb(void *data EINA_UNUSED, Evas *e EINA_UNUSED, Evas_Object *obj EINA_UNUSED, void *event_info)
{
   Evas_Event_Mouse_Down *ev = event_info;
   Eina_List *l;
   E_Zone *z;

   if (ev->button != 1) return;

   e_widget_radio_toggle_set(o_radio_all, 0);
   EINA_LIST_FOREACH(e_comp->zones, l, z)
     {
        if (obj == o_rectdim[z->num])
          {
             screen = z->num;
             e_widget_radio_toggle_set(o_radio[z->num], 1);
          }
        else
           e_widget_radio_toggle_set(o_radio[z->num], 0);
     }

   EINA_LIST_FOREACH(e_comp->zones, l, z)
     {
        if (screen == -1)
           evas_object_color_set(o_rectdim[z->num], 0, 0, 0, 0);
        else if (screen == (int)z->num)
           evas_object_color_set(o_rectdim[z->num], 0, 0, 0, 0);
        else
           evas_object_color_set(o_rectdim[z->num], 0, 0, 0, 200);
     }
}

static void
_save_dialog_show(E_Zone *zone, E_Client *ec, const char *params, void *dst, int sw, int sh)
{
   Evas *evas, *evas2;
   Evas_Object *o, *oa, *op, *ol;
   Evas_Modifier_Mask mask;
   E_Radio_Group *rg;
   int w, h;

   win = elm_win_add(NULL, NULL, ELM_WIN_BASIC);

   evas = evas_object_evas_get(win);
   elm_win_title_set(win, _("Where to put Screenshot..."));
   evas_object_event_callback_add(win, EVAS_CALLBACK_DEL, _win_delete_cb, NULL);
   elm_win_center(win, 1, 1);
   ecore_evas_name_class_set(e_win_ee_get(win), "E", "_shot_dialog");

   o = elm_layout_add(e_win_evas_win_get(evas));
   elm_win_resize_object_add(win, o);
   o_bg = o;;
   e_theme_edje_object_set(o, "base/theme/dialog", "e/widgets/dialog/main");
   evas_object_show(o);

   o = e_widget_list_add(evas, 0, 0);
   o_content = o;
   elm_object_part_content_set(o_bg, "e.swallow.content", o);
   evas_object_show(o);

   w = sw / 4;
   if (w < 220) w = 220;
   h = (w * sh) / sw;

   o = e_widget_aspect_add(evas, w, h);
   oa = o;
   o = e_widget_preview_add(evas, w, h);
   op = o;

   evas2 = e_widget_preview_evas_get(op);

   o = evas_object_image_filled_add(evas2);
   o_img = o;
   evas_object_image_colorspace_set(o, EVAS_COLORSPACE_ARGB8888);
   evas_object_image_alpha_set(o, EINA_FALSE);
   evas_object_image_size_set(o, sw, sh);
   evas_object_image_data_copy_set(o, dst);

   evas_object_image_data_update_add(o, 0, 0, sw, sh);
   e_widget_preview_extern_object_set(op, o);
   evas_object_show(o);

   evas_object_show(op);
   evas_object_show(oa);

   e_widget_aspect_child_set(oa, op);
   e_widget_list_object_append(o_content, oa, 0, 0, 0.5);

   o = e_widget_list_add(evas, 1, 1);
   o_hlist = o;

   o = e_widget_framelist_add(evas, _("Quality"), 0);
   ol = o;

   rg = e_widget_radio_group_new(&quality);
   o = e_widget_radio_add(evas, _("Perfect"), 100, rg);
   e_widget_framelist_object_append(ol, o);
   o = e_widget_radio_add(evas, _("High"), 90, rg);
   e_widget_framelist_object_append(ol, o);
   o = e_widget_radio_add(evas, _("Medium"), 70, rg);
   e_widget_framelist_object_append(ol, o);
   o = e_widget_radio_add(evas, _("Low"), 50, rg);
   e_widget_framelist_object_append(ol, o);

   e_widget_list_object_append(o_hlist, ol, 1, 0, 0.5);

   if (zone)
     {
        screen = -1;
        if (eina_list_count(e_comp->zones) > 1)
          {
             Eina_List *l;
             E_Zone *z;
             int i;

             o = e_widget_framelist_add(evas, _("Screen"), 0);
             ol = o;

             rg = e_widget_radio_group_new(&screen);
             o = e_widget_radio_add(evas, _("All"), -1, rg);
             o_radio_all = o;
             evas_object_smart_callback_add(o, "changed",
                                            _screen_change_cb, NULL);
             e_widget_framelist_object_append(ol, o);
             i = 0;
             EINA_LIST_FOREACH(e_comp->zones, l, z)
               {
                  char buf[32];

                  if (z->num >= MAXZONES) continue;
                  snprintf(buf, sizeof(buf), "%i", z->num);
                  o = e_widget_radio_add(evas, buf, z->num, rg);
                  o_radio[z->num] = o;
                  evas_object_smart_callback_add(o, "changed",
                                                 _screen_change_cb, NULL);
                  e_widget_framelist_object_append(ol, o);

                  o = evas_object_rectangle_add(evas2);
                  evas_object_event_callback_add(o, EVAS_CALLBACK_MOUSE_DOWN,
                                                 _rect_down_cb, NULL);
                  o_rectdim[z->num] = o;
                  evas_object_color_set(o, 0, 0, 0, 0);
                  evas_object_show(o);
                  evas_object_geometry_get(o_img, NULL, NULL, &w, &h);
                  evas_object_move(o, (z->x * w) / sw, (z->y * h) / sh);
                  evas_object_resize(o, (z->w * w) / sw, (z->h * h) / sh);
                  i++;
               }

             e_widget_list_object_append(o_hlist, ol, 1, 0, 0.5);
          }

     }
   e_widget_list_object_append(o_content, o_hlist, 0, 0, 0.5);

   o = o_content;
   e_widget_size_min_get(o, &w, &h);
   evas_object_size_hint_min_set(o, w, h);
   elm_object_part_content_set(o_bg, "e.swallow.content", o);
   evas_object_show(o);

   ///////////////////////////////////////////////////////////////////////

   o = e_widget_list_add(evas, 1, 1);
   o_box = o;
   e_widget_on_focus_hook_set(o, _on_focus_cb, NULL);
   elm_object_part_content_set(o_bg, "e.swallow.buttons", o);

   o = e_widget_button_add(evas, _("Save"), NULL, _win_save_cb, win, NULL);
   e_widget_list_object_append(o_box, o, 1, 0, 0.5);
   o = e_widget_button_add(evas, _("Share"), NULL,
                           _win_share_confirm_cb, win, NULL);
   e_widget_list_object_append(o_box, o, 1, 0, 0.5);
   o = e_widget_button_add(evas, _("Cancel"), NULL, _win_cancel_cb, win, NULL);
   e_widget_list_object_append(o_box, o, 1, 0, 0.5);

   o = o_box;
   e_widget_size_min_get(o, &w, &h);
   evas_object_size_hint_min_set(o, w, h);
   elm_object_part_content_set(o_bg, "e.swallow.buttons", o);

   o = evas_object_rectangle_add(evas);
   o_event = o;
   mask = 0;
   if (!evas_object_key_grab(o, "Tab", mask, ~mask, 0)) printf("grab err\n");
   mask = evas_key_modifier_mask_get(evas, "Shift");
   if (!evas_object_key_grab(o, "Tab", mask, ~mask, 0)) printf("grab err\n");
   mask = 0;
   if (!evas_object_key_grab(o, "Return", mask, ~mask, 0)) printf("grab err\n");
   mask = 0;
   if (!evas_object_key_grab(o, "KP_Enter", mask, ~mask, 0)) printf("grab err\n");
   mask = 0;
   if (!evas_object_key_grab(o, "space", mask, ~mask, 0)) printf("grab err\n");
   mask = 0;
   if (!evas_object_key_grab(o, "Escape", mask, ~mask, 0)) printf("grab err\n");
   evas_object_event_callback_add(o, EVAS_CALLBACK_KEY_DOWN, _key_down_cb, NULL);

   evas_object_size_hint_min_get(o_bg, &w, &h);
   evas_object_resize(o_bg, w, h);
   evas_object_resize(win, w, h);
   evas_object_size_hint_min_set(win, w, h);
   evas_object_size_hint_max_set(win, 99999, 99999);

   if (params)
     {
        char smode[128], squal[128], sscreen[128];

        if (sscanf(params, "%100s %100s %100s", smode, squal, sscreen) == 3)
          {
             screen = -1;
             if ((zone) && (!strcmp(sscreen, "current"))) screen = zone->num;
             else if (!strcmp(sscreen, "all")) screen = -1;
             else screen = atoi(sscreen);

             quality = 90;
             if (!strcmp(squal, "perfect")) quality = 100;
             else if (!strcmp(squal, "high")) quality = 90;
             else if (!strcmp(squal, "medium")) quality = 70;
             else if (!strcmp(squal, "low")) quality = 50;
             else quality = atoi(squal);

             if (!strcmp(smode, "save")) _win_save_cb(NULL, NULL);
             else if (!strcmp(smode, "share"))  _win_share_cb(NULL, NULL);
          }
     }
   else
     {
        evas_object_show(win);
        e_win_client_icon_set(win, "screenshot");

        if (!e_widget_focus_get(o_bg)) e_widget_focus_set(o_box, 1);
        if (ec)
          {
             E_Client *c = e_win_client_get(win);

             if (c) evas_object_layer_set(c->frame, ec->layer);
          }
     }
}

#ifdef HAVE_WAYLAND
static struct wl_buffer *
_create_shm_buffer(struct wl_shm *_shm, int width, int height, void **data_out)
{
   char filename[] = "wayland-shm-XXXXXX";
   Eina_Tmpstr *tmpfile = NULL;
   struct wl_shm_pool *pool;
   struct wl_buffer *buffer;
   int fd, size, stride;
   void *data;

   fd = eina_file_mkstemp(filename, &tmpfile);
   if (fd < 0) 
     {
        fprintf(stderr, "open %s failed: %m\n", tmpfile);
        return NULL;
     }

   stride = width * 4;
   size = stride * height;
   if (ftruncate(fd, size) < 0) 
     {
        fprintf(stderr, "ftruncate failed: %m\n");
        close(fd);
        return NULL;
     }

   data = mmap(NULL, size, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
   unlink(tmpfile);
   eina_tmpstr_del(tmpfile);

   if (data == MAP_FAILED) 
     {
        fprintf(stderr, "mmap failed: %m\n");
        close(fd);
        return NULL;
     }

   pool = wl_shm_create_pool(_shm, fd, size);
   close(fd);

   buffer = 
     wl_shm_pool_create_buffer(pool, 0, width, height, stride,
                               WL_SHM_FORMAT_ARGB8888);
   wl_shm_pool_destroy(pool);

   *data_out = data;

   return buffer;
}
#endif

static void
_wl_shot_now(E_Zone *zone, E_Client *ec, const char *params)
{
#ifdef HAVE_WAYLAND
   Eina_List *l;
   struct screenshooter_output *output;
   struct wl_shm *shm;
   int x, y, sw, sh, i;
   int ostride, bstride;
   unsigned char *dst, *d, *s;

   if ((win) || (url_up)) return;
   if ((!zone) && (!ec)) return;

   if (zone)
     {
        sw = e_comp->w;
        sh = e_comp->h;
        x = y = 0;
     }
   else
     {
        x = ec->x, y = ec->y, sw = ec->w, sh = ec->h;
        x = E_CLAMP(x, ec->zone->x, ec->zone->x + ec->zone->w);
        y = E_CLAMP(y, ec->zone->y, ec->zone->y + ec->zone->h);
        sw = E_CLAMP(sw, 1, ec->zone->x + ec->zone->w - x);
        sh = E_CLAMP(sh, 1, ec->zone->y + ec->zone->h - y);
     }

   shm = e_comp_wl->wl.shm ?: ecore_wl2_display_shm_get(e_comp_wl->wl.client_disp);

   e_bindings_disabled_set(1);
   EINA_LIST_FOREACH(_outputs, l, output)
     {
        if ((!zone) &&
            (!E_CONTAINS(output->x, output->y, output->w, output->h,
                         x, y, sw, sh)))
          continue;

        output->buffer =
          _create_shm_buffer(shm, output->w, output->h, &output->data);

        screenshooter_shoot(_wl_screenshooter, output->output, output->buffer);

        copy_done = EINA_FALSE;
        while (!copy_done)
          ecore_main_loop_iterate();
     }
   e_bindings_disabled_set(0);

   bstride = sw * sizeof(int);
   dst = malloc(bstride * sh);

   EINA_LIST_FOREACH(_outputs, l, output)
     {
        if ((!zone) &&
            (!E_CONTAINS(output->x, output->y, output->w, output->h,
                         x, y, sw, sh)))
          continue;

        ostride = output->w * sizeof(int);
        s = output->data;
        if (zone)
          {
             d = dst + (output->y * bstride + output->x * sizeof(int));
             for (i = 0; i < output->h; i++)
               {
                  memcpy(d, s, ostride);
                  d += bstride;
                  s += ostride;
               }
          }
        else
          {
             d = dst;
             for (i = y; i < (y + sh); i++)
               {
                  s = output->data;
                  s += (i * ostride) + (x * sizeof(int));
                  memcpy(d, s, bstride);
                  d += bstride;
               }
          }
     }

   _save_dialog_show(zone, ec, params, dst, sw, sh);

   free(dst);
#else
   (void)zone;
   (void)ec;
   (void)params;
#endif
}

static void
_x_shot_now(E_Zone *zone, E_Client *ec, const char *params)
{
#ifdef HAVE_WAYLAND_ONLY
   (void)zone;
   (void)ec;
   (void)params;
#else
   Ecore_X_Image *img;
   unsigned char *src;
   unsigned int *dst;
   int bpl = 0, rows = 0, bpp = 0, sw, sh;
   int x, y, w, h;
   Ecore_X_Window xwin;
   Ecore_X_Visual visual;
   Ecore_X_Display *display;
   Ecore_X_Screen *scr;
   Ecore_X_Window_Attributes watt;
   Ecore_X_Colormap colormap;
   int depth;

   if ((win) || (url_up)) return;
   if ((!zone) && (!ec)) return;
   if (zone)
     {
        xwin = e_comp->root;
        w = sw = e_comp->w;
        h = sh = e_comp->h;
        x = y = 0;
        if (!ecore_x_window_attributes_get(xwin, &watt)) return;
        visual = watt.visual;
        depth = watt.depth;
     }
   else
     {
        xwin = e_comp->ee_win;
        x = ec->x, y = ec->y, sw = ec->w, sh = ec->h;
        w = sw;
        h = sh;
        x = E_CLAMP(x, ec->zone->x, ec->zone->x + ec->zone->w);
        y = E_CLAMP(y, ec->zone->y, ec->zone->y + ec->zone->h);
        sw = E_CLAMP(sw, 1, ec->zone->x + ec->zone->w - x);
        sh = E_CLAMP(sh, 1, ec->zone->y + ec->zone->h - y);
        visual = e_pixmap_visual_get(ec->pixmap);
        depth = ec->depth;
     }
   img = ecore_x_image_new(w, h, visual, depth);
   if (!ecore_x_image_get(img, xwin, x, y, 0, 0, sw, sh))
     {
        Eina_Bool dialog = EINA_FALSE;
        ecore_x_image_free(img);
#ifdef __linux__
        FILE *f;

        f = fopen("/proc/sys/kernel/shmmax", "r");
        if (f)
          {
             long long unsigned int max = 0;

             int n = fscanf(f, "%llu", &max);
             if ((n > 0) && (max) && (max < (w * h * sizeof(int))))
               {
                  e_util_dialog_show(_("Screenshot Error"),
                                     _("SHMMAX is too small to take screenshot.<br>"
                                       "Consider increasing /proc/sys/kernel/shmmax to a value larger than %llu"),
                                     (long long unsigned int)(w * h * sizeof(int)));
                  dialog = EINA_TRUE;
               }
             fclose(f);
          }
#elif defined(__FreeBSD__) || defined(__FreeBSD_kernel__)
        int max;
        size_t len = sizeof(max);

        if (!sysctlbyname("kern.ipc.shmmax", &max, &len, NULL, 0))
          {
             if (max && (max < (w * h * sizeof(int))))
               {
                  e_util_dialog_show(_("Screenshot Error"),
                                     _("SHMMAX is too small to take screenshot.<br>"
                                       "Consider increasing kern.ipc.shmmax to a value larger than %llu"),
                                     (long long unsigned int)(w * h * sizeof(int)));
                  dialog = EINA_TRUE;
               }
          }
#endif
        if (!dialog)
          e_util_dialog_show(_("Screenshot Error"),
                             _("SHM creation failed.<br>"
                               "Ensure your system has enough RAM free and your user has sufficient permissions."));
        return;
     }
   src = ecore_x_image_data_get(img, &bpl, &rows, &bpp);
   display = ecore_x_display_get();
   scr = ecore_x_default_screen_get();
   colormap = ecore_x_default_colormap_get(display, scr);
   dst = malloc(sw * sh * sizeof(int));
   ecore_x_image_to_argb_convert(src, bpp, bpl, colormap, visual,
                                 0, 0, sw, sh,
                                 dst, (sw * sizeof(int)), 0, 0);

   _save_dialog_show(zone, ec, params, dst, sw, sh);

   free(dst);
   ecore_x_image_free(img);
#endif
}

static Eina_Bool
_shot_delay(void *data)
{
   timer = NULL;
   if (e_comp->comp_type == E_PIXMAP_TYPE_X)
     _x_shot_now(data, NULL, NULL);
   else
     _wl_shot_now(data, NULL, NULL);

   return EINA_FALSE;
}

static Eina_Bool
_shot_delay_border(void *data)
{
   border_timer = NULL;
   if (e_comp->comp_type == E_PIXMAP_TYPE_X)
     _x_shot_now(NULL, data, NULL);
   else
     _wl_shot_now(NULL, data, NULL);

   return EINA_FALSE;
}

static void
_shot_border(E_Client *ec)
{
   if (border_timer) ecore_timer_del(border_timer);
   border_timer = ecore_timer_add(1.0, _shot_delay_border, ec);
}

static void
_shot(E_Zone *zone)
{
   if (timer) ecore_timer_del(timer);
   timer = ecore_timer_add(1.0, _shot_delay, zone);
}

static void
_e_mod_menu_border_cb(void *data, E_Menu *m EINA_UNUSED, E_Menu_Item *mi EINA_UNUSED)
{
   _shot_border(data);
}

static void
_e_mod_menu_cb(void *data EINA_UNUSED, E_Menu *m, E_Menu_Item *mi EINA_UNUSED)
{
   if (m->zone) _shot(m->zone);
}

static void
_e_mod_action_border_cb(E_Object *obj EINA_UNUSED, const char *params EINA_UNUSED)
{
   E_Client *ec;

   ec = e_client_focused_get();
   if (!ec) return;
   if (border_timer)
     {
        ecore_timer_del(border_timer);
        border_timer = NULL;
     }
   if (e_comp->comp_type == E_PIXMAP_TYPE_X)
     _x_shot_now(NULL, ec, NULL);
   else
     _wl_shot_now(NULL, ec, NULL);
}

typedef struct
{
   E_Zone *zone;
   char *params;
} Delayed_Shot;

static void
_delayed_shot(void *data)
{
   Delayed_Shot *ds = data;

   if (e_comp->comp_type == E_PIXMAP_TYPE_X)
     _x_shot_now(ds->zone, NULL, ds->params);
   else
     _wl_shot_now(ds->zone, NULL, ds->params);
   e_object_unref(E_OBJECT(ds->zone));
   free(ds->params);
   free(ds);
}

static void
_e_mod_action_cb(E_Object *obj, const char *params)
{
   E_Zone *zone = NULL;
   Delayed_Shot *ds;

   if (obj)
     {
        if (obj->type == E_COMP_TYPE)
          zone = e_zone_current_get();
        else if (obj->type == E_ZONE_TYPE)
          zone = ((E_Zone *)obj);
        else
          zone = e_zone_current_get();
     }
   if (!zone) zone = e_zone_current_get();
   if (!zone) return;
   E_FREE_FUNC(timer, ecore_timer_del);
   ds = E_NEW(Delayed_Shot, 1);
   e_object_ref(E_OBJECT(zone));
   ds->zone = zone;
   ds->params = params ? strdup(params) : NULL;
   /* forced main loop iteration in screenshots causes bugs if the action
    * executes immediately
    */
   ecore_job_add(_delayed_shot, ds);
}

static void
_bd_hook(void *d EINA_UNUSED, E_Client *ec)
{
   E_Menu_Item *mi;
   E_Menu *m;
   Eina_List *l;

   if (!ec->border_menu) return;
   if (ec->iconic || (ec->desk != e_desk_current_get(ec->zone))) return;
   m = ec->border_menu;

   /* position menu item just before first separator */
   EINA_LIST_FOREACH(m->items, l, mi)
     if (mi->separator) break;
   if ((!mi) || (!mi->separator)) return;
   l = eina_list_prev(l);
   mi = eina_list_data_get(l);
   if (!mi) return;

   mi = e_menu_item_new_relative(m, mi);
   e_menu_item_label_set(mi, _("Take Shot"));
   e_util_menu_item_theme_icon_set(mi, "screenshot");
   e_menu_item_callback_set(mi, _e_mod_menu_border_cb, ec);
}

static void
_e_mod_menu_add(void *data EINA_UNUSED, E_Menu *m)
{
   E_Menu_Item *mi;

   mi = e_menu_item_new(m);
   e_menu_item_label_set(mi, _("Take Screenshot"));
   e_util_menu_item_theme_icon_set(mi, "screenshot");
   e_menu_item_callback_set(mi, _e_mod_menu_cb, NULL);
}

#ifdef HAVE_WAYLAND
static Ecore_Event_Handler *wl_global_handler;

static Eina_Bool
_wl_init()
{
   Eina_Iterator *itr;
   Ecore_Wl2_Global *global;
   struct wl_registry *reg;
   void *data;

   reg = e_comp_wl->wl.registry ?: ecore_wl2_display_registry_get(e_comp_wl->wl.client_disp);
   itr = ecore_wl2_display_globals_get(e_comp_wl->wl.client_disp);
   EINA_ITERATOR_FOREACH(itr, data)
     {
        global = (Ecore_Wl2_Global *)data;

        if ((!_wl_screenshooter) &&
            (!strcmp(global->interface, "screenshooter")))
          {
             _wl_screenshooter =
               wl_registry_bind(reg, global->id,
                                &screenshooter_interface, global->version);

             if (_wl_screenshooter)
               screenshooter_add_listener(_wl_screenshooter,
                                          &_screenshooter_listener,
                                          _wl_screenshooter);
          }
        else if (!strcmp(global->interface, "wl_output"))
          {
             struct screenshooter_output *output;
             Eina_List *l;

             EINA_LIST_FOREACH(_outputs, l, output)
               if (output->id == global->id) return ECORE_CALLBACK_RENEW;
             output = calloc(1, sizeof(*output));
             if (output)
               {
                  output->output =
                    wl_registry_bind(reg, global->id,
                                     &wl_output_interface, global->version);
                  _outputs = eina_list_append(_outputs, output);
                  wl_output_add_listener(output->output,
                                         &_output_listener, output);
               }
          }
     }
   eina_iterator_free(itr);

   return ECORE_CALLBACK_RENEW;
}
#endif

/* module setup */
E_API E_Module_Api e_modapi =
{
   E_MODULE_API_VERSION,
     "Shot"
};

E_API void *
e_modapi_init(E_Module *m)
{
   if (!ecore_con_url_init())
     {
        e_util_dialog_show(_("Shot Error"),
                           _("Cannot initialize network"));
        return NULL;
     }

   e_module_delayed_set(m, 1);

   shot_module = m;
   act = e_action_add("shot");
   if (act)
     {
        act->func.go = _e_mod_action_cb;
        e_action_predef_name_set(N_("Screen"), N_("Take Screenshot"),
                                 "shot", NULL,
                                 "syntax: [share|save [perfect|high|medium|low|QUALITY current|all|SCREEN-NUM]", 1);
     }
   border_act = e_action_add("border_shot");
   if (border_act)
     {
        border_act->func.go = _e_mod_action_border_cb;
        e_action_predef_name_set(N_("Window : Actions"), N_("Take Shot"),
                                 "border_shot", NULL,
                                 "syntax: [share|save perfect|high|medium|low|QUALITY all|current]", 1);
     }
   maug = e_int_menus_menu_augmentation_add_sorted
     ("main/2",  _("Take Screenshot"), _e_mod_menu_add, NULL, NULL, NULL);
   border_hook = e_int_client_menu_hook_add(_bd_hook, NULL);

#ifdef HAVE_WAYLAND
   if (e_comp->comp_type != E_PIXMAP_TYPE_X)
     _wl_init();
#endif

   return m;
}

E_API int
e_modapi_shutdown(E_Module *m EINA_UNUSED)
{
   _share_done();
   E_FREE_FUNC(win, evas_object_del);
   E_FREE_FUNC(cd, e_object_del);
   if (timer)
     {
        ecore_timer_del(timer);
        timer = NULL;
     }
   if (maug)
     {
        e_int_menus_menu_augmentation_del("main/2", maug);
        maug = NULL;
     }
   if (act)
     {
        e_action_predef_name_del("Screen", "Take Screenshot");
        e_action_del("shot");
        act = NULL;
     }
#ifdef HAVE_WAYLAND
   E_FREE_FUNC(wl_global_handler, ecore_event_handler_del);
#endif
   shot_module = NULL;
   e_int_client_menu_hook_del(border_hook);
   ecore_con_url_shutdown();
   return 1;
}

E_API int
e_modapi_save(E_Module *m EINA_UNUSED)
{
   return 1;
}
