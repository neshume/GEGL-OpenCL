/* This file is part of GEGL
 *
 * GEGL is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 3 of the License, or (at your option) any later version.
 *
 * GEGL is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with GEGL; if not, see <http://www.gnu.org/licenses/>.
 *
 * Copyright 2003 Calvin Williamson
 *           2006 Øyvind Kolås
 */

#define GEGL_INTERNAL

#include "config.h"

#include <string.h>

#include <glib-object.h>
#include <gobject/gvaluecollector.h>

#include "gegl-types.h"

#include "gegl-node.h"
#include "gegl-connection.h"
#include "gegl-pad.h"
#include "gegl-utils.h"
#include "gegl-visitable.h"

#include "operation/gegl-operation.h"
#include "operation/gegl-operations.h"
#include "operation/gegl-operation-meta.h"

#include "process/gegl-eval-mgr.h"
#include "process/gegl-have-visitor.h"
#include "process/gegl-prepare-visitor.h"
#include "process/gegl-finish-visitor.h"
#include "process/gegl-processor.h"

enum
{
  PROP_0,
  PROP_OP_CLASS,
  PROP_OPERATION,
  PROP_NAME
};

enum
{
  INVALIDATED,
  COMPUTED,
  LAST_SIGNAL
};


struct _GeglNodePrivate
{
  GSList         *children;  /*  used for children */
  GeglNode       *parent;
  gchar          *name;
  GeglProcessor  *processor;
};


#define GEGL_NODE_GET_PRIVATE(obj) \
  ((GeglNodePrivate *)(((GeglNode *) obj)->priv))


static guint gegl_node_signals[LAST_SIGNAL] = {0};


static void            gegl_node_class_init           (GeglNodeClass *klass);
static void            gegl_node_init                 (GeglNode      *self);
static void            finalize                       (GObject       *self_object);
static void            dispose                        (GObject       *self_object);
static void            set_property                   (GObject       *gobject,
                                                       guint          prop_id,
                                                       const GValue  *value,
                                                       GParamSpec    *pspec);
static void            get_property                   (GObject       *gobject,
                                                       guint          prop_id,
                                                       GValue        *value,
                                                       GParamSpec    *pspec);
static gboolean        pads_exist                     (GeglNode      *sink,
                                                       const gchar   *sink_pad_name,
                                                       GeglNode      *source,
                                                       const gchar   *source_pad_name);
static GeglConnection *find_connection                (GeglNode      *sink,
                                                       GeglPad       *sink_pad);
static void            visitable_init                 (gpointer       ginterface,
                                                       gpointer       interface_data);
static void            visitable_accept               (GeglVisitable *visitable,
                                                       GeglVisitor   *visitor);
static GSList*         visitable_depends_on           (GeglVisitable *visitable);
static gboolean        visitable_needs_visiting       (GeglVisitable *visitable);
static void            gegl_node_set_operation_object (GeglNode      *self,
                                                       GeglOperation *operation);
static void            gegl_node_set_op_class         (GeglNode      *self,
                                                       const gchar   *op_class,
                                                       const gchar   *first_property,
                                                       va_list        var_args);
static void            gegl_node_disconnect_sinks     (GeglNode      *self);
static void            gegl_node_disconnect_sources   (GeglNode      *self);
static void            property_changed               (GObject    *gobject,
                                                       GParamSpec *arg1,
                                                       gpointer    user_data);

G_DEFINE_TYPE_WITH_CODE (GeglNode, gegl_node, G_TYPE_OBJECT,
                         G_IMPLEMENT_INTERFACE (GEGL_TYPE_VISITABLE,
                                                visitable_init))


static void
gegl_node_class_init (GeglNodeClass *klass)
{
  GObjectClass *gobject_class = G_OBJECT_CLASS (klass);

  g_type_class_add_private (klass, sizeof (GeglNodePrivate));

  gobject_class->finalize     = finalize;
  gobject_class->dispose      = dispose;
  gobject_class->set_property = set_property;
  gobject_class->get_property = get_property;

  g_object_class_install_property (gobject_class, PROP_OPERATION,
                                   g_param_spec_object ("gegl-operation",
                                                        "Operation Object",
                                                        "The associated GeglOperation instance",
                                                        GEGL_TYPE_OPERATION,
                                                        G_PARAM_READWRITE |
                                                        G_PARAM_CONSTRUCT));

  g_object_class_install_property (gobject_class, PROP_OP_CLASS,
                                   g_param_spec_string ("operation",
                                                        "Operation Type",
                                                        "The type of associated GeglOperation",
                                                        "",
                                                        G_PARAM_CONSTRUCT |
                                                        G_PARAM_READWRITE));

  g_object_class_install_property (gobject_class, PROP_NAME,
                                   g_param_spec_string ("name",
                                                        "Name",
                                                        "The name of the node",
                                                        "",
                                                        G_PARAM_CONSTRUCT |
                                                        G_PARAM_READWRITE));

  gegl_node_signals[INVALIDATED] =
    g_signal_new ("invalidated",
                  G_TYPE_FROM_CLASS (klass),
                  G_SIGNAL_RUN_LAST | G_SIGNAL_NO_RECURSE | G_SIGNAL_NO_HOOKS,
                  0,
                  NULL, NULL,
                  g_cclosure_marshal_VOID__BOXED,
                  G_TYPE_NONE, 1,
                  GEGL_TYPE_RECTANGLE);

  gegl_node_signals[COMPUTED] =
    g_signal_new ("computed",
                  G_TYPE_FROM_CLASS (klass),
                  G_SIGNAL_RUN_LAST | G_SIGNAL_NO_RECURSE | G_SIGNAL_NO_HOOKS,
                  0,
                  NULL, NULL,
                  g_cclosure_marshal_VOID__BOXED,
                  G_TYPE_NONE, 1,
                  GEGL_TYPE_RECTANGLE);
}

static void
gegl_node_init (GeglNode *self)
{
  GeglNodePrivate *priv;

  self->priv = G_TYPE_INSTANCE_GET_PRIVATE (self,
                                            GEGL_TYPE_NODE,
                                            GeglNodePrivate);

  priv = GEGL_NODE_GET_PRIVATE (self);

  self->pads        = NULL;
  self->input_pads  = NULL;
  self->output_pads = NULL;
  self->sinks       = NULL;
  self->sources     = NULL;
  self->operation   = NULL;
  self->enabled     = TRUE;
  self->is_graph    = FALSE;
  self->cache       = NULL;

  priv->parent      = NULL;
  priv->children    = NULL;
  priv->name        = NULL;
  priv->processor   = NULL;
}

static void
visitable_init (gpointer ginterface,
                gpointer interface_data)
{
  GeglVisitableClass *visitable_class = ginterface;

  visitable_class->accept         = visitable_accept;
  visitable_class->depends_on     = visitable_depends_on;
  visitable_class->needs_visiting = visitable_needs_visiting;
}

static void
dispose (GObject *gobject)
{
  GeglNode        *self = GEGL_NODE (gobject);
  GeglNodePrivate *priv = GEGL_NODE_GET_PRIVATE (self);

  if (priv->parent != NULL)
    {
      GeglNode *parent = priv->parent;
      priv->parent = NULL;
      gegl_node_remove_child (parent, self);
    }

  gegl_node_remove_children (self);
  if (self->cache)
    {
      g_object_unref (self->cache);
      self->cache = NULL;
    }

  if (priv->processor)
    {
      gegl_processor_destroy (priv->processor);
      priv->processor = NULL;
    }
  G_OBJECT_CLASS (gegl_node_parent_class)->dispose (gobject);
}

