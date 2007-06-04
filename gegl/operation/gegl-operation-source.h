/* This file is part of GEGL
 *
 * GEGL is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * GEGL is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with GEGL; if not, write to the
 * Free Software Foundation, Inc., 51 Franklin Street, Fifth Floor,
 * Boston, MA 02110-1301, USA.
 *
 * Copyright 2006 Øyvind Kolås
 */
#ifndef __GEGL_OPERATION_SOURCE_H__
#define __GEGL_OPERATION_SOURCE_H__

#include <glib-object.h>
#include "gegl-types.h"
#include "buffer/gegl-buffer.h"
#include "gegl-operation.h"

#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */

#define GEGL_TYPE_OPERATION_SOURCE               (gegl_operation_source_get_type ())
#define GEGL_OPERATION_SOURCE(obj)               (G_TYPE_CHECK_INSTANCE_CAST ((obj), GEGL_TYPE_OPERATION_SOURCE, GeglOperationSource))
#define GEGL_OPERATION_SOURCE_CLASS(klass)       (G_TYPE_CHECK_CLASS_CAST ((klass),  GEGL_TYPE_OPERATION_SOURCE, GeglOperationSourceClass))
#define GEGL_OPERATION_SOURCE_GET_CLASS(obj)     (G_TYPE_INSTANCE_GET_CLASS ((obj),  GEGL_TYPE_OPERATION_SOURCE, GeglOperationSourceClass))

typedef struct _GeglOperationSource  GeglOperationSource;
struct _GeglOperationSource
{
    GeglOperation  operation;
};

typedef struct _GeglOperationSourceClass GeglOperationSourceClass;
struct _GeglOperationSourceClass
{
   GeglOperationClass operation_class;
   gboolean (*process) (GeglOperation *self,
                        gpointer       context_id);
};

GType             gegl_operation_source_get_type                  (void) G_GNUC_CONST;

#ifdef __cplusplus
}
#endif /* __cplusplus */

#endif