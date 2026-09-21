/* GEGL Operation: PS Style Colorize
 * Single color picker: picked color provides Hue/Saturation/Lightness
 * pick L(0~100) mapped to -100 ~ +100 for your colorize function
 * Fixed: remove C++ lambda, pure C compatible
 */
#include "config.h"
#include <glib/gi18n-lib.h>
#include <math.h>

#ifdef GEGL_PROPERTIES

property_color (tint_color, _("Tint Color"), "rgb(180,180,180)")
    description (_("Pick color; its Hue/Saturation are used, Lightness remapped to -100~100"))

#else

#define GEGL_OP_FILTER
#define GEGL_OP_NAME     ps_colorize
#define GEGL_OP_C_SOURCE ps-colorize.c
#include "gegl-op.h"

#define FLOAT_EPS       1e-12f

static inline gfloat sanitize_f(gfloat v)
{
  if (!isfinite(v))
    return 0.0f;
  return v;
}

static inline gfloat clamp_0_255(gfloat v)
{
  if (v < 0.0f) return 0.0f;
  if (v > 255.0f) return 255.0f;
  return v;
}

static void rgb8_to_hls(gdouble r8, gdouble g8, gdouble b8, gdouble *h_out, gdouble *l_out, gdouble *s_out)
{
  gdouble r = r8 / 255.0;
  gdouble g = g8 / 255.0;
  gdouble b = b8 / 255.0;
  gdouble maxc = MAX(MAX(r,g),b);
  gdouble minc = MIN(MIN(r,g),b);
  gdouble l = (maxc + minc) / 2.0;
  gdouble h=0, s=0;

  if (maxc != minc)
  {
    gdouble delta = maxc - minc;
    if (l < 0.5)
      s = delta / (maxc + minc);
    else
      s = delta / (2.0 - maxc - minc);

    if (maxc == r)
      h = fmod(((g - b)/delta),6.0);
    else if (maxc == g)
      h = ((b - r)/delta) + 2.0;
    else
      h = ((r - g)/delta) + 4.0;
    h *= 60.0;
    if (h < 0) h += 360.0;
  }
  *h_out = h;
  *l_out = l * 100.0;
  *s_out = s * 100.0;
}

// pure C replacement for C++ lambda hue2rgb
static gdouble hue2rgb(gdouble p, gdouble q, gdouble t)
{
  if (t < 0) t += 1;
  if (t > 1) t -= 1;
  if (t < 1.0/6.0) return p + (q-p)*6*t;
  if (t < 1.0/2.0) return q;
  if (t < 2.0/3.0) return p + (q-p)*(2.0/3.0 - t)*6;
  return p;
}

static void hls_to_rgb8(gdouble h, gdouble s, gdouble l, gdouble *r8, gdouble *g8, gdouble *b8)
{
  h /= 360.0;
  s /= 100.0;
  l /= 100.0;
  gdouble r,g,b;
  if (s == 0.0)
  {
    r = g = b = l;
  }
  else
  {
    gdouble q = (l < 0.5) ? (l*(1.0+s)) : (l+s - l*s);
    gdouble p = 2*l - q;
    r = hue2rgb(p,q,h + 1.0/3.0);
    g = hue2rgb(p,q,h);
    b = hue2rgb(p,q,h - 1.0/3.0);
  }
  *r8 = r * 255.0;
  *g8 = g * 255.0;
  *b8 = b * 255.0;
}

static inline void blend2(gdouble leftR, gdouble leftG, gdouble leftB,
                          gdouble rightR, gdouble rightG, gdouble rightB,
                          gdouble pos,
                          gdouble *outR, gdouble *outG, gdouble *outB)
{
  *outR = leftR * (1.0 - pos) + rightR * pos;
  *outG = leftG * (1.0 - pos) + rightG * pos;
  *outB = leftB * (1.0 - pos) + rightB * pos;
}

static inline void blend3(gdouble leftR, gdouble leftG, gdouble leftB,
                          gdouble mainR, gdouble mainG, gdouble mainB,
                          gdouble rightR, gdouble rightG, gdouble rightB,
                          gdouble pos,
                          gdouble *outR, gdouble *outG, gdouble *outB)
{
  if (pos < 0)
  {
    blend2(leftR,leftG,leftB, mainR,mainG,mainB, pos+1.0, outR,outG,outB);
  }
  else if (pos > 0)
  {
    blend2(mainR,mainG,mainB, rightR,rightG,rightB, pos, outR,outG,outB);
  }
  else
  {
    *outR = mainR;
    *outG = mainG;
    *outB = mainB;
  }
}

