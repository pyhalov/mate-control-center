/* -*- Mode: C; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 8 -*- */

/* mate-window-properties.c
 * Copyright (C) 2002 Seth Nickell
 * Copyright (C) 2002 Red Hat, Inc.
 *
 * Written by: Seth Nickell <snickell@stanford.edu>
 *             Havoc Pennington <hp@redhat.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2, or (at your option)
 * any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA
 * 02110-1301, USA.
 */

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include <stdlib.h>
#include <glib/gi18n.h>
#include <string.h>

#include <gdk/gdkx.h>

#include "mate-metacity-support.h"
#include "wm-common.h"
#include "capplet-util.h"

#define MARCO_SCHEMA "org.mate.Marco.general"
#define MARCO_THEME_KEY "theme"
#define MARCO_FONT_KEY  "titlebar-font"
#define MARCO_FOCUS_KEY "focus-mode"
#define MARCO_USE_SYSTEM_FONT_KEY "titlebar-uses-system-font"
#define MARCO_AUTORAISE_KEY "auto-raise"
#define MARCO_AUTORAISE_DELAY_KEY "auto-raise-delay"
#define MARCO_MOUSE_MODIFIER_KEY "mouse-button-modifier"
#define MARCO_DOUBLE_CLICK_TITLEBAR_KEY "action-double-click-titlebar"
#define MARCO_COMPOSITING_MANAGER_KEY "compositing-manager"
#define MARCO_COMPOSITING_FAST_ALT_TAB_KEY "compositing-fast-alt-tab"

/* keep following enums in sync with marco */
enum
{
        ACTION_TITLEBAR_TOGGLE_SHADE,
        ACTION_TITLEBAR_TOGGLE_MAXIMIZE,
        ACTION_TITLEBAR_TOGGLE_MAXIMIZE_HORIZONTALLY,
        ACTION_TITLEBAR_TOGGLE_MAXIMIZE_VERTICALLY,
        ACTION_TITLEBAR_MINIMIZE,
        ACTION_TITLEBAR_NONE,
        ACTION_TITLEBAR_LOWER,
        ACTION_TITLEBAR_MENU
};
enum
{
        FOCUS_MODE_CLICK,
        FOCUS_MODE_SLOPPY,
        FOCUS_MODE_MOUSE
};

typedef struct
{
        int number;
        char *name;
        const char *value; /* machine-readable name for storing config */
        GtkWidget *radio;
} MouseClickModifier;

static GtkWidget *dialog_win;
static GObject *compositing_checkbutton;
static GObject *compositing_fast_alt_tab_checkbutton;
static GObject *focus_mode_checkbutton;
static GObject *autoraise_checkbutton;
static GObject *autoraise_delay_slider;
static GtkWidget *autoraise_delay_hbox;
static GObject *double_click_titlebar_optionmenu;
static GObject *alt_click_hbox;

static GSettings *marco_settings;

static MouseClickModifier *mouse_modifiers = NULL;
static int n_mouse_modifiers = 0;

static void reload_mouse_modifiers (void);

static void
update_sensitivity ()
{
        gtk_widget_set_sensitive (GTK_WIDGET (compositing_fast_alt_tab_checkbutton),
                                  g_settings_get_boolean (marco_settings, MARCO_COMPOSITING_MANAGER_KEY));
        gtk_widget_set_sensitive (GTK_WIDGET (autoraise_checkbutton),
                                  g_settings_get_enum (marco_settings, MARCO_FOCUS_KEY) != FOCUS_MODE_CLICK);
        gtk_widget_set_sensitive (GTK_WIDGET (autoraise_delay_hbox),
                                  g_settings_get_enum (marco_settings, MARCO_FOCUS_KEY) != FOCUS_MODE_CLICK &&
                                  g_settings_get_boolean (marco_settings, MARCO_AUTORAISE_KEY));
}

static void
marco_settings_changed_callback (GSettings *settings,
                                 const gchar *key,
                                 gpointer user_data)
{
        update_sensitivity ();
}