static void
finalize (GObject *gobject)
{
  GeglNode        *self = GEGL_NODE (gobject);
  GeglNodePrivate *priv = GEGL_NODE_GET_PRIVATE (self);

  gegl_node_disconnect_sources (self);
  gegl_node_disconnect_sinks (self);

  if (self->pads)
    {
      g_slist_foreach (self->pads, (GFunc) g_object_unref, NULL);
      g_slist_free (self->pads);
      self->pads = NULL;
    }

  g_slist_free (self->input_pads);
  g_slist_free (self->output_pads);

  if (self->operation)
    {
      g_object_unref (self->operation);
      self->operation = NULL;
    }

  if (priv->name)
    {
      g_free (priv->name);
    }

  G_OBJECT_CLASS (gegl_node_parent_class)->finalize (gobject);
}

static void
set_property (GObject      *gobject,
              guint         property_id,
              const GValue *value,
              GParamSpec   *pspec)
{
  GeglNode *node = GEGL_NODE (gobject);

  switch (property_id)
    {
      case PROP_NAME:
        gegl_node_set_name (node, g_value_get_string (value));
        break;

      case PROP_OP_CLASS:
        gegl_node_set_op_class (node, g_value_get_string (value), NULL, NULL);
        break;

      case PROP_OPERATION:
        gegl_node_set_operation_object (node, g_value_get_object (value));
        break;

      default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID (gobject, property_id, pspec);
        break;
    }
}

static void
get_property (GObject    *gobject,
              guint       property_id,
              GValue     *value,
              GParamSpec *pspec)
{
  GeglNode *node = GEGL_NODE (gobject);

  switch (property_id)
    {
      case PROP_OP_CLASS:
        if (node->operation)
          g_value_set_string (value, GEGL_OPERATION_GET_CLASS (node->operation)->name);
        break;

      case PROP_NAME:
        g_value_set_string (value, gegl_node_get_name (node));
        break;

      case PROP_OPERATION:
        g_value_set_object (value, node->operation);
        break;

      default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID (gobject, property_id, pspec);
        break;
    }
}

/**
 * gegl_node_get_pad:
 * @self: a #GeglNode.
 * @name: property name.
 *
 * Get a property.
 *
 * Returns: A #GeglPad.
 **/
GeglPad *
gegl_node_get_pad (GeglNode    *self,
                   const gchar *name)
{
  GSList *list;

  g_return_val_if_fail (GEGL_IS_NODE (self), NULL);
  g_return_val_if_fail (name != NULL, NULL);

  if (!self->pads)
    return NULL;

  for (list = self->pads; list; list = g_slist_next (list))
    {
      GeglPad *property = list->data;

      if (!strcmp (name, gegl_pad_get_name (property)))
        return property;
    }

  return NULL;
}

/**
 * gegl_node_get_pads:
 * @self: a #GeglNode.
 *
 * Returns: A list of #GeglPad.
 **/
GSList *
gegl_node_get_pads (GeglNode *self)
{
  g_return_val_if_fail (GEGL_IS_NODE (self), NULL);

  return self->pads;
}

/**
 * gegl_node_get_input_pads:
 * @self: a #GeglNode.
 *
 * Returns: A list of #GeglPad.
 **/
GSList *
gegl_node_get_input_pads (GeglNode *self)
{
  g_return_val_if_fail (GEGL_IS_NODE (self), NULL);

  return self->input_pads;
}

void
gegl_node_add_pad (GeglNode *self,
                   GeglPad  *pad)
{
  g_return_if_fail (GEGL_IS_NODE (self));
  g_return_if_fail (GEGL_IS_PAD (pad));

  if (gegl_node_get_pad (self, gegl_pad_get_name (pad)))
    {
      return;
    }
  if (0) g_assert (!gegl_node_get_pad (self, gegl_pad_get_name (pad)));
  self->pads = g_slist_prepend (self->pads, pad);

  if (gegl_pad_is_output (pad))
    self->output_pads = g_slist_prepend (self->output_pads, pad);

  if (gegl_pad_is_input (pad))
    self->input_pads = g_slist_prepend (self->input_pads, pad);
}

void
gegl_node_remove_pad (GeglNode *self,
                      GeglPad  *pad)
{
  g_return_if_fail (GEGL_IS_NODE (self));
  g_return_if_fail (GEGL_IS_PAD (pad));

  self->pads = g_slist_remove (self->pads, pad);

  if (gegl_pad_is_output (pad))
    self->output_pads = g_slist_remove (self->output_pads, pad);

  if (gegl_pad_is_input (pad))
    self->input_pads = g_slist_remove (self->input_pads, pad);

  g_object_unref (pad);
}

static gboolean
pads_exist (GeglNode    *sink,
            const gchar *sink_pad_name,
            GeglNode    *source,
            const gchar *source_pad_name)
{
  GeglPad *sink_pad;

  GeglPad *source_pad;

  if (sink)
    {
      g_assert (sink_pad_name);
      sink_pad = gegl_node_get_pad (sink, sink_pad_name);
      if (!sink_pad || !gegl_pad_is_input (sink_pad))
        {
          g_warning ("%s: Can't find sink property %s of %s", G_STRFUNC,
                     sink_pad_name, gegl_node_get_debug_name (sink));
          return FALSE;
        }
    }

  if (source)
    {
      g_assert (source_pad_name);
      source_pad = gegl_node_get_pad (source, source_pad_name);
      if (!source_pad || !gegl_pad_is_output (source_pad))
        {
          g_warning ("%s: Can't find source property %s of %s", G_STRFUNC,
                     source_pad_name, gegl_node_get_debug_name (source));
          return FALSE;
        }
    }

  return TRUE;
}

static GeglConnection *
find_connection (GeglNode *sink,
                 GeglPad  *sink_pad)
{
  GSList *list;

  g_return_val_if_fail (GEGL_IS_NODE (sink), NULL);

  for (list = sink->sources; list; list = g_slist_next (list))
    {
      GeglConnection *connection = list->data;

      if (sink_pad == gegl_connection_get_sink_pad (connection))
        return connection;
    }

  return NULL;
}

gboolean
gegl_node_connect_to (GeglNode    *source,
                      const gchar *source_pad_name,
                      GeglNode    *sink,
                      const gchar *sink_pad_name)
{
  g_return_val_if_fail (GEGL_IS_NODE (source), FALSE);
  g_return_val_if_fail (source_pad_name != NULL, FALSE);
  g_return_val_if_fail (GEGL_IS_NODE (sink), FALSE);
  g_return_val_if_fail (sink_pad_name != NULL, FALSE);

  return gegl_node_connect_from (sink, sink_pad_name, source, source_pad_name);
}

void
gegl_node_invalidated (GeglNode            *node,
                       const GeglRectangle *rect)
{
  g_return_if_fail (GEGL_IS_NODE (node));
  g_return_if_fail (rect != NULL);

  if (node->cache)
    {
      gegl_cache_invalidate (node->cache, rect);
    }

  g_signal_emit (node, gegl_node_signals[INVALIDATED], 0,
                 rect, NULL);
}

