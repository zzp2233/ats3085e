#include "gx_lvgx.h"
#include "gx_applet.h"
#include "gx_appletkit.h"
#include "gx_application.h"
#include "gx_environment.h"
#include "gx_fontfacemap.h"
#include "gx_fontmanager.h"
#include "gx_packagedatabase.h"
#include "gx_painter.h"
#include "gx_persimwear.h"
#include "gx_pixmappaintdevice.h"
#include "gx_platformadapter.h"
#include "gx_styleengine.h"
#include "gx_touchevent.h"
#include "gx_vglite_register.h"
#include "gx_widget.h"
#include "gx_widgetmanager.h"
#include "logging/log_ctrl.h"
#include "lvgl/lvgl.h"
#include "platform/gx_fs.h"
#include "platform/gx_time.h"
#include "src/core/lv_obj_pos.h"
#include "src/core/lv_obj_style.h"
#include "src/misc/lv_color.h"
#include "sys/printk.h"
#include <lvgl/lvgl_freetype_font.h>

#define GX_LOG_TAG "lvgl"
#include "gx_logger.h"

using namespace gx;

extern "C" {
extern lv_font_t lv_default_font;

void wait_vsync_sem(void);

extern const lv_obj_class_t lvgx_widget_class;
}

static lvgx *lvgxInstance;

class VDBPaintDevice : public PixmapPaintDevice {
public:
  VDBPaintDevice() GX_DEFAULT;
  void setBuffer(void *buffer, int w, int h) {
    data.pixels = static_cast<uint8_t *>(buffer);
    data.width = w;
    data.height = h;
    data.pitch = w * 2;
    data.pixfmt = PixelFormat::RGB565;
  }
};

class LVGLWindow : public lv_obj_t {
public:
  class WidgetWrapper : public Widget {
  public:
    WidgetWrapper() : m_active() {}

    lv_obj_t *lv_cast() const {
      return reinterpret_cast<lv_obj_t *>(
          intptr_t(this) -
          intptr_t(&static_cast<LVGLWindow *>(nullptr)->widget));
    }
    void onBeforePaint() {
      if (m_active) {
        m_active = false;
        lv_obj_clear_flag(lv_cast(), LV_OBJ_FLAG_HIDDEN);
      }
      if (isInvalidated()) {
        // LogInfo() << "onBeforePaint()";
        lv_obj_invalidate(lv_cast());
        clearInvalidation();
      }
    }
    void onAppletActiveChanged(Applet *applet, bool active) {
      LogInfo() << "onAppletActiveChanged:" << applet->objectName() << active;
      if (!active && AppletKit::instance()->appletStack().empty()) {
        lv_obj_add_flag(lv_cast(), LV_OBJ_FLAG_HIDDEN);
        m_active = false;
      } else if (active && AppletKit::instance()->appletStack().empty()) {
        m_active = true;
      }
    }

  private:
    bool m_active;
  };

  WidgetWrapper widget;

  LVGLWindow() {
    lv_obj_add_flag(this, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE |
                              LV_OBJ_FLAG_HIDDEN);
    lv_obj_clear_flag(this,
                      LV_OBJ_FLAG_GESTURE_BUBBLE | LV_OBJ_FLAG_EVENT_BUBBLE);
    lv_obj_add_event_cb(this, onPressed, LV_EVENT_PRESSED, nullptr);
    lv_obj_add_event_cb(this, onPressing, LV_EVENT_PRESSING, nullptr);
    lv_obj_add_event_cb(this, onReleased, LV_EVENT_RELEASED, nullptr);

    App()->widgetManager()->beforePaint.connect(&widget,
                                                &WidgetWrapper::onBeforePaint);
    AppletKit::instance()->appletActiveChanged.connect(
        &widget, &WidgetWrapper::onAppletActiveChanged);
  }
  ~LVGLWindow();

