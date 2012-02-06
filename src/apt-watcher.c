/*
Copyright 2011 Canonical Ltd.

Authors:
    Conor Curran <conor.curran@canonical.com>

This program is free software: you can redistribute it and/or modify it 
under the terms of the GNU General Public License version 3, as published 
by the Free Software Foundation.

This program is distributed in the hope that it will be useful, but 
WITHOUT ANY WARRANTY; without even the implied warranties of 
MERCHANTABILITY, SATISFACTORY QUALITY, or FITNESS FOR A PARTICULAR 
PURPOSE.  See the GNU General Public License for more details.

You should have received a copy of the GNU General Public License along 
with this program.  If not, see <http://www.gnu.org/licenses/>.
*/

// Conor - 6/2/2012
// Please pull in packagekit's client lib
// using apt-get install --no-install-recommends libpackagekit-glib2-dev
// make sure you don't install package-kit
#define I_KNOW_THE_PACKAGEKIT_GLIB2_API_IS_SUBJECT_TO_CHANGE

#include <glib/gi18n.h>
#include <packagekit-glib2/packagekit.h>
#include "apt-watcher.h"
#include "dbus-shared-names.h"

static guint watcher_id;

struct _AptWatcher
{
	GObject parent_instance;
  SessionDbus* session_dbus_interface;
  DbusmenuMenuitem* apt_item;
  AptState current_state;
  PkClient* pkclient;  
  GCancellable * proxy_cancel;
  GDBusProxy * proxy;    
};
                                                                
static void apt_watcher_show_apt_dialog (DbusmenuMenuitem* mi,
                                         guint timestamp,
                                         gpointer userdata);
//static gboolean apt_watcher_start_apt_interaction (gpointer data);

G_DEFINE_TYPE (AptWatcher, apt_watcher, G_TYPE_OBJECT);

static void apt_watcher_signal_cb ( GDBusProxy* proxy,
                                    gchar* sender_name,
                                    gchar* signal_name,
                                    GVariant* parameters,
                                    gpointer user_data)
{
  g_return_if_fail (APT_IS_WATCHER (user_data));
  //AptWatcher* self = APT_WATCHER (user_data);

  g_variant_ref_sink (parameters);
  GVariant *value = g_variant_get_child_value (parameters, 0);

  if (g_strcmp0(signal_name, "UpdatesChanged") == 0){
    g_debug ("UpdatesChanged signal received");

  }
  else if (g_strcmp0(signal_name, "RestartScheduled") == 0) {
    g_debug ("RestartScheduled signal received");
  } 

  g_variant_unref (value);
  g_variant_unref (parameters);
}

static void
apt_watcher_on_name_appeared (GDBusConnection *connection,
                              const gchar     *name,
                              const gchar     *name_owner,
                              gpointer         user_data)
{
  g_return_if_fail (APT_IS_WATCHER (user_data));
 // AptWatcher* watcher = APT_WATCHER (user_data);
  
  g_print ("Name %s on %s is owned by %s\n",
           name,
           "the system bus",
           name_owner);
}


static void
apt_watcher_on_name_vanished (GDBusConnection *connection,
                              const gchar     *name,
                              gpointer         user_data)
{
  g_debug ("Name %s does not exist or has just vanished",
           name);
  g_return_if_fail (APT_IS_WATCHER (user_data));
}