static void
source_invalidated (GeglNode            *source,
                    const GeglRectangle *rect,
                    gpointer             data)
{
  GeglPad       *destination_pad = GEGL_PAD (data);
  GeglNode      *destination     = gegl_pad_get_node (destination_pad);
  GeglRectangle  dirty_rect;

  if (0) g_warning ("%s.%s is dirtied from %s (%i,%i %ix%i)",
                    gegl_node_get_debug_name (destination),
                    gegl_pad_get_name (destination_pad),
                    gegl_node_get_debug_name (source),
                    rect->x, rect->y,
                    rect->width, rect->height);

  if (destination->operation)
    {
      dirty_rect =
        gegl_operation_get_invalidated_by_change (destination->operation,
                                                  gegl_pad_get_name (destination_pad),
                                                  rect);
    }
  else
    {
      dirty_rect = *rect;
    }

  gegl_node_invalidated (destination, &dirty_rect);
}

gboolean
gegl_node_connect_from (GeglNode    *sink,
                        const gchar *sink_pad_name,
                        GeglNode    *source,
                        const gchar *source_pad_name)
{
  g_return_val_if_fail (GEGL_IS_NODE (sink), FALSE);
  g_return_val_if_fail (sink_pad_name != NULL, FALSE);
  g_return_val_if_fail (GEGL_IS_NODE (source), FALSE);
  g_return_val_if_fail (source_pad_name != NULL, FALSE);

  {
    GeglPad *pad;
    GeglPad *other_pad = NULL;

    pad = gegl_node_get_pad (sink, sink_pad_name);
    if (pad)
      other_pad = gegl_pad_get_connected_to (pad);
    else
      {
        g_warning ("%s: Didn't find pad '%s' of '%s'",
                   G_STRFUNC, sink_pad_name, gegl_node_get_debug_name (sink));
      }

    if (other_pad)
      {
        gegl_node_disconnect (sink, sink_pad_name);
      }
  }
  if (pads_exist (sink, sink_pad_name, source, source_pad_name))
    {
      GeglPad        *sink_pad   = gegl_node_get_pad (sink, sink_pad_name);
      GeglPad        *source_pad = gegl_node_get_pad (source, source_pad_name);
      GeglConnection *connection = gegl_pad_connect (sink_pad,
                                                     source_pad);

      gegl_connection_set_sink_node (connection, sink);
      gegl_connection_set_source_node (connection, source);

      sink->sources = g_slist_prepend (sink->sources, connection);
      source->sinks = g_slist_prepend (source->sinks, connection);

      g_signal_connect (G_OBJECT (source), "invalidated",
                        G_CALLBACK (source_invalidated), sink_pad);

      property_changed (G_OBJECT (source->operation), NULL, source);

      return TRUE;
    }

  return FALSE;
}

gboolean
gegl_node_disconnect (GeglNode    *sink,
                      const gchar *sink_pad_name)
{
  g_return_val_if_fail (GEGL_IS_NODE (sink), FALSE);
  g_return_val_if_fail (sink_pad_name != NULL, FALSE);

  if (pads_exist (sink, sink_pad_name, NULL, NULL))
    {
      GeglPad        *sink_pad   = gegl_node_get_pad (sink, sink_pad_name);
      GeglConnection *connection = find_connection (sink, sink_pad);
      GeglNode       *source;
      GeglPad        *source_pad;

      if (!connection)
        return FALSE;

      source_pad = gegl_connection_get_source_pad (connection);
      source     = gegl_connection_get_source_node (connection);

      {
        /* disconnecting dirt propagation */
        gulong handler;

        handler = g_signal_handler_find (source, G_SIGNAL_MATCH_DATA,
                                         gegl_node_signals[INVALIDATED],
                                         0, NULL, NULL, sink_pad);
        if (handler)
          {
            g_signal_handler_disconnect (source, handler);
          }
      }

      gegl_pad_disconnect (sink_pad, source_pad, connection);

      sink->sources = g_slist_remove (sink->sources, connection);
      source->sinks = g_slist_remove (source->sinks, connection);

      gegl_connection_destroy (connection);

      return TRUE;
    }

  return FALSE;
}

static void
gegl_node_disconnect_sources (GeglNode *self)
{
  while (TRUE)
    {
      GeglConnection *connection = g_slist_nth_data (self->sources, 0);

      if (connection)
        {
          GeglNode    *sink          = gegl_connection_get_sink_node (connection);
          GeglPad     *sink_pad      = gegl_connection_get_sink_pad (connection);
          const gchar *sink_pad_name = gegl_pad_get_name (sink_pad);

          g_assert (self == sink);

          gegl_node_disconnect (sink, sink_pad_name);
        }
      else
        break;
    }
}

static void
gegl_node_disconnect_sinks (GeglNode *self)
{
  while (TRUE)
    {
      GeglConnection *connection = g_slist_nth_data (self->sinks, 0);

      if (connection)
        {
          GeglNode    *sink          = gegl_connection_get_sink_node (connection);
          GeglNode    *source        = gegl_connection_get_source_node (connection);
          GeglPad     *sink_pad      = gegl_connection_get_sink_pad (connection);
          const gchar *sink_pad_name = gegl_pad_get_name (sink_pad);

          g_assert (self == source);

          gegl_node_disconnect (sink, sink_pad_name);
        }
      else
        break;
    }
}

/**
 * gegl_node_num_sinks:
 * @self: a #GeglNode.
 *
 * Gets the number of sinks
 *
 * Returns: number of sinks
 **/
gint
gegl_node_get_num_sinks (GeglNode *self)
{
  g_return_val_if_fail (GEGL_IS_NODE (self), -1);

  return g_slist_length (self->sinks);
}

/**
 * gegl_node_get_sinks:
 * @self: a #GeglNode.
 *
 * Gets list of sink connections attached to this self.
 *
 * Returns: list of sink connections.
 **/
GSList *
gegl_node_get_sinks (GeglNode *self)
{
  g_return_val_if_fail (GEGL_IS_NODE (self), FALSE);

  return self->sinks;
}

void
gegl_node_link (GeglNode *source,
                GeglNode *sink)
{
  g_return_if_fail (GEGL_IS_NODE (source));
  g_return_if_fail (GEGL_IS_NODE (sink));

  /* using connect_to is more natural here, but leads to an extra
   * function call, perhaps connect_to and connect_from should be swapped?
   */
  gegl_node_connect_to (source, "output", sink, "input");
}

void
gegl_node_link_many (GeglNode *source,
                     GeglNode *dest,
                     ...)
{
  va_list var_args;

  g_return_if_fail (GEGL_IS_NODE (source));
  g_return_if_fail (GEGL_IS_NODE (dest));

  va_start (var_args, dest);
  while (dest)
    {
      gegl_node_link (source, dest);
      source = dest;
      dest   = va_arg (var_args, GeglNode *);
    }
  va_end (var_args);
}

static GeglBuffer *
gegl_node_apply_roi (GeglNode            *self,
                     const gchar         *output_pad_name,
                     const GeglRectangle *roi)
{
  GeglEvalMgr *eval_mgr;
  GeglBuffer  *buffer;

  eval_mgr      = g_object_new (GEGL_TYPE_EVAL_MGR, NULL);
  eval_mgr->roi = *roi;
  buffer        = gegl_eval_mgr_apply (eval_mgr, self, output_pad_name);

  g_object_unref (eval_mgr);

  return buffer;
}