  void setGeometry(const Rect &rect) {
    lv_obj_set_pos(this, rect.x(), rect.y());
    lv_obj_set_size(this, rect.width(), rect.height());
    widget.setGeometry(rect);
  }

private:
  static void postPressEvent(TouchEvent::State state) {
    lv_indev_t *indev = lv_indev_get_act();
    if (lv_indev_get_type(indev) != LV_INDEV_TYPE_POINTER)
      return;
    lv_point_t p;
    lv_indev_get_point(indev, &p);
    // LogInfo() << state << Point(p.x, p.y) << os::clock_ms();
    App()->postEvent(new TouchEvent(state, Point(p.x, p.y), os::clock_ms()));
  }
  static void onPressed(lv_event_t *e) {
    LVGLWindow *w = static_cast<LVGLWindow *>(lv_event_get_current_target(e));
    if (w->class_p != &lvgx_widget_class) {
      LogWarn() << "w->class_p != &lvgx_widget_class";
      return;
    }
    postPressEvent(TouchEvent::Pressed);
  }
  static void onPressing(lv_event_t *e) { postPressEvent(TouchEvent::Pressed); }
  static void onReleased(lv_event_t *e) {
    postPressEvent(TouchEvent::Released);
  }
};

static void lv_widget_constructor(const lv_obj_class_t *class_p,
                                  lv_obj_t *obj) {
  LV_UNUSED(class_p);
  LV_TRACE_OBJ_CREATE("begin");
  new (static_cast<LVGLWindow *>(obj)) LVGLWindow;
  LV_TRACE_OBJ_CREATE("finished");
}

static void lv_widget_destructor(const lv_obj_class_t *class_p, lv_obj_t *obj) {
  LVGLWindow *w = static_cast<LVGLWindow *>(obj);
  w->~LVGLWindow();
}

static void draw_widget(lv_event_t *e) {
  lv_obj_t *obj = lv_event_get_target(e);
  LVGLWindow *w = static_cast<LVGLWindow *>(obj);
  lv_draw_ctx_t *draw_ctx = lv_event_get_draw_ctx(e);
  lv_draw_rect_dsc_t draw_dsc_sel;
  lv_draw_rect_dsc_init(&draw_dsc_sel);
  draw_dsc_sel.bg_color = lv_color_hex(0xff808080);
  lv_area_t coords;
  lv_obj_get_content_coords(obj, &coords);
  // lv_draw_rect(draw_ctx, &draw_dsc_sel, &coords);
  int buf_w = lv_area_get_width(draw_ctx->buf_area);
  int buf_h = lv_area_get_height(draw_ctx->buf_area);
  VDBPaintDevice buf;
  buf.setBuffer(draw_ctx->buf, buf_w, buf_h);
  Painter p(&buf);
  Point position(coords.x1, coords.y1);
  Rect clip(Point(draw_ctx->clip_area->x1, draw_ctx->clip_area->y1),
            Point(draw_ctx->clip_area->x2, draw_ctx->clip_area->y2));
  Point offset(draw_ctx->buf_area->x1, draw_ctx->buf_area->y1);
  position -= offset;
  clip -= offset;
  w->widget.render(&p, position, clip,
                   Widget::DrawChild | Widget::UpdateShowing);
}

static void lv_widget_event(const lv_obj_class_t *class_p, lv_event_t *e) {
  LV_UNUSED(class_p);

  lv_event_code_t code = lv_event_get_code(e);

  if (code == LV_EVENT_DRAW_MAIN) {
    draw_widget(e);
  }
}

const lv_obj_class_t lvgx_widget_class = {
    .base_class = &lv_obj_class,
    .constructor_cb = lv_widget_constructor,
    .destructor_cb = lv_widget_destructor,
    .event_cb = lv_widget_event,
    .width_def = LV_SIZE_CONTENT,
    .height_def = LV_SIZE_CONTENT,
    .instance_size = sizeof(LVGLWindow),
};

LVGLWindow *gx_lvgl_widget_create(lv_obj_t *parent) {
  LV_LOG_INFO("begin");
  lv_obj_t *obj = lv_obj_class_create_obj(&lvgx_widget_class, parent);
  lv_obj_class_init_obj(obj);
  return static_cast<LVGLWindow *>(obj);
}

