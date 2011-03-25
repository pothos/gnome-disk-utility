/* -*- mode: C; c-file-style: "gnu"; indent-tabs-mode: nil; -*-
 *
 * Copyright (C) 2008-2011 Red Hat, Inc.
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General
 * Public License along with this library; if not, write to the
 * Free Software Foundation, Inc., 59 Temple Place, Suite 330,
 * Boston, MA 02111-1307, USA.
 *
 * Author: David Zeuthen <davidz@redhat.com>
 */

#include "config.h"
#include <glib/gi18n.h>

#include "gduiscsipathmodel.h"
#include "gduutils.h"

struct _GduIScsiPathModel
{
  GtkListStore parent_instance;

  UDisksClient *client;

  UDisksIScsiTarget *iscsi_target;
  GDBusObject *object;
};

typedef struct
{
  GtkListStoreClass parent_class;
} GduIScsiPathModelClass;

enum
{
  PROP_0,
  PROP_CLIENT,
  PROP_OBJECT
};

static void update_all (GduIScsiPathModel *model);
static void on_notify_for_iscsi_target (GObject    *object,
                                        GParamSpec *pspec,
                                        gpointer    user_data);

G_DEFINE_TYPE (GduIScsiPathModel, gdu_iscsi_path_model, GTK_TYPE_LIST_STORE);

static void
gdu_iscsi_path_model_finalize (GObject *object)
{
  GduIScsiPathModel *model = GDU_ISCSI_PATH_MODEL (object);

  g_signal_handlers_disconnect_by_func (model->iscsi_target,
                                        G_CALLBACK (on_notify_for_iscsi_target),
                                        model);
  g_object_unref (model->iscsi_target);
  g_object_unref (model->client);

  G_OBJECT_CLASS (gdu_iscsi_path_model_parent_class)->finalize (object);
}

static void
gdu_iscsi_path_model_init (GduIScsiPathModel *model)
{
}

static void
gdu_iscsi_path_model_get_property (GObject    *object,
                                    guint       prop_id,
                                    GValue     *value,
                                    GParamSpec *pspec)
{
  GduIScsiPathModel *model = GDU_ISCSI_PATH_MODEL (object);

  switch (prop_id)
    {
    case PROP_CLIENT:
      g_value_set_object (value, gdu_iscsi_path_model_get_client (model));
      break;

    case PROP_OBJECT:
      g_value_set_object (value, gdu_iscsi_path_model_get_object (model));
      break;

    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
    }
}

static void
gdu_iscsi_path_model_set_property (GObject      *object,
                                    guint         prop_id,
                                    const GValue *value,
                                    GParamSpec   *pspec)
{
  GduIScsiPathModel *model = GDU_ISCSI_PATH_MODEL (object);

  switch (prop_id)
    {
    case PROP_CLIENT:
      model->client = g_value_dup_object (value);
      break;

    case PROP_OBJECT:
      model->object = g_value_dup_object (value);
      break;

    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
    }
}

/* ---------------------------------------------------------------------------------------------------- */

static void
gdu_iscsi_path_model_constructed (GObject *object)
{
  GduIScsiPathModel *model = GDU_ISCSI_PATH_MODEL (object);
  GType types[GDU_ISCSI_PATH_MODEL_N_COLUMNS];
  /* GDBusObjectManager *object_manager; */

  model->iscsi_target = UDISKS_GET_ISCSI_TARGET (model->object);
  g_assert (model->iscsi_target != NULL);

  types[0] = G_TYPE_BOOLEAN;
  types[1] = G_TYPE_STRING;
  types[2] = G_TYPE_INT;
  types[3] = G_TYPE_INT;
  types[4] = G_TYPE_STRING;
  types[5] = G_TYPE_STRING;
  G_STATIC_ASSERT (6 == GDU_ISCSI_PATH_MODEL_N_COLUMNS);
  gtk_list_store_set_column_types (GTK_LIST_STORE (model),
                                   GDU_ISCSI_PATH_MODEL_N_COLUMNS,
                                   types);

  update_all (model);
  g_signal_connect (model->iscsi_target,
                    "notify",
                    G_CALLBACK (on_notify_for_iscsi_target),
                    model);

  if (G_OBJECT_CLASS (gdu_iscsi_path_model_parent_class)->constructed != NULL)
    G_OBJECT_CLASS (gdu_iscsi_path_model_parent_class)->constructed (object);
}