static inline void colorize_kernel(gfloat inR, gfloat inG, gfloat inB,
                                   gdouble new_h,
                                   gdouble new_sat,
                                   gdouble new_light,
                                   gfloat *outR, gfloat *outG, gfloat *outB)
{
  gdouble r8 = inR * 255.0;
  gdouble g8 = inG * 255.0;
  gdouble b8 = inB * 255.0;

  gdouble h_orig, l_orig, s_orig;
  rgb8_to_hls(r8, g8, b8, &h_orig, &l_orig, &s_orig);

  gdouble hueR, hueG, hueB;
  hls_to_rgb8(new_h, 100.0, 50.0, &hueR, &hueG, &hueB);

  gdouble colR, colG, colB;
  blend2(128,128,128, hueR,hueG,hueB, new_sat / 100.0, &colR, &colG, &colB);

  gdouble resR, resG, resB;
  if (new_light <= -100.0)
  {
    resR = 0; resG =0; resB=0;
  }
  else if (new_light >= 100.0)
  {
    resR =255; resG=255; resB=255;
  }
  else if (new_light >= 0.0)
  {
    gdouble pos = 2.0 * (1.0 - new_light /100.0) * (l_orig /100.0 - 1.0) + 1.0;
    blend3(0,0,0, colR,colG,colB, 255,255,255, pos, &resR, &resG, &resB);
  }
  else
  {
    gdouble pos = 2.0 * (1.0 + new_light /100.0) * (l_orig /100.0) -1.0;
    blend3(0,0,0, colR,colG,colB, 255,255,255, pos, &resR, &resG, &resB);
  }

  *outR = (gfloat)clamp_0_255(resR) / 255.0f;
  *outG = (gfloat)clamp_0_255(resG) / 255.0f;
  *outB = (gfloat)clamp_0_255(resB) / 255.0f;
}

static void
prepare(GeglOperation *op)
{
  gegl_operation_set_format(op, "input",  babl_format("RGBA float"));
  gegl_operation_set_format(op, "output", babl_format("RGBA float"));
}

static gboolean
process(GeglOperation       *op,
        GeglBuffer          *in_buf,
        GeglBuffer          *out_buf,
        const GeglRectangle *roi,
        gint                 level)
{
  if (!roi || roi->width <= 0 || roi->height <= 0)
    return TRUE;

  GeglColor *tint_color;
  g_object_get(G_OBJECT(op),
    "tint-color", &tint_color,
    NULL);

  gdouble r_col, g_col, b_col, a_col;
  gegl_color_get_rgba(tint_color, &r_col, &g_col, &b_col, &a_col);
  gdouble pick_h, pick_l, pick_s;
  rgb8_to_hls(r_col*255.0, g_col*255.0, b_col*255.0, &pick_h, &pick_l, &pick_s);
  g_object_unref(tint_color);

  // 映射：拾取颜色亮度0~100 → -100~100
  gdouble pick_light = pick_l * 2.0 - 100.0;

  gint stride = roi->width * 4;
  gfloat *in_line  = g_new(gfloat, stride);
  gfloat *out_line = g_new(gfloat, stride);

  for (gint y = 0; y < roi->height; y++)
  {
    GeglRectangle row_rect = { roi->x, roi->y + y, roi->width, 1 };
    gegl_buffer_get(in_buf, &row_rect, 1.0f, babl_format("RGBA float"),
                     in_line, GEGL_AUTO_ROWSTRIDE, GEGL_ABYSS_NONE);

    for (gint x = 0; x < roi->width; x++)
    {
      gint px = x * 4;
      gfloat r = in_line[px + 0];
      gfloat g = in_line[px + 1];
      gfloat b = in_line[px + 2];
      gfloat a = in_line[px + 3];

      gfloat ro, go, bo;
      colorize_kernel(r,g,b, pick_h, pick_s, pick_light, &ro, &go, &bo);

      out_line[px + 0] = sanitize_f(ro);
      out_line[px + 1] = sanitize_f(go);
      out_line[px + 2] = sanitize_f(bo);
      out_line[px + 3] = sanitize_f(a);
    }

    gegl_buffer_set(out_buf, &row_rect, 0, babl_format("RGBA float"),
                     out_line, GEGL_AUTO_ROWSTRIDE);
  }

  g_free(in_line);
  g_free(out_line);
  return TRUE;
}

static void
gegl_op_class_init(GeglOpClass *klass)
{
  GeglOperationClass       *oclass = GEGL_OPERATION_CLASS(klass);
  GeglOperationFilterClass *fclass = GEGL_OPERATION_FILTER_CLASS(klass);

  oclass->prepare = prepare;
  fclass->process = process;

  gegl_operation_class_set_keys(oclass,
    "name",        "lb:ps-colorize",
    "title",       _("PS Style Colorize"),
    "description", _("Single color picker colorize. Hue/Saturation/Lightness from picked color, lightness remapped to -100~100."),
    "gimp:menu-path", "<Image>/Colors/myfilters",
    "gimp:menu-label", _("PS Colorize..."),
    NULL);
}

#endif