static String globalUri(const String &path) { return "prc:///" + path; }

static void setupFont(const String &fontFaces, const String &fontFamily,
                      float fontSize) {
  // load font-faces map
  if (!fontFaces.empty()) {
    String uri = globalUri(fontFaces);
    FontFaceMap &map = App()->fontManager()->faces();
    if (!map.readFile(uri))
      LogError() << "Failed to load global font face mapping table:"
                 << fontFaces;
  }
  // config default font
  if (!fontFamily.empty()) {
    Font font(fontFamily, fontSize);
    if (!font.isNull())
      App()->setFont(font);
  }
}

static struct k_thread thpool1_thread;
static K_KERNEL_STACK_DEFINE(thpool1_stack, 4 * 1024);
static struct k_thread thpool2_thread;
static K_KERNEL_STACK_DEFINE(thpool2_stack, 4 * 1024);

class AsyncProxyObject : public Object {
  EventLoop loop;
  static void __entry(void *p1, void *p2, void *p3) {
    AsyncProxyObject *async_thread_pool = (AsyncProxyObject *)p1;
    while (async_thread_pool->loop.waitEvents(),
           async_thread_pool->loop.processEvents()) {
    }
  }

public:
  AsyncProxyObject() {
    k_thread_create(&thpool1_thread, thpool1_stack,
                    K_THREAD_STACK_SIZEOF(thpool1_stack), __entry, this, NULL,
                    NULL, 12, 0, K_NO_WAIT);
    k_thread_name_set(&thpool1_thread, "gx_async1");
    k_thread_start(&thpool1_thread);

    k_thread_create(&thpool2_thread, thpool2_stack,
                    K_THREAD_STACK_SIZEOF(thpool2_stack), __entry, this, NULL,
                    NULL, 12, 0, K_NO_WAIT);
    k_thread_name_set(&thpool2_thread, "gx_async2");
    k_thread_start(&thpool2_thread);
  }
  ~AsyncProxyObject() override { loop.postEvent(new QuitEvent); }
  bool event(Event *event) override {
    loop.postEvent(event);
    return true;
  }
};

class LvgxPlatform : public PlatformAdapter {
public:
  virtual void processNative(CoreApplication *app, int filter) {
    //TODO 
     os::delay_ms(1);
    // wait_vsync_sem();
    //  lv_task_handler();
  }

  void screenMetrics(PaintDeviceMetric *metric) const override {
    metric->width = CONFIG_GX_SCREEN_WIDTH;
    metric->height = CONFIG_GX_SCREEN_HEIGHT;
    metric->dpi = 326;
    metric->pixelFormat = PixelFormat::RGB565;
#ifdef CONFIG_GX_SCREEN_SHAPE_ROUND
    metric->shape = PaintDevice::Round;
#else
    metric->shape = PaintDevice::Rectange;
#endif
  }
};

struct lvgx : PrimitiveObject {
  Application app;
  LVGLWindow *lvglWindow;
  Widget window;
  JsVM vm;
  AppletKit *kit;
  PersimWear *service;

  lvgx() : app(new LvgxPlatform), lvglWindow() {
    lvgxInstance = this;
    gx_engine_register();
    EnvPath::setEntry(EnvPath::GlobalPRCPackage, "/gui/global.prc");
    EnvPath::packages().push_back(EnvPath::Entry("/gui/apps"));
    app.styleEngine()->setPalette(StyleEngine::Background, Color::Black);
    app.styleEngine()->setPalette(StyleEngine::Text, Color::White);
    app.setCacheCapacity(Application::ImageTextureCache, 4096 * 1024);
    app.setCacheCapacity(Application::FontGlyphCache, 512 * 1024);
    app.setAsyncProxy(new AsyncProxyObject);

    setupFont("font-faces", "sans-serif", 32);

    kit = new AppletKit(&window, "/gui/pkgs.db");
    // set system language and transitions
    kit->setLanguage("zh-CN");
    kit->setOptions(AppletKit::NoLauncherApplet);
    // kit->setDefaultTransition(AppletKit::OpenEnter, "slide");
    // kit->setDefaultTransition(AppletKit::OpenExit, "zoom");
    // kit->setDefaultTransition(AppletKit::CloseEnter, "zoom");
    // kit->setDefaultTransition(AppletKit::CloseExit, "slide");

    service = new PersimWear;
  }
  ~lvgx() { lvgxInstance = nullptr; }
};

