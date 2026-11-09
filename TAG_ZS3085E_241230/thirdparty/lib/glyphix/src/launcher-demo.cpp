#include "gx_appletkit.h"
#include "gx_application.h"
#include "gx_environment.h"
#include "gx_eventdispatcher.h"
#include "gx_fontfacemap.h"
#include "gx_fontmanager.h"
#include "gx_jsvm.h"
#include "gx_logger.h"
#include "gx_paintdevice.h"
#include "gx_painter.h"
#include "gx_touchevent.h"
#include "gx_vectorpath.h"
#include "gx_styleengine.h"
#include "gx_widget.h"
#include "gx_persimwear.h"
#include "platform/gx_time.h"
#include "gx_zephyrlcddriver.h"
#include "gx_zephyrplatform.h"
#include "kernel.h"
#ifdef CONFIG_VG_LITE
#include "gx_vglite_register.h"
#endif
#include "gx_jsvm.h"

using namespace gx;

static JsValue storageSet(const JsCallContext &ctx) {
    if (ctx.argc() > 2) {
        ctx.thisObject()["storage"].setProperty(ctx.arg(0), ctx.arg(1));
    }
    return JsValue();
}

static JsValue storageGet(const JsCallContext &ctx) {
    if (ctx.argc() > 2) {
        return ctx.thisObject()["storage"][ctx.arg(0)];
    }
    return JsValue();
}

static JsValue storageWatch(const JsCallContext &ctx) {
    return JsValue();
}

static JsValue storageModuleInit(JsVM *vm) {
    JsValue m = vm->newObject();
    m.setProperty("storage", vm->newObject());
    m.setProperty("set", storageSet);
    m.setProperty("get", storageGet);
    m.setProperty("watch", storageWatch);
    return m;
}
GX_JSVM_MODULE(system_storage, "system.storage", storageModuleInit)

extern "C" {
void gx_touch_post(int x, int y, int pressed) {

    App()->postEvent(new TouchEvent(pressed ? TouchEvent::Pressed : TouchEvent::Released, {x, y},
                                    os::clock_ms()));
}

void gx_set_lcd_framebuffer(void *ptr) {
    static_cast<LCDDevice *>(App()->screenDevice())->setFramebuffer(static_cast<uint8_t *>(ptr));
}

const char *printVersion(void) {
    return CoreApplication::versionString().c_str();
}

}

// glyphix-emu --config pkgs.db --target com.example.app --width 460 --height 552 --dpi 326
// --font-size 28.0 --font-map font-faces --font-family sans-serif --surface surface/surface.json

static void glyphix_entry(void *prma1, void *pram2, void *prma3) {
    Application app(new PersimPlatform);
#ifdef CONFIG_VG_LITE
    gx_engine_register();
#endif

    EnvPath::setEntry(EnvPath::GlobalPRCPackage, "/gui/global.prc");
    EnvPath::setEntry(EnvPath::AppletFiles, "/gui/data/files");
    EnvPath::packages().push_back(EnvPath::Entry("/gui/apps"));
    FontFaceMap &map = App()->fontManager()->faces();
    if (!map.readFile("prc:///font-faces"))
        LogError() << "Failed to load global font face mapping table.";
    Font font("sans-serif", 28.0f);
    if (!font.isNull())
        App()->setFont(font);

    app.styleEngine()->setPalette(StyleEngine::Background, Color::Black);
    app.styleEngine()->setPalette(StyleEngine::Text, Color::White);

    JsVM vm;
    Widget window;
    AppletKit kit(&window, "/gui/pkgs.db");
    PersimWear services;

    kit.setLanguage("zh-CN");
    kit.setDefaultTransition(AppletKit::OpenEnter, "slide");
    kit.setDefaultTransition(AppletKit::OpenExit, "zoom");
    kit.setDefaultTransition(AppletKit::CloseEnter, "zoom");
    kit.setDefaultTransition(AppletKit::CloseExit, "slide");

    // const char *target = "com.realthread.Weather";
    const char *target = "launcher.app";
    if (!kit.launch(target))
        LogError() << "Application" << target << "launch failed!";
    int code = app.exec();
    LogInfo() << "Glyphix exit with code:" << code;
}

static struct k_thread gui_thread;
static K_KERNEL_STACK_DEFINE(thread_stack, 16*1024);

extern "C" int glyphix_start() {
    k_thread_create(&gui_thread, thread_stack, K_THREAD_STACK_SIZEOF(thread_stack), glyphix_entry, NULL, NULL, NULL, 12, 0, K_NO_WAIT);
    k_thread_name_set(&gui_thread, "glyphix");
    k_thread_start(&gui_thread);
    return 0;
}