static void
mouse_focus_toggled_callback (GtkWidget *button,
                              void      *data)
{
        g_settings_set_enum (marco_settings,
                             MARCO_FOCUS_KEY,
                             gtk_toggle_button_get_active (GTK_TOGGLE_BUTTON (button)) ?
                             FOCUS_MODE_SLOPPY : FOCUS_MODE_CLICK);
}

static void
mouse_focus_changed_callback (GSettings *settings,
                              const gchar *key,
                              gpointer user_data)
{
       gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (focus_mode_checkbutton),
                                     g_settings_get_enum (settings, key) == FOCUS_MODE_SLOPPY);
}

static void
autoraise_delay_value_changed_callback (GtkWidget *slider,
                                        void      *data)
{
        g_settings_set_int (marco_settings,
                            MARCO_AUTORAISE_DELAY_KEY,
                            gtk_range_get_value (GTK_RANGE (slider)) * 1000);
}

static void
double_click_titlebar_changed_callback (GtkWidget *optionmenu,
                                        void      *data)
{
        g_settings_set_enum (marco_settings, MARCO_DOUBLE_CLICK_TITLEBAR_KEY,
                             gtk_combo_box_get_active (GTK_COMBO_BOX (optionmenu)));
}

static void
alt_click_radio_toggled_callback (GtkWidget *radio,
                                  void      *data)
{
        MouseClickModifier *modifier = data;
        gboolean active;
        gchar *value;

        active = gtk_toggle_button_get_active (GTK_TOGGLE_BUTTON (radio));

        if (active) {
                value = g_strdup_printf ("<%s>", modifier->value);
                g_settings_set_string (marco_settings, MARCO_MOUSE_MODIFIER_KEY, value);
                g_free (value);
        }
}

static void
set_alt_click_value ()
{
	gboolean match_found = FALSE;
        gchar *mouse_move_modifier;
        gchar *value;
	int i;

        mouse_move_modifier = g_settings_get_string (marco_settings, MARCO_MOUSE_MODIFIER_KEY);

	/* We look for a matching modifier and set it. */
	if (mouse_move_modifier != NULL) {
		for (i = 0; i < n_mouse_modifiers; i ++) {
                        value = g_strdup_printf ("<%s>", mouse_modifiers[i].value);
			if (strcmp (value, mouse_move_modifier) == 0) {
				gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (mouse_modifiers[i].radio),
							      TRUE);
				match_found = TRUE;
				break;
			}
                        g_free (value);
                }
                g_free (mouse_move_modifier);
	}

	/* No matching modifier was found; we set all the toggle buttons to be
	 * insensitive. */
	for (i = 0; i < n_mouse_modifiers; i++) {
		gtk_toggle_button_set_inconsistent (GTK_TOGGLE_BUTTON (mouse_modifiers[i].radio),
						    ! match_found);
	}
}

static void
wm_unsupported ()
{
        GtkWidget *no_tool_dialog;

        no_tool_dialog =
                gtk_message_dialog_new (NULL,
                                        GTK_DIALOG_DESTROY_WITH_PARENT,
                                        GTK_MESSAGE_ERROR,
                                        GTK_BUTTONS_CLOSE,
                                        " ");
        gtk_window_set_title (GTK_WINDOW (no_tool_dialog), "");
        gtk_window_set_resizable (GTK_WINDOW (no_tool_dialog), FALSE);

        gtk_message_dialog_set_markup (GTK_MESSAGE_DIALOG (no_tool_dialog), _("The current window manager is unsupported"));

        gtk_dialog_run (GTK_DIALOG (no_tool_dialog));

        gtk_widget_destroy (no_tool_dialog);
}

static void
wm_changed_callback (GdkScreen *screen,
                     void      *data)
{
        const char *current_wm;

        current_wm = gdk_x11_screen_get_window_manager_name (screen);

        gtk_widget_set_sensitive (dialog_win, g_strcmp0 (current_wm, WM_COMMON_MARCO) == 0);
}