void
gegl_node_blit (GeglNode            *node,
                gdouble              scale,
                const GeglRectangle *roi,
                const Babl          *format,
                gpointer             destination_buf,
                gint                 rowstride,
                GeglBlitFlags        flags)
{
  g_return_if_fail (GEGL_IS_NODE (node));
  g_return_if_fail (roi != NULL);

  if (flags == GEGL_BLIT_DEFAULT)
    {
      GeglBuffer *buffer;
      buffer = gegl_node_apply_roi (node, "output", roi);
      if (buffer && destination_buf)
        {
          if (destination_buf)
            {
              gegl_buffer_get (buffer, 1.0, roi, format, destination_buf, rowstride);
            }

          if (scale != 1.0)
            {
              g_warning ("Scale %f!=1.0 in blit without cache NYI", scale);
            }
        }

      /* and unrefing to ultimately clean it off from the graph */
      if (buffer)
        g_object_unref (buffer);
    }
  else if ((flags & GEGL_BLIT_CACHE) ||
           (flags & GEGL_BLIT_DIRTY))
    {
      GeglCache *cache = gegl_node_get_cache (node);
      if (!(flags & GEGL_BLIT_DIRTY))
        { /* if we're not blitting dirtily, we need to make sure
             that the data is available */
          GeglProcessor *processor = gegl_node_new_processor (node, roi);
          while (gegl_processor_work (processor, NULL)) ;
          g_object_unref (G_OBJECT (processor));
        }
      if (destination_buf)
        {
          gegl_buffer_get (GEGL_BUFFER (cache), scale, roi,
                           format, destination_buf, rowstride);
        }
    }
}

GeglBuffer *
gegl_node_apply (GeglNode    *self,
                 const gchar *output_prop_name)
{
  GeglRectangle defined;

  g_return_val_if_fail (GEGL_IS_NODE (self), NULL);

  defined = gegl_node_get_bounding_box (self);
  return gegl_node_apply_roi (self, "output", &defined);
}

GSList *
gegl_node_get_depends_on (GeglNode *self)
{
  GSList *depends_on = NULL;
  GSList *llink;

  g_return_val_if_fail (GEGL_IS_NODE (self), NULL);

  for (llink = self->sources; llink; llink = g_slist_next (llink))
    {
      GeglConnection *connection = llink->data;
      GeglNode       *source_node;

      source_node = gegl_connection_get_source_node (connection);

      if (source_node->is_graph)
        {
          GeglNode *proxy = gegl_node_get_output_proxy (source_node, "output");

          if (!g_slist_find (depends_on, proxy))
            depends_on = g_slist_prepend (depends_on, proxy);
        }
      else if (!g_slist_find (depends_on, source_node))
        {
          depends_on = g_slist_prepend (depends_on, source_node);
        }
    }

  if (gegl_node_get_name (self) &&
      !strcmp (gegl_node_get_name (self), "proxynop-input"))
    {
      GeglGraph *graph = g_object_get_data (G_OBJECT (self), "graph");

      if (graph)
        depends_on = g_slist_concat (depends_on,
                                     gegl_node_get_depends_on (GEGL_NODE (graph)));
    }

  return depends_on;
}

static void
visitable_accept (GeglVisitable *visitable,
                  GeglVisitor   *visitor)
{
  gegl_visitor_visit_node (visitor, (GeglNode *) visitable);
}

static GSList *
visitable_depends_on (GeglVisitable *visitable)
{
  GeglNode *self = GEGL_NODE (visitable);

  return gegl_node_get_depends_on (self);
}

static gboolean
visitable_needs_visiting (GeglVisitable *visitable)
{
  return TRUE;
}

static void
gegl_node_set_op_class (GeglNode    *node,
                        const gchar *op_class,
                        const gchar *first_property,
                        va_list      var_args)
{
  g_return_if_fail (GEGL_IS_NODE (node));
  g_return_if_fail (op_class);

  if (op_class && op_class[0] != '\0')
    {
      GType          type;
      GeglOperation *operation;

      type = gegl_operation_gtype_from_name (op_class);

      if (!type)
        {
          g_warning ("Failed to set operation type %s, using a passthrough op instead", op_class);
          if (strcmp (op_class, "nop"))
            {
              gegl_node_set_op_class (node, "nop", NULL, var_args);
            }
          else
            {
              g_warning ("The failing op was 'nop' this means that GEGL was unable to locate any of it's\n"
                         "plug-ins. Try making GEGL_PATH point to the directory containing the .so|.dll\n"
                         "files with the image processing plug-ins, optionally you could try to make it\n"
                         "point to the operations directory of a GEGL sourcetree with a build.");
            }
          return;
        }

      if (node->operation &&
          type == G_OBJECT_TYPE (node->operation))
        {
          gegl_node_set_valist (node, first_property, var_args);
          return;
        }

      operation = GEGL_OPERATION (g_object_new_valist (type, first_property,
                                                       var_args));
      gegl_node_set_operation_object (node, operation);
      g_object_unref (operation);
    }
}


static void
property_changed (GObject    *gobject,
                  GParamSpec *arg1,
                  gpointer    user_data)
{
  GeglNode *self = GEGL_NODE (user_data);

  if (self->operation &&
      arg1 != user_data &&
      g_type_is_a (G_OBJECT_TYPE (self->operation), GEGL_TYPE_OPERATION_META))
    {
      gegl_operation_meta_property_changed (
        GEGL_OPERATION_META (self->operation), arg1, user_data);
    }

  if (arg1 != user_data &&
      ((arg1 &&
        arg1->value_type != GEGL_TYPE_BUFFER) ||
       (self->operation && !arg1)))
    {
      if (self->operation && !arg1)
        { /* these means we were called due to a operation change

             FIXME: The logic of this if is not quite intuitive,
             perhaps the thing being checked should be slightly different,
             or perhaps a bug lurks here?
           */
          GeglRectangle dirty_rect;
/*          GeglRectangle new_have_rect;*/

          dirty_rect = self->have_rect;
          /*new_have_rect = gegl_node_get_bounding_box (self);

             gegl_rectangle_bounding_box (&dirty_rect,
                                       &dirty_rect,
                                       &new_have_rect);*/

          gegl_node_invalidated (self, &dirty_rect);
        }
      else
        {
          /* we were called due to a property change */
          GeglRectangle dirty_rect;
          GeglRectangle new_have_rect;

          dirty_rect    = self->have_rect;
          new_have_rect = gegl_node_get_bounding_box (self);

          gegl_rectangle_bounding_box (&dirty_rect,
                                       &dirty_rect,
                                       &new_have_rect);

          gegl_node_invalidated (self, &dirty_rect);
        }
    }
}