static void
gdu_iscsi_path_model_class_init (GduIScsiPathModelClass *klass)
{
  GObjectClass *gobject_class;

  gobject_class = G_OBJECT_CLASS (klass);
  gobject_class->finalize     = gdu_iscsi_path_model_finalize;
  gobject_class->constructed  = gdu_iscsi_path_model_constructed;
  gobject_class->get_property = gdu_iscsi_path_model_get_property;
  gobject_class->set_property = gdu_iscsi_path_model_set_property;

  /**
   * GduIScsiPathModel:client:
   *
   * The #UDisksClient used by the #GduIScsiPathModel instance.
   */
  g_object_class_install_property (gobject_class,
                                   PROP_CLIENT,
                                   g_param_spec_object ("client",
                                                        "Client",
                                                        "The client used by the tree model",
                                                        UDISKS_TYPE_CLIENT,
                                                        G_PARAM_READABLE |
                                                        G_PARAM_WRITABLE |
                                                        G_PARAM_CONSTRUCT_ONLY |
                                                        G_PARAM_STATIC_STRINGS));

  /**
   * GduIScsiPathModel:object:
   *
   * The #GDBusObject that is a iSCSI target.
   */
  g_object_class_install_property (gobject_class,
                                   PROP_OBJECT,
                                   g_param_spec_object ("object",
                                                        "Object",
                                                        "The iSCSI target",
                                                        G_TYPE_DBUS_OBJECT,
                                                        G_PARAM_READABLE |
                                                        G_PARAM_WRITABLE |
                                                        G_PARAM_CONSTRUCT_ONLY |
                                                        G_PARAM_STATIC_STRINGS));
}

/**
 * gdu_iscsi_path_model_new:
 * @client: A #UDisksClient.
 * @object: A #GDBusObject.
 *
 * Creates a new #GduIScsiPathModel for viewing the paths on the iSCSI
 * target on @object.
 *
 * Returns: A #GduIScsiPathModel. Free with g_object_unref().
 */
GduIScsiPathModel *
gdu_iscsi_path_model_new (UDisksClient  *client,
                          GDBusObject   *object)
{
  return GDU_ISCSI_PATH_MODEL (g_object_new (GDU_TYPE_ISCSI_PATH_MODEL,
                                             "client", client,
                                             "object", object,
                                             NULL));
}

/**
 * gdu_iscsi_path_model_get_client:
 * @model: A #GduIScsiPathModel.
 *
 * Gets the #UDisksClient used by @model.
 *
 * Returns: (transfer none): A #UDisksClient. Do not free, the object
 * belongs to @model.
 */
UDisksClient *
gdu_iscsi_path_model_get_client (GduIScsiPathModel *model)
{
  g_return_val_if_fail (GDU_IS_ISCSI_PATH_MODEL (model), NULL);
  return model->client;
}

/**
 * gdu_iscsi_path_model_get_object:
 * @model: A #GduIScsiPathModel.
 *
 * Gets the #GDBusObject used by @model.
 *
 * Returns: (transfer none): A #GDBusObject. Do not free, the object
 * belongs to @model.
 */
GDBusObject *
gdu_iscsi_path_model_get_object (GduIScsiPathModel *model)
{
  g_return_val_if_fail (GDU_IS_ISCSI_PATH_MODEL (model), NULL);
  return model->object;
}

/* ---------------------------------------------------------------------------------------------------- */

static void
update_all (GduIScsiPathModel *model)
{
  GVariant *portals_and_interfaces;
  GVariantIter portal_iter;
  const gchar *portal_name;
  gint port;
  gint group;
  GVariantIter *iface_iter;

  gtk_list_store_clear (GTK_LIST_STORE (model));

  portals_and_interfaces = udisks_iscsi_target_get_portals_and_interfaces (model->iscsi_target);
  g_variant_iter_init (&portal_iter, portals_and_interfaces);

  while (g_variant_iter_next (&portal_iter,
                              "(^&ayiia(ays))",
                              &portal_name,
                              &port,
                              &group,
                              &iface_iter))
    {
      const gchar *iface_name;
      const gchar *state;

      while (g_variant_iter_next (iface_iter,
                                  "(^&ays)",
                                  &iface_name,
                                  &state))
        {
          gchar *status;
          gboolean active;
          if (g_strcmp0 (state, "LOGGED_IN") == 0)
            {
              active = TRUE;
              status = g_strdup (_("Logged In"));
            }
          else if (g_strcmp0 (state, "FAILED") == 0)
            {
              active = TRUE;
              status = g_strdup_printf ("<span foreground=\"#ff0000\"><b>%s</b></span>", _("FAILED"));
            }
          else if (g_strcmp0 (state, "") == 0)
            {
              active = FALSE;
              status = g_strdup (_("Not Logged In"));
            }
          else
            {
              active = TRUE;
              status = g_strdup (state);
            }

          gtk_list_store_insert_with_values (GTK_LIST_STORE (model),
                                             NULL, /* GtkTreeIter *out */
                                             G_MAXINT, /* position */
                                             GDU_ISCSI_PATH_MODEL_COLUMN_ACTIVE, active,
                                             GDU_ISCSI_PATH_MODEL_COLUMN_PORTAL_ADDRESS, portal_name,
                                             GDU_ISCSI_PATH_MODEL_COLUMN_PORTAL_PORT, port,
                                             GDU_ISCSI_PATH_MODEL_COLUMN_TPGT, group,
                                             GDU_ISCSI_PATH_MODEL_COLUMN_INTERFACE, iface_name,
                                             GDU_ISCSI_PATH_MODEL_COLUMN_STATUS, status,
                                             -1);
          g_free (status);
        }
      g_variant_iter_free (iface_iter);
    }
}

static void
on_notify_for_iscsi_target (GObject    *object,
                            GParamSpec *pspec,
                            gpointer    user_data)
{
  GduIScsiPathModel *model = GDU_ISCSI_PATH_MODEL (user_data);
  update_all (model);
}
