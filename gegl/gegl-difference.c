#include "gegl-difference.h"
#include "gegl-scanline-processor.h"
#include "gegl-image-iterator.h"
#include "gegl-utils.h"

static void class_init (GeglDifferenceClass * klass);
static void init (GeglDifference * self, GeglDifferenceClass * klass);

static GeglScanlineFunc get_scanline_func(GeglComp * comp, GeglColorSpaceType space, GeglChannelSpaceType type);

static void fg_difference_bg_float (GeglFilter * filter, GeglImageIterator ** iters, gint width);

static gpointer parent_class = NULL;

GType
gegl_difference_get_type (void)
{
  static GType type = 0;

  if (!type)
    {
      static const GTypeInfo typeInfo =
      {
        sizeof (GeglDifferenceClass),
        (GBaseInitFunc) NULL,
        (GBaseFinalizeFunc) NULL,
        (GClassInitFunc) class_init,
        (GClassFinalizeFunc) NULL,
        NULL,
        sizeof (GeglDifference),
        0,
        (GInstanceInitFunc) init,
        NULL
      };

      type = g_type_register_static (GEGL_TYPE_BLEND, 
                                     "GeglDifference", 
                                     &typeInfo, 
                                     0);
    }
    return type;
}

static void 
class_init (GeglDifferenceClass * klass)
{
  GeglCompClass *comp_class = GEGL_COMP_CLASS(klass);
  parent_class = g_type_class_peek_parent(klass);
  comp_class->get_scanline_func = get_scanline_func;
}

static void 
init (GeglDifference * self, 
      GeglDifferenceClass * klass)
{
}

/* scanline_funcs[data type] */
static GeglScanlineFunc scanline_funcs[] = 
{ 
  NULL, 
  NULL, 
  fg_difference_bg_float, 
  NULL 
};

static GeglScanlineFunc
get_scanline_func(GeglComp * comp,
                  GeglColorSpaceType space,
                  GeglChannelSpaceType type)
{
  return scanline_funcs[type];
}


static void                                                            
fg_difference_bg_float (GeglFilter * filter,              
                      GeglImageIterator ** iters,        
                      gint width)                       
{                                                                       
  gfloat **d = (gfloat**)gegl_image_iterator_color_channels(iters[0]);
  gfloat *da = (gfloat*)gegl_image_iterator_alpha_channel(iters[0]);
  gint d_color_chans = gegl_image_iterator_get_num_colors(iters[0]);

  gfloat **b = (gfloat**)gegl_image_iterator_color_channels(iters[1]);
  gfloat *ba = (gfloat*)gegl_image_iterator_alpha_channel(iters[1]);
  gint b_color_chans = gegl_image_iterator_get_num_colors(iters[1]);

  gfloat **f = (gfloat**)gegl_image_iterator_color_channels(iters[2]);
  gfloat * fa = (gfloat*)gegl_image_iterator_alpha_channel(iters[2]);
  gint f_color_chans = gegl_image_iterator_get_num_colors(iters[2]);

  gint alpha_mask = 0x0;

  if(ba) 
    alpha_mask |= GEGL_BG_ALPHA; 
  if(fa)
    alpha_mask |= GEGL_FG_ALPHA; 

  {
    gfloat *d0 = (d_color_chans > 0) ? d[0]: NULL;   
    gfloat *d1 = (d_color_chans > 1) ? d[1]: NULL;
    gfloat *d2 = (d_color_chans > 2) ? d[2]: NULL;

    gfloat *b0 = (b_color_chans > 0) ? b[0]: NULL;   
    gfloat *b1 = (b_color_chans > 1) ? b[1]: NULL;
    gfloat *b2 = (b_color_chans > 2) ? b[2]: NULL;

    gfloat *f0 = (f_color_chans > 0) ? f[0]: NULL;   
    gfloat *f1 = (f_color_chans > 1) ? f[1]: NULL;
    gfloat *f2 = (f_color_chans > 2) ? f[2]: NULL;

    switch(alpha_mask)
      {
      case GEGL_NO_ALPHA:
          {
            gfloat diff2;
            gfloat diff1;
            gfloat diff0;

            switch(d_color_chans)
              {
                case 3: 
                  while(width--)                                                        
                    {                                                                   
                      diff0 = *b0++ - *f0++;
                      *d0++ = ABS(diff0);
                      diff1 = *b1++ - *f1++;
                      *d1++ = ABS(diff1);
                      diff2 = *b2++ - *f2++;
                      *d2++ = ABS(diff2);
                    }
                  break;
                case 2: 
                  while(width--)                                                        
                    {                                                                   
                      diff0 = *b0++ - *f0++;
                      *d0++ = ABS(diff0);
                      diff1 = *b1++ - *f1++;
                      *d1++ = ABS(diff1);
                    }
                  break;
                case 1: 
                  while(width--)                                                        
                    {                                                                   
                      diff0 = *b0++ - *f0++;
                      *d0++ = ABS(diff0);
                    }
                  break;
              }
          }
        break;
      case GEGL_FG_ALPHA:
        g_warning("not implemented yet\n");
        break;
      case GEGL_BG_ALPHA:
        g_warning("not implemented yet\n");
        break;
      case GEGL_FG_BG_ALPHA:
          {
            gfloat diff2;
            gfloat diff1;
            gfloat diff0;

            gfloat a;
            gfloat b;

            switch(d_color_chans)
              {
                case 3: 
                  while(width--)                                                        
                    {                                                                   
                      a =  1.0 - 2.0 * *ba;
                      b =  1.0 - 2.0 * *fa;

                      diff0 = *fa * *b0 - *ba * *f0;
                      *d0++ = (diff0 >= 0.0) ? *b0 + a * *f0 : *f0 + b * *b0; f0++; b0++;
                      diff1 = *fa * *b1 - *ba * *f1;
                      *d1++ = (diff1 >= 0.0) ? *b1 + a * *f1 : *f1 + b * *b1; f1++; b1++;
                      diff2 = *fa * *b2 - *ba * *f2;
                      *d2++ = (diff2 >= 0.0) ? *b2 + a * *f2 : *f2 + b * *b2; f2++; b2++;

                      *da++ = *fa + *ba - *ba * *fa; fa++; ba++;
                    }
                  break;
                case 2: 
                  while(width--)                                                        
                    {                                                                   
                      a =  1.0 - 2.0 * *ba;
                      b =  1.0 - 2.0 * *fa;

                      diff0 = *fa * *b0 - *ba * *f0;
                      *d0++ = (diff0 >= 0.0) ? *b0 + a * *f0 : *f0 + b * *b0; f0++; b0++;
                      diff1 = *fa * *b1 - *ba * *f1;
                      *d1++ = (diff1 >= 0.0) ? *b1 + a * *f1 : *f1 + b * *b1; f1++; b1++;

                      *da++ = *fa + *ba - *ba * *fa; fa++; ba++;
                    }
                  break;
                case 1: 
                  while(width--)                                                        
                    {                                                                   
                      a =  1.0 - 2.0 * *ba;
                      b =  1.0 - 2.0 * *fa;

                      diff0 = *fa * *b0 - *ba * *f0;
                      *d0++ = (diff0 >= 0.0) ? *b0 + a * *f0 : *f0 + b * *b0; f0++; b0++;

                      *da++ = *fa + *ba - *ba * *fa; fa++; ba++;
                    }
                  break;
              }

          }
        break;
      }
  }

  g_free(d);
  g_free(b);
  g_free(f);
}                                                                       