static void
response_cb (GtkWidget *dialog_win,
             int        response_id,
             void      *data)
{

        if (response_id == GTK_RESPONSE_HELP) {
		capplet_help (GTK_WINDOW (dialog_win),
			      "goscustdesk-58");
        } else {
                gtk_widget_destroy (dialog_win);
        }
}

int
main (int argc, char **argv)
{
        GdkScreen *screen;
        GtkBuilder *builder;
        GError *error = NULL;
        const char *current_wm;
        int i;

        bindtextdomain (GETTEXT_PACKAGE, MATELOCALEDIR);
	bind_textdomain_codeset(GETTEXT_PACKAGE, "UTF-8");
        textdomain (GETTEXT_PACKAGE);

        gtk_init (&argc, &argv);

        screen = gdk_display_get_default_screen (gdk_display_get_default ());
        current_wm = gdk_x11_screen_get_window_manager_name (screen);

        if (g_strcmp0 (current_wm, WM_COMMON_METACITY) == 0) {
                mate_metacity_config_tool ();
                return 0;
        }

        if (g_strcmp0 (current_wm, WM_COMMON_MARCO) != 0) {
                wm_unsupported ();
                return 1;
        }

        marco_settings = g_settings_new (MARCO_SCHEMA);

        builder = gtk_builder_new ();
        gtk_builder_set_translation_domain (builder, GETTEXT_PACKAGE);

        if (gtk_builder_add_from_file (builder, UIDIR "/mate-window-properties.ui", &error) == 0) {
                g_warning ("Could not parse UI file: %s", error->message);
                g_error_free (error);
                g_object_unref (builder);
                return 1;
        }

        dialog_win = GTK_WIDGET (gtk_builder_get_object (builder,
                                                         "main-dialog"));
        compositing_checkbutton = gtk_builder_get_object (builder,
                                                         "compositing-manager-checkbutton");
        compositing_fast_alt_tab_checkbutton = gtk_builder_get_object (builder,
                                                         "compositing-fast-alt-tab-checkbutton");
        focus_mode_checkbutton = gtk_builder_get_object (builder,
                                                         "focus-mode-checkbutton");
        autoraise_checkbutton = gtk_builder_get_object (builder,
                                                        "autoraise-checkbutton");
        autoraise_delay_slider = gtk_builder_get_object (builder,
                                                         "autoraise-delay-slider");
        autoraise_delay_hbox = GTK_WIDGET (gtk_builder_get_object (builder,
                                                                   "autoraise-delay-hbox"));
        double_click_titlebar_optionmenu = gtk_builder_get_object (builder,
                                                                   "double-click-titlebar-optionmenu");
        alt_click_hbox = gtk_builder_get_object (builder, "alt-click-box");

        gtk_range_set_range (GTK_RANGE (autoraise_delay_slider),
                             0, 10);

        gtk_range_set_increments (GTK_RANGE (autoraise_delay_slider),
                                  0.2, 1.0);


        reload_mouse_modifiers ();

        gtk_combo_box_text_append_text (GTK_COMBO_BOX_TEXT (double_click_titlebar_optionmenu), _("Roll up"));
        gtk_combo_box_text_append_text (GTK_COMBO_BOX_TEXT (double_click_titlebar_optionmenu), _("Maximize"));
        gtk_combo_box_text_append_text (GTK_COMBO_BOX_TEXT (double_click_titlebar_optionmenu), _("Maximize Horizontally"));
        gtk_combo_box_text_append_text (GTK_COMBO_BOX_TEXT (double_click_titlebar_optionmenu), _("Maximize Vertically"));
        gtk_combo_box_text_append_text (GTK_COMBO_BOX_TEXT (double_click_titlebar_optionmenu), _("Minimize"));
        gtk_combo_box_text_append_text (GTK_COMBO_BOX_TEXT (double_click_titlebar_optionmenu), _("None"));
        gtk_combo_box_set_active (GTK_COMBO_BOX (double_click_titlebar_optionmenu),
                                  g_settings_get_enum (marco_settings, MARCO_DOUBLE_CLICK_TITLEBAR_KEY));

        set_alt_click_value ();
        gtk_range_set_value (GTK_RANGE (autoraise_delay_slider),
                             g_settings_get_int (marco_settings, MARCO_AUTORAISE_DELAY_KEY) / 1000.0);
        gtk_combo_box_set_active (GTK_COMBO_BOX (double_click_titlebar_optionmenu),
                                  g_settings_get_enum (marco_settings, MARCO_DOUBLE_CLICK_TITLEBAR_KEY));

        g_signal_connect (G_OBJECT (dialog_win), "response",
                          G_CALLBACK (response_cb), NULL);

        g_signal_connect (G_OBJECT (dialog_win), "destroy",
                          G_CALLBACK (gtk_main_quit), NULL);

        g_signal_connect (marco_settings, "changed",
                          G_CALLBACK (marco_settings_changed_callback), NULL);

        g_settings_bind (marco_settings,
                         MARCO_COMPOSITING_MANAGER_KEY,
                         compositing_checkbutton,
                         "active",
                         G_SETTINGS_BIND_DEFAULT);

        g_settings_bind (marco_settings,
                         MARCO_COMPOSITING_FAST_ALT_TAB_KEY,
                         compositing_fast_alt_tab_checkbutton,
                         "active",
                         G_SETTINGS_BIND_DEFAULT);

        g_signal_connect (focus_mode_checkbutton, "toggled",
                          G_CALLBACK (mouse_focus_toggled_callback), NULL);
        g_signal_connect (marco_settings, "changed::" MARCO_FOCUS_KEY,
                          G_CALLBACK (mouse_focus_changed_callback), NULL);

        g_settings_bind (marco_settings,
                         MARCO_AUTORAISE_KEY,
                         autoraise_checkbutton,
                         "active",
                         G_SETTINGS_BIND_DEFAULT);

        g_signal_connect (autoraise_delay_slider, "value_changed",
                          G_CALLBACK (autoraise_delay_value_changed_callback), NULL);

        g_signal_connect (double_click_titlebar_optionmenu, "changed",
                          G_CALLBACK (double_click_titlebar_changed_callback), NULL);

        g_signal_connect (G_OBJECT (screen), "window_manager_changed",
                          G_CALLBACK (wm_changed_callback), NULL);

        i = 0;
        while (i < n_mouse_modifiers) {
                g_signal_connect (G_OBJECT (mouse_modifiers[i].radio), "toggled",
                                  G_CALLBACK (alt_click_radio_toggled_callback),
                                  &mouse_modifiers[i]);
                ++i;
        }

        /* update sensitivity */
        update_sensitivity ();

        capplet_set_icon (dialog_win, "preferences-system-windows");
        gtk_widget_show (dialog_win);

        gtk_main ();

        g_object_unref (marco_settings);
        g_object_unref (builder);

        return 0;
}