LVGLWindow::~LVGLWindow() {
  LogInfo() << "LVGLWindow::~LVGLWindow():" << this;
  if (lvgxInstance->lvglWindow == this) {
    LogInfo() << "LVGLWindow::~LVGLWindow(lvgxInstance->lvglWindow)";
    lvgxInstance->window.setParent(nullptr);
    lvgxInstance->lvglWindow = nullptr;
  }
}

lvgx_t *lvgx_init(void) {
  if (lvgxInstance)
    return lvgxInstance;
  return new lvgx;
}

void lvgx_setup_window(lvgx_t *lvgx, lv_obj_t *parent) {
  lvgx->lvglWindow = gx_lvgl_widget_create(parent);
  lv_area_t coords;
  lv_obj_get_content_coords(parent, &coords);
  Rect rect(0, 0, lv_area_get_width(&coords), lv_area_get_height(&coords));
  lvgx->lvglWindow->setGeometry(rect);
  lvgx->window.setParent(&lvgx->lvglWindow->widget);
  lvgx->window.setRect(rect);
  lvgx->kit->appletsWindow()->setGeometry(rect);
}

int lvgx_launch_applet(const char *applet_name) {
  AppletKit::instance()->launch(applet_name);
  return 0;
}

void lvgx_process_events() {
  if (App()) {
    App()->processEvents(EventLoop::ExcludePaintEvents);
    App()->widgetManager()->beforePaint();
  }
}

bool lvgx_applet_actived(void) {
  if (!AppletKit::instance())
	return false;

  return AppletKit::instance()->activeApplet();
}

struct AppletInfo {
  String packageName;
};

static void event_handler(lv_event_t *event) {
  auto *info = static_cast<AppletInfo *>(event->user_data);
  if (event->code == LV_EVENT_CLICKED) {
    lvgx_launch_applet(info->packageName.c_str());
  } else if (event->code == LV_EVENT_DELETE) {
    delete info;
  }
}

void lvgx_applet_list(lv_obj_t *parent) {
  lv_obj_t *cont = lv_obj_create(parent);
  lv_obj_set_pos(cont, 50, 50);
  lv_obj_set_size(cont, 360, 360);
  lv_obj_set_style_bg_color(cont, lv_color_hex(0x3B3B3B), LV_PART_MAIN);
  lv_obj_center(cont);
  lv_obj_set_flex_flow(cont, LV_FLEX_FLOW_COLUMN_REVERSE);

  PackageDatabase *db = AppletKit::instance()->database(ADBT_Applet);
  Vector<String> applets = db->keys();
  for (const auto &name : applets) {
    lv_obj_t *btn = lv_btn_create(cont);
    lv_obj_add_event_cb(btn, event_handler, LV_EVENT_ALL, new AppletInfo{name});
    lv_obj_set_size(btn, 360, 80);

    // lv_obj_set_style_bg_color(btn, lv_color_hex(0x3B3B3B), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(btn, 255, LV_PART_MAIN);
    lv_obj_set_style_pad_all(btn, 1, LV_PART_MAIN);
    lv_obj_set_style_border_width(btn, 2, LV_PART_MAIN);
    lv_obj_set_style_radius(btn, 20, LV_PART_MAIN);

    String appletName = AppletKit::instance()->queryPackage(name).first;
    lv_obj_t *label = lv_label_create(btn);
    lv_obj_set_style_text_font(label, &lv_default_font, 0); // set ttf font
    lv_label_set_text(label, appletName.c_str());
    lv_obj_center(label);
  }
  lv_obj_invalidate(lv_scr_act());
}