static void
gegl_node_set_operation_object (GeglNode      *self,
                                GeglOperation *operation)
{
  g_return_if_fail (GEGL_IS_NODE (self));

  if (!operation)
    return;

  g_return_if_fail (GEGL_IS_OPERATION (operation));

  {
    GSList   *output_c        = NULL;
    GeglNode *output          = NULL;
    gchar    *output_dest_pad = NULL;
    GeglNode *input           = NULL;
    GeglNode *aux             = NULL;

    if (self->operation)
      g_object_unref (self->operation);

    g_object_ref (operation);
    self->operation = operation;

    /* FIXME: handle multiple outputs */

    if (gegl_node_get_pad (self, "output"))
      output_c = gegl_pad_get_connections (gegl_node_get_pad (self, "output"));
    if (output_c && output_c->data)
      {
        GeglConnection *connection = output_c->data;
        GeglPad        *pad;

        output          = gegl_connection_get_sink_node (connection);
        pad             = gegl_connection_get_sink_pad (connection);
        output_dest_pad = g_strdup (pad->param_spec->name);
      }
    input = gegl_node_get_producer (self, "input", NULL);
    aux   = gegl_node_get_producer (self, "aux", NULL);

    gegl_node_disconnect_sources (self);
    gegl_node_disconnect_sinks (self);

    /* FIXME: handle this in a more generic way, but it is needed to allow
     * the attach to work properly.
     */

    if (gegl_node_get_pad (self, "output"))
      gegl_node_remove_pad (self, gegl_node_get_pad (self, "output"));
    if (gegl_node_get_pad (self, "input"))
      gegl_node_remove_pad (self, gegl_node_get_pad (self, "input"));
    if (gegl_node_get_pad (self, "aux"))
      gegl_node_remove_pad (self, gegl_node_get_pad (self, "aux"));

    gegl_operation_attach (operation, self);

    if (input)
      gegl_node_connect_from (self, "input", input, "output");
    if (aux)
      gegl_node_connect_from (self, "aux", aux, "output");
    if (output)
      gegl_node_connect_to (self, "output", output, output_dest_pad);

    if (output_dest_pad)
      g_free (output_dest_pad);
  }

  g_signal_connect (G_OBJECT (operation), "notify", G_CALLBACK (property_changed), self);
  property_changed (G_OBJECT (operation), (GParamSpec *) self, self);
}

void
gegl_node_set (GeglNode    *self,
               const gchar *first_property_name,
               ...)
{
  va_list var_args;

  g_return_if_fail (GEGL_IS_NODE (self));

  va_start (var_args, first_property_name);
  gegl_node_set_valist (self, first_property_name, var_args);
  va_end (var_args);
}

void
gegl_node_get (GeglNode    *self,
               const gchar *first_property_name,
               ...)
{
  va_list var_args;

  g_return_if_fail (GEGL_IS_NODE (self));
  g_return_if_fail (self->is_graph || GEGL_IS_OPERATION (self->operation));

  va_start (var_args, first_property_name);
  gegl_node_get_valist (self, first_property_name, var_args);
  va_end (var_args);
}

void
gegl_node_set_valist (GeglNode    *self,
                      const gchar *first_property_name,
                      va_list      var_args)
{
  const gchar *property_name;

  g_return_if_fail (GEGL_IS_NODE (self));

  g_object_ref (self);

  g_object_freeze_notify (G_OBJECT (self));

  property_name = first_property_name;
  while (property_name)
    {
      GValue      value = { 0, };
      GParamSpec *pspec = NULL;
      gchar      *error = NULL;

      if (!strcmp (property_name, "operation"))
        {
          const gchar *op_class;
          const gchar *op_first_property;

          op_class          = va_arg (var_args, gchar *);
          op_first_property = va_arg (var_args, gchar *);

          /* pass the following properties as construction properties
           * to the operation */
          gegl_node_set_op_class (self, op_class, op_first_property, var_args);
          break;
        }
      else if (!strcmp (property_name, "name"))
        {
          pspec = g_object_class_find_property (
            G_OBJECT_GET_CLASS (G_OBJECT (self)), property_name);

          g_value_init (&value, G_PARAM_SPEC_VALUE_TYPE (pspec));
          G_VALUE_COLLECT (&value, var_args, 0, &error);
          if (error)
            {
              g_warning ("%s: %s", G_STRFUNC, error);
              g_free (error);
              g_value_unset (&value);
              break;
            }
          g_object_set_property (G_OBJECT (self), property_name, &value);
          g_value_unset (&value);
        }
      else
        {
          if (self->operation)
            {
              pspec = g_object_class_find_property (
                G_OBJECT_GET_CLASS (G_OBJECT (self->operation)), property_name);
            }
          if (!pspec)
            {
              g_warning ("%s:%s has no property named: '%s'",
                         G_STRFUNC,
                         gegl_node_get_debug_name (self), property_name);
              break;
            }
          if (!(pspec->flags & G_PARAM_WRITABLE))
            {
              g_warning ("%s: property (%s of operation class '%s' is not writable",
                         G_STRFUNC,
                         pspec->name,
                         G_OBJECT_TYPE_NAME (self->operation));
              break;
            }

          g_value_init (&value, G_PARAM_SPEC_VALUE_TYPE (pspec));
          G_VALUE_COLLECT (&value, var_args, 0, &error);
          if (error)
            {
              g_warning ("%s: %s", G_STRFUNC, error);
              g_free (error);
              g_value_unset (&value);
              break;
            }
          g_object_set_property (G_OBJECT (self->operation), property_name, &value);
          g_value_unset (&value);
        }

      property_name = va_arg (var_args, gchar *);
    }
  g_object_thaw_notify (G_OBJECT (self));

  g_object_unref (self);
}

void
gegl_node_get_valist (GeglNode    *self,
                      const gchar *first_property_name,
                      va_list      var_args)
{
  const gchar *property_name;

  g_return_if_fail (G_IS_OBJECT (self));

  g_object_ref (self);

  property_name = first_property_name;

  while (property_name)
    {
      GValue      value = { 0, };
      GParamSpec *pspec;
      gchar      *error;

      if (!strcmp (property_name, "class"))
        {
          g_warning ("Getting a deprecated property \"class\" "
                     "use \"operation\" instead");
          property_name = "operation";
        }

      if (!strcmp (property_name, "operation") ||
          !strcmp (property_name, "name"))
        {
          pspec = g_object_class_find_property (
            G_OBJECT_GET_CLASS (G_OBJECT (self)), property_name);
        }
      else
        {
          if (self->is_graph)
            {
              pspec = g_object_class_find_property (
                G_OBJECT_GET_CLASS (G_OBJECT (
                                      gegl_node_get_output_proxy (self, "output")->operation)), property_name);
              if (!pspec)
                {
                  pspec = g_object_class_find_property (
                    G_OBJECT_GET_CLASS (G_OBJECT (self->operation)), property_name);
                }
            }
          else
            {
              pspec = g_object_class_find_property (
                G_OBJECT_GET_CLASS (G_OBJECT (self->operation)), property_name);
            }

          if (!pspec)
            {
              g_warning ("%s:%s has no property named: '%s'",
                         G_STRFUNC,
                         gegl_node_get_debug_name (self), property_name);
              break;
            }
          if (!(pspec->flags & G_PARAM_READABLE))
            {
              g_warning ("%s: property '%s' of operation class '%s' is not readable",
                         G_STRFUNC,
                         property_name,
                         G_OBJECT_TYPE_NAME (self->operation));
            }
        }

      g_value_init (&value, G_PARAM_SPEC_VALUE_TYPE (pspec));
      gegl_node_get_property (self, property_name, &value);
      G_VALUE_LCOPY (&value, var_args, 0, &error);
      if (error)
        {
          g_warning ("%s: %s", G_STRFUNC, error);
          g_free (error);
          g_value_unset (&value);
          break;
        }
      g_value_unset (&value);

      property_name = va_arg (var_args, gchar *);
    }
  g_object_unref (self);
}

