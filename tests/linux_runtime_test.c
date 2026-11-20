/* Local WebKit/X11 acceptance fixture. Uses only disposable windows and local HTML. */
#include "window.h"
#include "input.h"
#include "chat_view.h"
#include "config.h"
#include "hotkey.h"
#include "raise.h"
#include "theme.h"
#include "xutil.h"
#include "killswitch.h"
#include "lifecycle.h"
#include <gdk/gdkx.h>
#include <X11/extensions/XTest.h>
#include <X11/keysym.h>
#include <jsc/jsc.h>
#include <stdio.h>
#include <string.h>

static int failures;
static Display *dpy;
static void pump(int ms) {
    gint64 end=g_get_monotonic_time()+ms*1000;
    do { while(g_main_context_iteration(NULL,FALSE)); g_usleep(1000); } while(g_get_monotonic_time()<end);
}
static void check(const char *what,bool ok) { printf("%s %s\n",ok?"PASS":"FAIL",what);fflush(stdout);if(!ok)failures++; }
static void key(KeySym sym,bool down) { XTestFakeKeyEvent(dpy,XKeysymToKeycode(dpy,sym),down,CurrentTime);XFlush(dpy);pump(45); }
static void chord(KeySym sym) { key(XK_Alt_L,true);key(sym,true);key(sym,false);key(XK_Alt_L,false);pump(60); }
static bool evaluating;
static char *value;
static void evaluated(GObject *obj,GAsyncResult *result,gpointer data) {
    (void)data;GError *err=NULL;
    JSCValue *v=webkit_web_view_evaluate_javascript_finish(WEBKIT_WEB_VIEW(obj),result,&err);
    g_free(value);value=v?jsc_value_to_string(v):g_strdup(err?err->message:"error");
    if(v)g_object_unref(v);
    g_clear_error(&err);evaluating=false;
}
static const char *eval(WebKitWebView *view,const char *js) {
    evaluating=true;webkit_web_view_evaluate_javascript(view,js,-1,NULL,NULL,NULL,evaluated,NULL);
    for(int n=0;n<500&&evaluating;n++)pump(10);
    return evaluating?"TIMEOUT":value;
}
static GtkWidget *find_class(GtkWidget *w,const char *name) {
    if(gtk_style_context_has_class(gtk_widget_get_style_context(w),name))return w;
    if(GTK_IS_CONTAINER(w)) {
        GList *list=gtk_container_get_children(GTK_CONTAINER(w));GtkWidget *found=NULL;
        for(GList *it=list;it&&!found;it=it->next)found=find_class(it->data,name);
        g_list_free(list);return found;
    }
    return NULL;
}
static int count_class(GtkWidget *w,const char *name) {
    int n=gtk_style_context_has_class(gtk_widget_get_style_context(w),name)?1:0;
    if(GTK_IS_CONTAINER(w)){GList *list=gtk_container_get_children(GTK_CONTAINER(w));for(GList *it=list;it;it=it->next)n+=count_class(it->data,name);g_list_free(list);}return n;
}
static gboolean debug_entry(GtkWidget *w,GdkEventKey *e,gpointer data){(void)w;(void)data;printf("ENTRY KEY %s state %u\n",gdk_keyval_name(e->keyval),e->state);return FALSE;}
static Window focus(void){Window w;int revert;XGetInputFocus(dpy,&w,&revert);return w;}
int main(int argc,char **argv) {
    XInitThreads();gtk_init(&argc,&argv);
    if(argc==3 && !strcmp(argv[1],"--clipboard-matches")) {
        GdkPixbuf *got=gtk_clipboard_wait_for_image(gtk_clipboard_get(GDK_SELECTION_CLIPBOARD));
        GdkPixbuf *expected=gdk_pixbuf_new_from_file(argv[2],NULL);
        bool match=got&&expected&&gdk_pixbuf_get_width(got)==gdk_pixbuf_get_width(expected)&&gdk_pixbuf_get_height(got)==gdk_pixbuf_get_height(expected);
        if(match)for(int y=0;y<gdk_pixbuf_get_height(got);y++)for(int x=0;x<gdk_pixbuf_get_width(got);x++) {
            guchar *a=gdk_pixbuf_get_pixels(got)+y*gdk_pixbuf_get_rowstride(got)+x*gdk_pixbuf_get_n_channels(got);
            guchar *b=gdk_pixbuf_get_pixels(expected)+y*gdk_pixbuf_get_rowstride(expected)+x*gdk_pixbuf_get_n_channels(expected);
            if(memcmp(a,b,3))match=false;
        }
        if(got)g_object_unref(got);
        if(expected)g_object_unref(expected);
        return match?0:1;
    }
    xutil_install_error_handlers();config_init();input_init();
    chat_view_init();raise_init();window_init(TRUE);check("hotkey initialization",hotkey_init());
    lifecycle_init();
    dpy=XOpenDisplay(NULL);
    GtkWidget *external=gtk_window_new(GTK_WINDOW_TOPLEVEL),*entry=gtk_entry_new();
    gtk_window_set_default_size(GTK_WINDOW(external),400,180);gtk_window_move(GTK_WINDOW(external),650,100);
    g_signal_connect(entry,"key-press-event",G_CALLBACK(debug_entry),NULL);
    gtk_container_add(GTK_CONTAINER(external),entry);gtk_widget_show_all(external);pump(100);
    Window xid=gdk_x11_window_get_xid(gtk_widget_get_window(external));
    gtk_window_set_focus(GTK_WINDOW(external),entry);XSetInputFocus(dpy,xid,RevertToParent,CurrentTime);XSync(dpy,False);pump(100);
    GdkEvent *external_focus=gdk_event_new(GDK_FOCUS_CHANGE);
    external_focus->focus_change.window=g_object_ref(gtk_widget_get_window(external));
    external_focus->focus_change.in=TRUE;
    gtk_widget_event(external,external_focus);gdk_event_free(external_focus);pump(100);
    Window original=focus();
    XRaiseWindow(dpy,xid);XSync(dpy,False);pump(250);
    Window root_return,parent_return,*children=NULL;unsigned count=0;
    XQueryTree(dpy,DefaultRootWindow(dpy),&root_return,&parent_return,&children,&count);
    check("overlay reasserts topmost without activation",count && children[count-1]==overlay_get_xid() && focus()==original);
    if(children)XFree(children);
    check("normal startup",input_get_typing_target()==TYPING_TARGET_NONE);
    key(XK_n,true);key(XK_n,false);
    printf("NORMAL observed: [%s]\n",gtk_entry_get_text(GTK_ENTRY(entry)));
    check("normal typing reaches external app",!strcmp(gtk_entry_get_text(GTK_ENTRY(entry)),"n"));
    check("error page replaces launcher",find_class(overlay_get_window_widget(),"error-title") && count_class(overlay_get_window_widget(),"app-button")==0);
    check("only status dots remain in header",!find_class(overlay_get_window_widget(),"theme-btn") && !find_class(overlay_get_window_widget(),"top-ctrl-btn"));
    overlay_show_toast("Fullscreen mode");
    check("status actions create no notification bubble",!find_class(overlay_get_window_widget(),"toast-box"));
    check("four footer actions retained",count_class(overlay_get_window_widget(),"nav-button")==4);
    check("four footer click-zone markers",count_class(overlay_get_window_widget(),"nav-target-dot")==4);
    GtkWidget *recovery=find_class(overlay_get_window_widget(),"recovery-word");
    int ex=0,ey=0,rx=0,ry=0;
    gdk_window_get_origin(gtk_widget_get_window(overlay_get_window_widget()),&ex,&ey);
    gtk_widget_translate_coordinates(recovery,overlay_get_window_widget(),0,0,&rx,&ry);
    XTestFakeMotionEvent(dpy,-1,ex+rx+12,ey+ry+5,CurrentTime);XFlush(dpy);pump(60);
    XTestFakeButtonEvent(dpy,1,True,CurrentTime);XTestFakeButtonEvent(dpy,1,False,CurrentTime);XFlush(dpy);pump(80);
    check("single recovery click keeps error page",!strcmp(overlay_get_current_app(),"home"));
    pump(500);
    for(int click=0;click<2;++click){XTestFakeButtonEvent(dpy,1,True,CurrentTime);XTestFakeButtonEvent(dpy,1,False,CurrentTime);XFlush(dpy);pump(70);}
    check("double recovery click opens ChatGPT",!strcmp(overlay_get_current_app(),"chatgpt"));
    check("recovery preserves desktop focus",focus()==original);
    overlay_switch_app("home");pump(200);
    for(int small=0;small<2;++small) {
        overlay_set_size(small?260:360,small?180:280);pump(200);
        int w,h;gtk_window_get_size(GTK_WINDOW(overlay_get_window_widget()),&w,&h);
        check("error page fits requested size",w==(small?260:360)&&h==(small?180:280));
        GdkPixbuf *preview=gdk_pixbuf_get_from_window(gtk_widget_get_window(overlay_get_window_widget()),0,0,w,h);
        if(preview){char *path=g_build_filename(g_get_user_data_dir(),small?"error-home-small.png":"error-home-dark.png",NULL);gdk_pixbuf_save(preview,path,"png",NULL,NULL);g_free(path);g_object_unref(preview);}
    }
    overlay_set_size(360,280);pump(100);
    check("no old feature pages",!find_class(overlay_get_window_widget(),"ghost-page") && !find_class(overlay_get_window_widget(),"url-entry"));
    overlay_switch_app("chatgpt");
    WebKitWebView *view=chat_view_get_chatgpt_view();
    webkit_web_view_load_html(view,"<html><head><title>Local fixture</title></head><body><header>Header</header><aside>Sidebar</aside><article><p>Answer</p></article><textarea id='prompt-textarea'></textarea><button data-testid='send-button'>Send</button></body></html>","https://chatgpt.com/");
    for(int n=0;n<1000 && (!webkit_web_view_get_title(view)||strcmp(webkit_web_view_get_title(view),"Local fixture"));n++)pump(10);
    check("local ChatGPT fixture loads",webkit_web_view_get_title(view)&&!strcmp(webkit_web_view_get_title(view),"Local fixture"));
    gtk_entry_set_text(GTK_ENTRY(entry),"");
    const char *point=eval(view,"(()=>{const r=document.getElementById('prompt-textarea').getBoundingClientRect();return [Math.floor(r.x+r.width/2),Math.floor(r.y+r.height/2)].join(',')})()");
    int hx=0,hy=0;sscanf(point,"%d,%d",&hx,&hy);
    pump(300);
    int px,py;gdk_window_get_origin(gtk_widget_get_window(GTK_WIDGET(view)),&px,&py);
    int vx=0,vy=0;
    XTestFakeMotionEvent(dpy,-1,700,130,CurrentTime);XFlush(dpy);pump(60);
    XTestFakeMotionEvent(dpy,-1,px+vx+hx,py+vy+hy,CurrentTime);XFlush(dpy);
    for(int n=0;n<100&&input_get_typing_target()!=TYPING_TARGET_OVERLAY;n++)pump(10);
    check("hover composer automatically enables typing",input_get_typing_target()==TYPING_TARGET_OVERLAY);
    check("hover activation turns ball green",find_class(overlay_get_window_widget(),"pill-overlay")!=NULL);
    check("hover activation preserves OS focus",focus()==original);
    pump(120);key(XK_h,true);key(XK_h,false);
    printf("HOVER observed: [%s] external: [%s]\n",eval(view,"document.getElementById('prompt-textarea').value"),gtk_entry_get_text(GTK_ENTRY(entry)));
    check("hover-enabled typing reaches composer only",!strcmp(eval(view,"document.getElementById('prompt-textarea').value"),"h")&&!strcmp(gtk_entry_get_text(GTK_ENTRY(entry)),""));
    input_release_grab();
    XTestFakeMotionEvent(dpy,-1,700,130,CurrentTime);XFlush(dpy);pump(60);
    eval(view,"document.getElementById('prompt-textarea').value=''");
    for(int kind=0;kind<2;kind++) {
        eval(view,kind?"document.getElementById('prompt-textarea').disabled=true":"document.getElementById('prompt-textarea').readOnly=true");
        XTestFakeMotionEvent(dpy,-1,700,130,CurrentTime);XFlush(dpy);pump(60);
        XTestFakeMotionEvent(dpy,-1,px+hx,py+hy,CurrentTime);XFlush(dpy);pump(200);
        check(kind?"disabled field does not auto-enable":"read-only field does not auto-enable",input_get_typing_target()==TYPING_TARGET_NONE);
        eval(view,"document.getElementById('prompt-textarea').disabled=false;document.getElementById('prompt-textarea').readOnly=false");
    }
    for(int kind=0;kind<2;kind++) {
        eval(view,kind?"document.body.insertAdjacentHTML('beforeend',`<div id='hover-extra' contenteditable='true' style='position:fixed;left:200px;top:100px;width:100px;height:40px'>Editable</div>`);":"document.body.insertAdjacentHTML('beforeend',`<input id='hover-extra' type='email' style='position:fixed;left:200px;top:100px;width:100px;height:40px'>`);");
        XTestFakeMotionEvent(dpy,-1,700,130,CurrentTime);XFlush(dpy);pump(60);
        XTestFakeMotionEvent(dpy,-1,px+220,py+120,CurrentTime);XFlush(dpy);pump(250);
        check(kind?"contenteditable hover enables typing":"input hover enables typing",input_get_typing_target()==TYPING_TARGET_OVERLAY);
        check("hover focuses the specific field",!strcmp(eval(view,"document.activeElement.id"),"hover-extra"));
        check("all hover targets preserve OS focus",focus()==original);
        input_release_grab();eval(view,"document.getElementById('hover-extra').remove()");
    }
    XTestFakeMotionEvent(dpy,-1,700,130,CurrentTime);XFlush(dpy);pump(80);
    gtk_entry_set_text(GTK_ENTRY(entry),"");chord(XK_m);
    check("Alt+M enables overlay capture",input_get_typing_target()==TYPING_TARGET_OVERLAY && input_get_grab_status()==GRAB_STATUS_ACTIVE);
    check("enable preserves X11 focus",focus()==original);
    check("green status ball",find_class(overlay_get_window_widget(),"pill-overlay")!=NULL);
    check("composer selected",!strcmp(eval(view,"document.activeElement.id"),"prompt-textarea"));
    /* Status properties must change on mode transitions, not on each key. */
    XSelectInput(dpy,overlay_get_xid(),PropertyChangeMask);
    XSync(dpy,False);
    XEvent status_event;
    while(XCheckTypedWindowEvent(dpy,overlay_get_xid(),PropertyNotify,&status_event));
    key(XK_a,true);key(XK_a,false);key(XK_Shift_L,true);key(XK_b,true);key(XK_b,false);key(XK_Shift_L,false);
    XSync(dpy,False);
    int status_writes=0;
    Atom status_atom=XInternAtom(dpy,"_CRONQ_STATE",False);
    while(XCheckTypedWindowEvent(dpy,overlay_get_xid(),PropertyNotify,&status_event))
        if(status_event.xproperty.atom==status_atom)status_writes++;
    check("typing avoids redundant X11 status writes",status_writes==0);
    check("overlay receives exact typed text",!strcmp(eval(view,"document.getElementById('prompt-textarea').value"),"aB"));
    check("no mirrored text",!strcmp(gtk_entry_get_text(GTK_ENTRY(entry)),""));
    check("typing preserves X11 focus",focus()==original);
    chord(XK_m);key(XK_z,true);key(XK_z,false);
    check("second Alt+M restores normal typing",input_get_typing_target()==TYPING_TARGET_NONE && !strcmp(gtk_entry_get_text(GTK_ENTRY(entry)),"z"));
    check("normal typing not duplicated to overlay",!strcmp(eval(view,"document.getElementById('prompt-textarea').value"),"aB"));
    check("gray status ball",find_class(overlay_get_window_widget(),"pill-normal")!=NULL);
    for(int mode=0;mode<4;mode++) {
        theme_set_mode((ThemeMode)mode);pump(50);
        const char *expected=(mode==1||mode==2)?"rgb(0, 0, 0)":"rgb(255, 255, 255)";
        check("ChatGPT palette follows appearance",!strcmp(eval(view,"getComputedStyle(document.querySelector('article p')).color"),expected));
        check("composer remains visible",!strcmp(eval(view,"getComputedStyle(document.getElementById('prompt-textarea')).display!=='none'"),"true"));
        GtkWidget *mode_widget=gtk_widget_get_parent(find_class(overlay_get_window_widget(),"pill-normal"));
        check("indicator visible in all appearances",gtk_widget_is_visible(mode_widget));
        check("theme preserves foreground focus",focus()==original);
    }
    theme_set_mode(THEME_DARK);chord(XK_m);
    GtkClipboard *clipboard=gtk_clipboard_get(GDK_SELECTION_CLIPBOARD);
    gtk_clipboard_set_text(clipboard,"clipboard text",-1);
    overlay_execute_clipboard_action(CLIPBOARD_ACTION_SELECT_ALL);
    overlay_execute_clipboard_action(CLIPBOARD_ACTION_PASTE);pump(250);
    check("text clipboard paste targets ChatGPT",!strcmp(eval(view,"document.getElementById('prompt-textarea').value"),"clipboard text"));
    check("clipboard paste never goes to external app",!strcmp(gtk_entry_get_text(GTK_ENTRY(entry)),"z"));
    eval(view,"window.imagePaste='';document.addEventListener('paste',e=>{if(e.clipboardData.files.length)window.imagePaste=e.clipboardData.files[0].type;})");
    GdkPixbuf *pixel=gdk_pixbuf_new(GDK_COLORSPACE_RGB,FALSE,8,2,2);gdk_pixbuf_fill(pixel,0x55aaffff);
    gtk_clipboard_set_image(clipboard,pixel);g_object_unref(pixel);
    overlay_execute_clipboard_action(CLIPBOARD_ACTION_PASTE);pump(150);
    check("image clipboard paste reaches ChatGPT",!strcmp(eval(view,"window.imagePaste"),"image/png"));
    check("clipboard operations preserve OS focus",focus()==original);
    GdkWindow *overlay_window=gtk_widget_get_window(overlay_get_window_widget());
    int ox,oy,ow,oh;overlay_get_geometry(&ox,&oy,&ow,&oh);
    GdkPixbuf *shot=gdk_pixbuf_get_from_window(overlay_window,0,0,ow,oh);
    if(shot){char *path=g_build_filename(g_get_user_data_dir(),"typing-overlay.png",NULL);gdk_pixbuf_save(shot,path,"png",NULL,NULL);g_free(path);g_object_unref(shot);}
    GDBusConnection *bus=g_bus_get_sync(G_BUS_TYPE_SESSION,NULL,NULL);
    if(bus){g_dbus_connection_emit_signal(bus,NULL,"/ScreenSaver","org.freedesktop.ScreenSaver","ActiveChanged",g_variant_new("(b)",TRUE),NULL);g_dbus_connection_flush_sync(bus,NULL,NULL);pump(100);}
    check("screen lock signal releases routing",input_get_typing_target()==TYPING_TARGET_NONE);
    g_clear_object(&bus);
    chord(XK_m);
    XTestFakeMotionEvent(dpy,-1,700,130,CurrentTime);XTestFakeButtonEvent(dpy,1,True,CurrentTime);XTestFakeButtonEvent(dpy,1,False,CurrentTime);XFlush(dpy);pump(100);
    check("outside click returns normal typing",input_get_typing_target()==TYPING_TARGET_NONE);
    check("outside click preserves external focus",focus()==original);
    chord(XK_m);chord(XK_x);pump(50);
    check("hide preserves typing target",!overlay_is_visible() && input_get_typing_target()==TYPING_TARGET_OVERLAY);
    XWindowAttributes hidden_attrs;XGetWindowAttributes(dpy,overlay_get_xid(),&hidden_attrs);
    check("hidden surface is unmapped",hidden_attrs.map_state==IsUnmapped);
    eval(view,"document.getElementById('prompt-textarea').value=''");
    key(XK_h,true);key(XK_h,false);
    check("hidden overlay receives text only",!strcmp(eval(view,"document.getElementById('prompt-textarea').value"),"h") && !strcmp(gtk_entry_get_text(GTK_ENTRY(entry)),"z"));
    check("hidden typing preserves OS focus",focus()==original);
    /* Click through the hidden overlay's former rectangle, not only outside it. */
    XTestFakeMotionEvent(dpy,-1,px+hx,py+hy,CurrentTime);XTestFakeButtonEvent(dpy,1,True,CurrentTime);XTestFakeButtonEvent(dpy,1,False,CurrentTime);XFlush(dpy);pump(100);
    check("click under hidden overlay releases typing",input_get_typing_target()==TYPING_TARGET_NONE);
    input_set_typing_target(TYPING_TARGET_OVERLAY);check("hidden overlay cannot start a new capture",input_get_typing_target()==TYPING_TARGET_NONE);
    overlay_show();chord(XK_m);chord(XK_x);chord(XK_m);
    check("Alt+M can release hidden typing",input_get_typing_target()==TYPING_TARGET_NONE);
    overlay_show();
    overlay_set_opacity(0.80);input_set_ghost_mode(GHOST_MODE_ON);
    check("ghost starts at 50 percent",overlay_get_opacity()==0.50);
    overlay_set_opacity(0.60);input_set_ghost_mode(GHOST_MODE_ON);
    check("already enabled ghost preserves manual opacity",overlay_get_opacity()==0.60);
    input_set_ghost_mode(GHOST_MODE_OFF);
    check("leaving ghost restores 100 percent",overlay_get_opacity()==1.0);
    input_set_ghost_mode(GHOST_MODE_ON);
    check("reentering ghost resets to 50 percent",overlay_get_opacity()==0.50);
    input_set_ghost_mode(GHOST_MODE_OFF);
    for(int prompt=0;prompt<3;prompt++) {
        if(prompt==1)overlay_hide();else overlay_show();
        pump(320);key(XK_Alt_L,true);key(XK_Shift_L,true);key(XK_k,true);
        check("kill shortcut arms confirmation",killswitch_prompt_active());
        /* Cancel before releasing the chord, then test ordinary Escape too. */
        if(prompt!=2){key(XK_k,false);key(XK_Shift_L,false);key(XK_Alt_L,false);}
        key(XK_Escape,true);key(XK_Escape,false);
        check("Escape dismisses kill confirmation",!killswitch_prompt_active());
        if(prompt==2){key(XK_k,false);key(XK_Shift_L,false);key(XK_Alt_L,false);}
    }
    overlay_show();hotkey_set_locked(true);pump(100);chord(XK_m);
    check("lock disables typing toggle",hotkey_is_locked()&&!overlay_is_visible()&&input_get_typing_target()==TYPING_TARGET_NONE);
    pump(260);key(XK_Alt_L,true);key(XK_k,true);key(XK_x,true);key(XK_x,false);key(XK_k,false);key(XK_Alt_L,false);pump(80);
    check("unlock chord restores overlay",!hotkey_is_locked()&&overlay_is_visible());
    input_release_grab();
    Display *rival=XOpenDisplay(NULL);int grabbed=XGrabKeyboard(rival,DefaultRootWindow(rival),False,GrabModeAsync,GrabModeAsync,CurrentTime);XSync(rival,False);
    check("conflicting grab fixture",grabbed==GrabSuccess);
    input_set_typing_target(TYPING_TARGET_OVERLAY);
    check("grab conflict never falls back to mirroring",input_get_typing_target()==TYPING_TARGET_NONE&&input_get_grab_status()==GRAB_STATUS_FAILED);
    XUngrabKeyboard(rival,CurrentTime);XSync(rival,False);XCloseDisplay(rival);
    overlay_universal_reset();check("reset Home/normal/solid",!strcmp(overlay_get_current_app(),"home")&&input_get_typing_target()==TYPING_TARGET_NONE&&input_get_ghost_mode()==GHOST_MODE_OFF);
    check("all operations preserve original focus",focus()==original);
    overlay_switch_app("chatgpt");
    char *recovery_file=g_build_filename(g_get_user_data_dir(),"recovery.html",NULL);
    g_file_set_contents(recovery_file,"<title>Recovery fixture</title><textarea id='prompt-textarea'></textarea>",-1,NULL);
    char *recovery_uri=g_filename_to_uri(recovery_file,NULL,NULL);
    webkit_web_view_load_uri(view,recovery_uri);
    for(int n=0;n<500 && g_strcmp0(webkit_web_view_get_title(view),"Recovery fixture");n++)pump(10);
    check("recovery fixture loads",!g_strcmp0(webkit_web_view_get_title(view),"Recovery fixture"));
    webkit_web_view_terminate_web_process(view);pump(200);
    for(int n=0;n<500 && (webkit_web_view_is_loading(view)||g_strcmp0(webkit_web_view_get_title(view),"Recovery fixture"));n++)pump(10);
    pump(2000);
    const char *recovered=eval(view,"document.title");
    check("WebKit process recovers with backoff",!strcmp(recovered,"Recovery fixture"));
    if(strcmp(recovered,"Recovery fixture"))fprintf(stderr,"Recovery actual title: %s uri: %s\n",recovered,webkit_web_view_get_uri(view));
    g_free(recovery_file);g_free(recovery_uri);
    lifecycle_cleanup();input_cleanup();hotkey_cleanup();raise_cleanup();chat_view_cleanup();window_cleanup();gtk_widget_destroy(external);XCloseDisplay(dpy);config_cleanup();g_free(value);
    printf("linux_runtime_test: %d failure(s)\n",failures);return failures?1:0;
}