#include <X11/Xlib.h>
#include <X11/keysym.h>
#include <gdk/gdkx.h>

static void
fill_radio (GtkRadioButton     *group,
            MouseClickModifier *modifier)
{
        modifier->radio =
                gtk_radio_button_new_with_mnemonic_from_widget (group,
                                                                modifier->name);
        gtk_box_pack_start (GTK_BOX (alt_click_hbox),
                            modifier->radio, FALSE, FALSE, 0);

        gtk_widget_show (modifier->radio);
}

static void
reload_mouse_modifiers (void)
{
        XModifierKeymap *modmap;
        KeySym *keymap;
        int keysyms_per_keycode;
        int map_size;
        int i;
        gboolean have_meta;
        gboolean have_hyper;
        gboolean have_super;
        int min_keycode, max_keycode;
        int mod_meta, mod_super, mod_hyper;

#if GTK_CHECK_VERSION (3, 0, 0)
        XDisplayKeycodes (gdk_x11_display_get_xdisplay(gdk_display_get_default()),
#else
        XDisplayKeycodes (gdk_display,
#endif
                          &min_keycode,
                          &max_keycode);

#if GTK_CHECK_VERSION (3, 0, 0)
        keymap = XGetKeyboardMapping (gdk_x11_display_get_xdisplay(gdk_display_get_default()),
#else
        keymap = XGetKeyboardMapping (gdk_display,
#endif
                                      min_keycode,
                                      max_keycode - min_keycode,
                                      &keysyms_per_keycode);

#if GTK_CHECK_VERSION (3, 0, 0)
        modmap = XGetModifierMapping (gdk_x11_display_get_xdisplay(gdk_display_get_default()));
#else
        modmap = XGetModifierMapping (gdk_display);
#endif

        have_super = FALSE;
        have_meta = FALSE;
        have_hyper = FALSE;

        /* there are 8 modifiers, and the first 3 are shift, shift lock,
         * and control
         */
        map_size = 8 * modmap->max_keypermod;
        i = 3 * modmap->max_keypermod;
        mod_meta = mod_super = mod_hyper = 0;
        while (i < map_size) {
                /* get the key code at this point in the map,
                 * see if its keysym is one we're interested in
                 */
                int keycode = modmap->modifiermap[i];

                if (keycode >= min_keycode &&
                    keycode <= max_keycode) {
                        int j = 0;
                        KeySym *syms = keymap + (keycode - min_keycode) * keysyms_per_keycode;

                        while (j < keysyms_per_keycode) {
                                if (syms[j] == XK_Super_L ||
                                    syms[j] == XK_Super_R)
                                        mod_super = i / modmap->max_keypermod;
                                else if (syms[j] == XK_Hyper_L ||
                                         syms[j] == XK_Hyper_R)
                                        mod_hyper = i / modmap->max_keypermod;
                                else if ((syms[j] == XK_Meta_L ||
                                          syms[j] == XK_Meta_R))
                                        mod_meta = i / modmap->max_keypermod;
                                ++j;
                        }
                }

                ++i;
        }

        if ((1 << mod_meta) != Mod1Mask)
                have_meta = TRUE;
        if (mod_super != 0 &&
            mod_super != mod_meta)
                have_super = TRUE;
        if (mod_hyper != 0 &&
            mod_hyper != mod_meta &&
            mod_hyper != mod_super)
                have_hyper = TRUE;

        XFreeModifiermap (modmap);
        XFree (keymap);

        i = 0;
        while (i < n_mouse_modifiers) {
                g_free (mouse_modifiers[i].name);
                if (mouse_modifiers[i].radio)
                        gtk_widget_destroy (mouse_modifiers[i].radio);
                ++i;
        }
        g_free (mouse_modifiers);
        mouse_modifiers = NULL;

        n_mouse_modifiers = 1; /* alt */
        if (have_super)
                ++n_mouse_modifiers;
        if (have_hyper)
                ++n_mouse_modifiers;
        if (have_meta)
                ++n_mouse_modifiers;

        mouse_modifiers = g_new0 (MouseClickModifier, n_mouse_modifiers);

        i = 0;

        mouse_modifiers[i].number = i;
        mouse_modifiers[i].name = g_strdup (_("_Alt"));
        mouse_modifiers[i].value = "Alt";
        ++i;

        if (have_hyper) {
                mouse_modifiers[i].number = i;
                mouse_modifiers[i].name = g_strdup (_("H_yper"));
                mouse_modifiers[i].value = "Hyper";
                ++i;
        }

        if (have_super) {
                mouse_modifiers[i].number = i;
                mouse_modifiers[i].name = g_strdup (_("S_uper (or \"Windows logo\")"));
                mouse_modifiers[i].value = "Super";
                ++i;
        }

        if (have_meta) {
                mouse_modifiers[i].number = i;
                mouse_modifiers[i].name = g_strdup (_("_Meta"));
                mouse_modifiers[i].value = "Meta";
                ++i;
        }

        g_assert (i == n_mouse_modifiers);

        i = 0;
        while (i < n_mouse_modifiers) {
                fill_radio (i == 0 ? NULL : GTK_RADIO_BUTTON (mouse_modifiers[i-1].radio),
                            &mouse_modifiers[i]);
                ++i;
        }
}