void
gegl_node_set_property (GeglNode     *self,
                        const gchar  *property_name,
                        const GValue *value)
{
  g_return_if_fail (GEGL_IS_NODE (self));
  g_return_if_fail (property_name != NULL);
  g_return_if_fail (value != NULL);

  if (!strcmp (property_name, "class"))
    {
      g_warning ("Setting a deprecated property \"class\", "
                 "use \"operation\" instead.");

      g_object_set_property (G_OBJECT (self), "operation", value);
    }

  if (!strcmp (property_name, "operation") ||
      !strcmp (property_name, "name"))
    {
      g_object_set_property (G_OBJECT (self),
                             property_name, value);
    }
  else
    {
      if (self->operation)
        {
          g_object_set_property (G_OBJECT (self->operation),
                                 property_name, value);
        }
    }
}

void
gegl_node_get_property (GeglNode    *self,
                        const gchar *property_name,
                        GValue      *value)
{
  g_return_if_fail (GEGL_IS_NODE (self));
  g_return_if_fail (property_name != NULL);
  g_return_if_fail (value != NULL);

  if (!strcmp (property_name, "operation") ||
      !strcmp (property_name, "name"))
    {
      g_object_get_property (G_OBJECT (self),
                             property_name, value);
    }
  else
    {
      if (self->is_graph &&
          !strcmp (property_name, "output"))
        {
          g_warning ("Eeek!");
          g_object_get_property (G_OBJECT (gegl_node_get_output_proxy (self, "output")->operation),
                                 property_name, value);
        }
      else
        {
          if (self->operation)
            {
              g_object_get_property (G_OBJECT (self->operation),
                                     property_name, value);
            }
        }
    }
}

GParamSpec *
gegl_node_find_property (GeglNode    *self,
                         const gchar *property_name)
{
  GParamSpec *pspec = NULL;

  g_return_val_if_fail (GEGL_IS_NODE (self), NULL);
  g_return_val_if_fail (property_name != NULL, NULL);

  if (self->operation)
    pspec = g_object_class_find_property (
      G_OBJECT_GET_CLASS (G_OBJECT (self->operation)), property_name);
  if (!pspec)
    pspec = g_object_class_find_property (
      G_OBJECT_GET_CLASS (G_OBJECT (self)), property_name);
  return pspec;
}

const gchar *
gegl_node_get_operation (const GeglNode *node)
{
  if (node == NULL)
    return NULL;

  if (node->operation == NULL)
    {
      if (node->is_graph)
        return "GraphNode";

      return NULL;
    }

  return GEGL_OPERATION_GET_CLASS (node->operation)->name;
}

void
gegl_node_set_need_rect (GeglNode *node,
                         gpointer  context_id,
                         gint      x,
                         gint      y,
                         gint      width,
                         gint      height)
{
  GeglNodeContext *context;

  g_return_if_fail (GEGL_IS_NODE (node));
  g_return_if_fail (context_id != NULL);

  context = gegl_node_get_context (node, context_id);

  context->need_rect.x      = x;
  context->need_rect.y      = y;
  context->need_rect.width  = width;
  context->need_rect.height = height;
}

const gchar *
gegl_node_get_debug_name (GeglNode *node)
{
  static gchar  ret_buf[512];

  const gchar  *name;
  const gchar  *operation;

  g_return_val_if_fail (GEGL_IS_NODE (node), NULL);

  name      = gegl_node_get_name (node);
  operation = gegl_node_get_operation (node);

  if (name && *name)
    {
      g_snprintf (ret_buf, sizeof (ret_buf),
                  "%s named %s", operation ? operation : "(none)", name);
    }
  else
    {
      g_snprintf (ret_buf, sizeof (ret_buf),
                  "%s", operation ? operation : "(none)");
    }

  return ret_buf;
}

GeglNode *
gegl_node_get_producer (GeglNode *node,
                        gchar    *pad_name,
                        gchar   **output_pad_name)
{
  GeglPad *pad;

  g_return_val_if_fail (GEGL_IS_NODE (node), NULL);
  g_return_val_if_fail (pad_name != NULL, NULL);

  pad = gegl_node_get_pad (node, pad_name);

  if (pad &&
      gegl_pad_is_input (pad) &&
      gegl_pad_get_num_connections (pad) == 1)
    {
      GeglConnection *connection = g_slist_nth_data (pad->connections, 0);

      if (output_pad_name)
        {
          GeglPad *pad = gegl_connection_get_source_pad (connection);
          *output_pad_name = g_strdup (gegl_pad_get_name (pad));
        }
      return gegl_connection_get_source_node (connection);
    }
  return NULL;
}

GeglRectangle
gegl_node_get_bounding_box (GeglNode *root)
{
  GeglRectangle dummy = { 0, 0, 0, 0 };
  GeglVisitor  *prepare_visitor;
  GeglVisitor  *have_visitor;
  GeglVisitor  *finish_visitor;
  guchar       *id;
  gint          i;

  GeglPad      *pad;

  if (!root)
    return dummy;
  pad = gegl_node_get_pad (root, "output");
  if (pad && pad->node != root)
    {
      root = pad->node;
    }
  if (!pad || !root)
    return dummy;
  g_object_ref (root);

  id = g_malloc (1);

  for (i = 0; i < 2; i++)
    {
      prepare_visitor = g_object_new (GEGL_TYPE_PREPARE_VISITOR, "id", id, NULL);
      gegl_visitor_dfs_traverse (prepare_visitor, GEGL_VISITABLE (root));
      g_object_unref (prepare_visitor);
    }

  have_visitor = g_object_new (GEGL_TYPE_HAVE_VISITOR, "id", id, NULL);
  gegl_visitor_dfs_traverse (have_visitor, GEGL_VISITABLE (root));
  g_object_unref (have_visitor);

  finish_visitor = g_object_new (GEGL_TYPE_FINISH_VISITOR, "id", id, NULL);
  gegl_visitor_dfs_traverse (finish_visitor, GEGL_VISITABLE (root));
  g_object_unref (finish_visitor);

  g_object_unref (root);
  g_free (id);

  return root->have_rect;
}

void
gegl_node_process (GeglNode *self)
{
  GeglProcessor *processor;

  g_return_if_fail (GEGL_IS_NODE (self));

  processor = gegl_node_new_processor (self, NULL);

  while (gegl_processor_work (processor, NULL)) ;
  gegl_processor_destroy (processor);
}

#if 0
/* simplest form of GeglProcess that processes all data in one
 *
 * single large chunk
 */
void
gegl_node_process (GeglNode *self)
{
  GeglNode        *input;
  GeglNodeContext *context;
  GeglBuffer      *buffer;
  GeglRectangle    defined;

  g_return_if_fail (GEGL_IS_NODE (self));
  g_return_if_fail (g_type_is_a (G_OBJECT_TYPE (self->operation),
                                 GEGL_TYPE_OPERATION_SINK));

  input   = gegl_node_get_producer (self, "input", NULL);
  defined = gegl_node_get_bounding_box (input);
  buffer  = gegl_node_apply_roi (input, "output", &defined);

  g_assert (GEGL_IS_BUFFER (buffer));
  context = gegl_node_add_context (self, &defined);

  {
    GValue value = { 0, };
    g_value_init (&value, GEGL_TYPE_BUFFER);
    g_value_set_object (&value, buffer);
    gegl_node_context_set_property (context, "input", &value);
    g_value_unset (&value);
  }

  gegl_node_context_set_result_rect (context, defined.x, defined.y, defined.width, defined.h);
  gegl_operation_process (self->operation, &defined, "foo");
  gegl_node_remove_context (self, &defined);
  g_object_unref (buffer);
}
#endif

