#include "ultralight_bridge.h"
#include <Ultralight/Ultralight.h>

using namespace ultralight;

struct shenlong_ul_view {
  Ref<Renderer> renderer;
  Ref<View> view;
  unsigned width = 0, height = 0;
};

extern "C" shenlong_ul_view *shenlong_ul_create(unsigned width, unsigned height) {
  if (!width || !height) return nullptr;
  auto *out = new shenlong_ul_view();
  out->width = width; out->height = height;
  out->renderer = Renderer::Create();
  out->view = out->renderer->CreateView(width, height, false);
  return out->view ? out : nullptr;
}

extern "C" int shenlong_ul_load_html(shenlong_ul_view *ctx, const char *html,
                                      const char *base_url) {
  if (!ctx || !ctx->view || !html) return 0;
  ctx->view->LoadHTML(String(html), String(base_url ? base_url : "file:///ui/"));
  return 1;
}

extern "C" void shenlong_ul_update(shenlong_ul_view *ctx) {
  if (!ctx || !ctx->renderer) return;
  ctx->renderer->Update();
  ctx->renderer->Render();
}

extern "C" const unsigned char *shenlong_ul_bitmap(shenlong_ul_view *ctx,
                                                     unsigned *width,
                                                     unsigned *height,
                                                     unsigned *row_bytes) {
  if (!ctx || !ctx->view || !ctx->view->bitmap()) return nullptr;
  auto bitmap = ctx->view->bitmap();
  if (width) *width = bitmap->width();
  if (height) *height = bitmap->height();
  if (row_bytes) *row_bytes = bitmap->row_bytes();
  return bitmap->raw_pixels();
}

extern "C" void shenlong_ul_destroy(shenlong_ul_view *ctx) { delete ctx; }