static void
fetch_proxy_cb (GObject * object, GAsyncResult * res, gpointer user_data)
{
  GError * error = NULL;

  AptWatcher* self = APT_WATCHER(user_data);
  g_return_if_fail(self != NULL);

  GDBusProxy * proxy = g_dbus_proxy_new_for_bus_finish(res, &error);

  if (self->proxy_cancel != NULL) {
    g_object_unref(self->proxy_cancel);
    self->proxy_cancel = NULL;
  }

  if (error != NULL) {
    g_warning("Could not grab DBus proxy for %s: %s",
               "org.debian.apt", error->message);
    g_error_free(error);
    return;
  }

  self->proxy = proxy;
  // Set up the watch.
  watcher_id = g_bus_watch_name (G_BUS_TYPE_SYSTEM,
                                 "org.freedesktop.PackageKit",
                                 G_BUS_NAME_WATCHER_FLAGS_NONE,
                                 apt_watcher_on_name_appeared,
                                 apt_watcher_on_name_vanished,
                                 self,
                                 NULL);
  
  g_signal_connect (self->proxy,
                    "g-signal",
                    G_CALLBACK(apt_watcher_signal_cb),
                    self);   
}


static gboolean 
apt_watcher_start_apt_interaction (gpointer data)
{
  g_return_val_if_fail (APT_IS_WATCHER (data), FALSE);
  AptWatcher* self = APT_WATCHER (data);
  g_dbus_proxy_new_for_bus (G_BUS_TYPE_SYSTEM,
                            G_DBUS_PROXY_FLAGS_NONE,
                            NULL,
                            "org.freedesktop.PackageKit",
                            "/org/freedesktop/PackageKit",
                            "org.freedesktop.PackageKit",
                            self->proxy_cancel,
                            fetch_proxy_cb,
                            self);
  return FALSE;    
}


static void
apt_watcher_show_apt_dialog (DbusmenuMenuitem * mi,
                             guint timestamp,
                             gpointer userdata)
{
  GError * error = NULL;
  g_return_if_fail (APT_IS_WATCHER (userdata));
  AptWatcher* self = APT_WATCHER (userdata);
  const gchar* disposition = NULL;
  disposition = dbusmenu_menuitem_property_get (self->apt_item,
                                                DBUSMENU_MENUITEM_PROP_DISPOSITION);
                                    
  if (g_strcmp0 (disposition, DBUSMENU_MENUITEM_DISPOSITION_ALERT) == 0){  	
    gchar * helper = g_build_filename (LIBEXECDIR, "gtk-logout-helper", NULL);
	  gchar * dialog_line = g_strdup_printf ("%s --%s", helper, "restart");
  	g_free(helper);
	  if (!g_spawn_command_line_async(dialog_line, &error)) {
		  g_warning("Unable to show dialog: %s", error->message);
		  g_error_free(error);
	  }
	  g_free(dialog_line);
  } 
  else{
    if (!g_spawn_command_line_async("update-manager", &error))
    {
      g_warning("Unable to show update-manager: %s", error->message);
      g_error_free(error);
    }
  }   
}

static void
apt_watcher_init (AptWatcher *self)
{
  self->current_state = UP_TO_DATE;
  self->pkclient = pk_client_new ();
  g_timeout_add_seconds (60,
                         apt_watcher_start_apt_interaction,
                         self); 
}

static void
apt_watcher_finalize (GObject *object)
{
  g_bus_unwatch_name (watcher_id);  
  AptWatcher* self = APT_WATCHER (object);
           
  if (self->proxy != NULL)
    g_object_unref (self->proxy);
         
  G_OBJECT_CLASS (apt_watcher_parent_class)->finalize (object);
}

static void
apt_watcher_class_init (AptWatcherClass *klass)
{
  GObjectClass* object_class = G_OBJECT_CLASS (klass);
  object_class->finalize = apt_watcher_finalize;
}

AptWatcher* apt_watcher_new (SessionDbus* session_dbus,
                             DbusmenuMenuitem* item)
{
  AptWatcher* watcher = g_object_new (APT_TYPE_WATCHER, NULL);
  watcher->session_dbus_interface = session_dbus;
  watcher->apt_item = item;
  g_signal_connect (G_OBJECT(watcher->apt_item),
                    DBUSMENU_MENUITEM_SIGNAL_ITEM_ACTIVATED,
                    G_CALLBACK(apt_watcher_show_apt_dialog), watcher);
  return watcher;
}
                               