static gint
lookup_context (gconstpointer a,
                gconstpointer context_id)
{
  GeglNodeContext *context = (void *) a;

  if (context->context_id == context_id)
    return 0;
  return -1;
}

void babl_backtrack (void);

GeglNodeContext *
gegl_node_get_context (GeglNode *self,
                       gpointer  context_id)
{
  GSList          *found;
  GeglNodeContext *context = NULL;

  g_return_val_if_fail (GEGL_IS_NODE (self), NULL);
  g_return_val_if_fail (context_id != NULL, NULL);

  found = g_slist_find_custom (self->context, context_id, lookup_context);
  if (found)
    context = found->data;
  else
    {
      g_warning ("didn't find %p", context_id);
      babl_backtrack ();
    }
  return context;
}

void
gegl_node_remove_context (GeglNode *self,
                          gpointer  context_id)
{
  GeglNodeContext *context;

  g_return_if_fail (GEGL_IS_NODE (self));
  g_return_if_fail (context_id != NULL);

  context = gegl_node_get_context (self, context_id);
  if (!context)
    {
      g_warning ("didn't find context %p for %s",
                 context_id, gegl_node_get_debug_name (self));
      return;
    }
  self->context = g_slist_remove (self->context, context);
  g_object_unref (context);
}

GeglNodeContext *
gegl_node_add_context (GeglNode *self,
                       gpointer  context_id)
{
  GeglNodeContext *context = NULL;
  GSList          *found;

  g_return_val_if_fail (GEGL_IS_NODE (self), NULL);
  g_return_val_if_fail (context_id != NULL, NULL);

  found = g_slist_find_custom (self->context, context_id, lookup_context);

  if (found)
    context = found->data;

  if (context)
    {
      /* silently ignore, since multiple traversals of prepare are done
       * to saturate the graph */
      return context;
    }

  context             = g_object_new (GEGL_TYPE_NODE_CONTEXT, NULL);
  context->node       = self;
  context->context_id = context_id;
  self->context       = g_slist_prepend (self->context, context);
  return context;
}

GeglNode *
gegl_node_detect (GeglNode *root,
                  gint      x,
                  gint      y)
{
  if (root)
    {
      /* make sure the have rects are computed */
      /* FIXME: do not call this all the time! */
      gegl_node_get_bounding_box (root);

      if (root->operation)
        return gegl_operation_detect (root->operation, x, y);
      else
        {
          if (root->is_graph)
            {
              GeglNode *foo = gegl_node_get_output_proxy (root, "output");
              if (foo && foo != root)
                return gegl_node_detect (foo, x, y);
            }
        }
    }
  return root;
}

/* this is a bit hacky, but allows us to insert a node into the graph,
 * and avoid the full defined region of the entire graph to change
 */
void
gegl_node_insert_before (GeglNode *self,
                         GeglNode *to_be_inserted)
{
  GeglNode     *other;
  GeglRectangle rectangle;

  g_return_if_fail (GEGL_IS_NODE (self));
  g_return_if_fail (GEGL_IS_NODE (to_be_inserted));

  other     = gegl_node_get_producer (self, "input", NULL);/*XXX: handle pad name */
  rectangle = gegl_node_get_bounding_box (to_be_inserted);

  g_signal_handlers_block_matched (other, G_SIGNAL_MATCH_FUNC, 0, 0, 0, source_invalidated, NULL);
  /* the blocked handler disappears during the relinking */
  gegl_node_link_many (other, to_be_inserted, self, NULL);

  /* emit the change ourselves */
  gegl_node_invalidated (self, &rectangle);
}

gint
gegl_node_get_consumers (GeglNode      *node,
                         const gchar   *output_pad,
                         GeglNode    ***nodes,
                         const gchar ***pads)
{
  GSList  *connections;
  gint     n_connections;
  GeglPad *pad;
  gchar  **pasp = NULL;

  g_return_val_if_fail (GEGL_IS_NODE (node), 0);
  g_return_val_if_fail (output_pad != NULL, 0);

  pad = gegl_node_get_pad (node, output_pad);

  if (!pad)
    {
      g_warning ("%s: no such pad %s for %s",
                 G_STRFUNC, output_pad, gegl_node_get_debug_name (node));
      return 0;
    }

  connections = gegl_pad_get_connections (pad);
  {
    GSList *iter;
    gint    pasp_size = 0;
    gint    i;
    gint    pasp_pos = 0;

    n_connections = g_slist_length (connections);
    pasp_size    += (n_connections + 1) * sizeof (gchar *);

    for (iter = connections; iter; iter = g_slist_next (iter))
      {
        GeglConnection *connection = iter->data;
        GeglPad        *pad        = gegl_connection_get_sink_pad (connection);
        pasp_size += strlen (gegl_pad_get_name (pad)) + 1;
      }
    if (nodes)
      *nodes = g_malloc ((n_connections + 1) * sizeof (void *));
    if (pads)
      {
        pasp  = g_malloc (pasp_size);
        *pads = (void *) pasp;
      }
    i        = 0;
    pasp_pos = (n_connections + 1) * sizeof (void *);
    for (iter = connections; iter; iter = g_slist_next (iter))
      {
        GeglConnection *connection = iter->data;
        GeglPad        *pad        = gegl_connection_get_sink_pad (connection);
        GeglNode       *node       = gegl_connection_get_sink_node (connection);
        const gchar    *pad_name   = gegl_pad_get_name (pad);

        if (nodes)
          (*nodes)[i] = node;
        if (pasp)
          {
            pasp[i] = ((gchar *) pasp) + pasp_pos;
            strcpy (pasp[i], pad_name);
          }
        pasp_pos += strlen (pad_name) + 1;
        i++;
      }
    if (nodes)
      (*nodes)[i] = NULL;
    if (pads)
      pasp[i] = NULL;
  }
  return n_connections;
}

static void
computed_event (GeglCache *self,
                void      *foo,
                void      *user_data)
{
  GeglNode *node = GEGL_NODE (user_data);

  g_signal_emit (node, gegl_node_signals[COMPUTED], 0, foo, NULL, NULL);
}

GeglCache *
gegl_node_get_cache (GeglNode *node)
{
  g_return_val_if_fail (GEGL_IS_NODE (node), NULL);

  if (!node->cache)
    {
      GeglPad    *pad;
      const Babl *format;

      pad = gegl_node_get_pad (node, "output");
      g_assert (pad);
      format = gegl_pad_get_format (pad);
      if (!format)
        {
          format = babl_format ("RGBA float");
        }

      node->cache = g_object_new (GEGL_TYPE_CACHE,
                                  "node", node,
                                  "format", format,
                                  NULL);
      g_signal_connect (G_OBJECT (node->cache), "computed",
                        (GCallback) computed_event,
                        node);
    }
  return node->cache;
}

const gchar *
gegl_node_get_name (GeglNode *self)
{
  GeglNodePrivate *priv;

  g_return_val_if_fail (GEGL_IS_NODE (self), NULL);

  priv = GEGL_NODE_GET_PRIVATE (self);

  return priv->name;
}

void
gegl_node_set_name (GeglNode    *self,
                    const gchar *name)
{
  GeglNodePrivate *priv;

  g_return_if_fail (GEGL_IS_NODE (self));

  priv = GEGL_NODE_GET_PRIVATE (self);

  if (priv->name)
    g_free (priv->name);
  priv->name = g_strdup (name);
}

void
gegl_node_remove_children (GeglNode *self)
{
  g_return_if_fail (GEGL_IS_NODE (self));

  while (TRUE)
    {
      GeglNode *child = gegl_node_get_nth_child (self, 0);

      if (child)
        gegl_node_remove_child (self, child);
      else
        break;
    }
}

GeglNode *
gegl_node_add_child (GeglNode *self,
                     GeglNode *child)
{
  GeglNodePrivate *self_priv;
  GeglNodePrivate *child_priv;

  g_return_val_if_fail (GEGL_IS_NODE (self), NULL);
  g_return_val_if_fail (GEGL_IS_NODE (child), NULL);

  self_priv  = GEGL_NODE_GET_PRIVATE (self);
  child_priv = GEGL_NODE_GET_PRIVATE (child);

  self_priv->children = g_slist_prepend (self_priv->children,
                                         g_object_ref (child));
  self->is_graph = TRUE;
  child_priv->parent  = self;

  return child;
}

GeglNode *
gegl_node_remove_child (GeglNode *self,
                        GeglNode *child)
{
  GeglNodePrivate *self_priv;
  GeglNodePrivate *child_priv;

  g_return_val_if_fail (GEGL_IS_NODE (self), NULL);
  g_return_val_if_fail (GEGL_IS_NODE (child), NULL);

  self_priv  = GEGL_NODE_GET_PRIVATE (self);
  child_priv = GEGL_NODE_GET_PRIVATE (child);

  g_assert (child_priv->parent == self ||
            child_priv->parent == NULL);
  g_return_val_if_fail (GEGL_IS_NODE (child), NULL);

  self_priv->children = g_slist_remove (self_priv->children, child);

  if (child_priv->parent != NULL)
    {
      /* if parent isn't set then the node is already in dispose
       */
      child_priv->parent = NULL;
      g_object_unref (child);
    }


  if (self_priv->children == NULL)
    self->is_graph = FALSE;

  return child;
}

GeglNode *
gegl_node_get_parent (GeglNode *self)
{
  GeglNodePrivate *self_priv;

  g_return_val_if_fail (GEGL_IS_NODE (self), NULL);

  self_priv  = GEGL_NODE_GET_PRIVATE (self);
  return self_priv->parent;
}

GeglNode *
gegl_node_adopt_child (GeglNode *self,
                       GeglNode *child)
{
  GeglNode *old_parent;

  g_return_val_if_fail (GEGL_IS_NODE (child), NULL);
  g_object_ref (child);
  old_parent = gegl_node_get_parent (child);
  if (old_parent)
    {
      gegl_node_remove_child (old_parent, child);
    }

  if (self)
    {
      gegl_node_add_child (self, child);
    }
  else
    {
      g_object_ref (child);
    }

  g_object_unref (child);
  return child;
}

gint
gegl_node_get_num_children (GeglNode *self)
{
  GeglNodePrivate *priv;

  g_return_val_if_fail (GEGL_IS_NODE (self), -1);

  priv = GEGL_NODE_GET_PRIVATE (self);

  return g_slist_length (priv->children);
}

GeglNode *
gegl_node_get_nth_child (GeglNode *self,
                         gint      n)
{
  GeglNodePrivate *priv;

  g_return_val_if_fail (GEGL_IS_NODE (self), NULL);

  priv = GEGL_NODE_GET_PRIVATE (self);

  return g_slist_nth_data (priv->children, n);
}

/*
 * Returns a copy of the graphs internal list of nodes
 */
GSList *
gegl_node_get_children (GeglNode *self)
{
  GeglNodePrivate *priv;

  g_return_val_if_fail (GEGL_IS_NODE (self), NULL);

  priv = GEGL_NODE_GET_PRIVATE (self);

  return g_slist_copy (priv->children);
}

/*
 *  returns a freshly created node, owned by the graph, and thus freed with it
 */
GeglNode *
gegl_node_new_child (GeglNode    *parent,
                     const gchar *first_property_name,
                     ...)
{
  GeglNode    *node;
  va_list      var_args;
  const gchar *name;

  node = g_object_new (GEGL_TYPE_NODE, NULL);
  if (parent)
    gegl_node_add_child (parent, node);

  name = first_property_name;
  va_start (var_args, first_property_name);
  gegl_node_set_valist (node, first_property_name, var_args);
  va_end (var_args);

  if (parent)
    g_object_unref (node);
  return node;
}

GeglNode *
gegl_node_create_child (GeglNode    *self,
                        const gchar *operation)
{
  g_return_val_if_fail (operation != NULL, NULL);

  return gegl_node_new_child (self, "operation", operation, NULL);
}

static void
graph_source_invalidated (GeglNode            *source,
                          const GeglRectangle *rect,
                          gpointer             data)
{
  GeglNode      *destination = GEGL_NODE (data);
  GeglRectangle  dirty_rect  = *rect;

  if (0) g_warning ("graph:%s is dirtied from %s (%i,%i %ix%i)",
                    gegl_node_get_debug_name (destination),
                    gegl_node_get_debug_name (source),
                    rect->x, rect->y,
                    rect->width, rect->height);

  g_signal_emit (destination, gegl_node_signals[INVALIDATED], 0,
                 &dirty_rect, NULL);
}


static GeglNode *
gegl_node_get_pad_proxy (GeglNode    *graph,
                         const gchar *name,
                         gboolean     is_graph_input)
{
  GeglNode *node = graph;
  GeglPad  *pad;

  pad = gegl_node_get_pad (node, name);
  if (!pad)
    {
      GeglNode *nop     = g_object_new (GEGL_TYPE_NODE, "operation", "nop", "name", is_graph_input ? "proxynop-input" : "proxynop-output", NULL);
      GeglPad  *nop_pad = gegl_node_get_pad (nop, is_graph_input ? "input" : "output");
      gegl_node_add_child (graph, nop);
      g_object_unref (nop); /* our reference is made by the
                               gegl_node_add_child call */

      {
        GeglPad *new_pad = g_object_new (GEGL_TYPE_PAD, NULL);
        gegl_pad_set_param_spec (new_pad, nop_pad->param_spec);
        gegl_pad_set_node (new_pad, nop);
        gegl_pad_set_name (new_pad, name);
        gegl_node_add_pad (node, new_pad);

        /* hack, decoreating the pad to make it recognized in later
         * processing
         */
        if (!strcmp (name, "aux"))
          {
            g_object_set_data (G_OBJECT (nop), "is-aux", "foo");
          }
      }

      g_object_set_data (G_OBJECT (nop), "graph", graph);

      if (!is_graph_input)
        {
          g_signal_connect (G_OBJECT (nop), "invalidated",
                            G_CALLBACK (graph_source_invalidated), graph);
        }
      return nop;
    }
  return gegl_pad_get_node (pad);
}

GeglNode *
gegl_node_get_input_proxy (GeglNode    *node,
                           const gchar *name)
{
  g_return_val_if_fail (GEGL_IS_NODE (node), NULL);

  return gegl_node_get_pad_proxy (node, name, TRUE);
}

GeglNode *
gegl_node_get_output_proxy (GeglNode    *node,
                            const gchar *name)
{
  g_return_val_if_fail (GEGL_IS_NODE (node), NULL);

  return gegl_node_get_pad_proxy (node, name, FALSE);
}

GeglNode *
gegl_node_new (void)
{
  return g_object_new (GEGL_TYPE_NODE, NULL);
}
