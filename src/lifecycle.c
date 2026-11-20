#define _GNU_SOURCE
#include "lifecycle.h"
#include "input.h"
#include "hotkey.h"
#include "xutil.h"
#include <gio/gio.h>
#include <glib/gstdio.h>
#include <fcntl.h>
#include <signal.h>
#include <unistd.h>
#include <stdint.h>
#include <ucontext.h>
#include <time.h>
#include <string.h>

static GDBusConnection *s_system, *s_session;
static guint s_sleep, s_screen, s_gnome;
static char s_report[1024];

static void system_event(GDBusConnection *connection,const gchar *sender,const gchar *path,
                        const gchar *interface,const gchar *signal,GVariant *params,gpointer data) {
    (void)connection;(void)sender;(void)path;(void)interface;(void)data;
    gboolean active=FALSE;
    if(g_variant_is_of_type(params,G_VARIANT_TYPE("(b)"))) g_variant_get(params,"(b)",&active);
    if(active) input_release_grab();
    else if(!strcmp(signal,"PrepareForSleep")) hotkey_reload_grabs();
}
void lifecycle_init(void) {
    /* login1 suspend notification plus the freedesktop/GNOME screen lock signals. */
    s_system=g_bus_get_sync(G_BUS_TYPE_SYSTEM,NULL,NULL);
    if(s_system) s_sleep=g_dbus_connection_signal_subscribe(s_system,"org.freedesktop.login1",
        "org.freedesktop.login1.Manager","PrepareForSleep","/org/freedesktop/login1",NULL,
        G_DBUS_SIGNAL_FLAGS_NONE,system_event,NULL,NULL);
    s_session=g_bus_get_sync(G_BUS_TYPE_SESSION,NULL,NULL);
    if(s_session) {
        s_screen=g_dbus_connection_signal_subscribe(s_session,NULL,"org.freedesktop.ScreenSaver",
            "ActiveChanged",NULL,NULL,G_DBUS_SIGNAL_FLAGS_NONE,system_event,NULL,NULL);
        s_gnome=g_dbus_connection_signal_subscribe(s_session,NULL,"org.gnome.ScreenSaver",
            "ActiveChanged",NULL,NULL,G_DBUS_SIGNAL_FLAGS_NONE,system_event,NULL,NULL);
    }
}
void lifecycle_cleanup(void) {
    if(s_system) { if(s_sleep)g_dbus_connection_signal_unsubscribe(s_system,s_sleep);g_clear_object(&s_system); }
    if(s_session) {
        if(s_screen)g_dbus_connection_signal_unsubscribe(s_session,s_screen);
        if(s_gnome)g_dbus_connection_signal_unsubscribe(s_session,s_gnome);
        g_clear_object(&s_session);
    }
    s_sleep=s_screen=s_gnome=0;
}
static size_t append_hex(char *out,uintptr_t value) {
    char digits[2*sizeof(value)];size_t n=0;
    do {digits[n++]="0123456789abcdef"[value&15];value>>=4;}while(value);
    for(size_t i=0;i<n;i++)out[i]=digits[n-i-1];
    return n;
}
static void report_signal(int sig,siginfo_t *info,void *context) {
    /* Only async-signal-safe syscalls here; no GTK, allocation, locks or stdio. */
    char message[192]="Cronq native crash\nsignal=0x";size_t n=sizeof("Cronq native crash\nsignal=0x")-1;
    n+=append_hex(message+n,(uintptr_t)sig);
    const char address[]="\nfault_address=0x";memcpy(message+n,address,sizeof(address)-1);n+=sizeof(address)-1;
    n+=append_hex(message+n,(uintptr_t)info->si_addr);
    uintptr_t pc=0;
#if defined(__x86_64__)
    pc=(uintptr_t)((ucontext_t *)context)->uc_mcontext.gregs[REG_RIP];
#elif defined(__aarch64__)
    pc=(uintptr_t)((ucontext_t *)context)->uc_mcontext.pc;
#else
    (void)context;
#endif
    const char instruction[]="\ninstruction=0x";memcpy(message+n,instruction,sizeof(instruction)-1);n+=sizeof(instruction)-1;
    n+=append_hex(message+n,pc);message[n++]='\n';
    int fd=open(s_report,O_WRONLY|O_CREAT|O_EXCL|O_CLOEXEC,0600);
    if(fd>=0){ssize_t ignored=write(fd,message,n);(void)ignored;close(fd);}
    _exit(128+sig);
}
static gint compare_names(gconstpointer a,gconstpointer b) { return g_strcmp0(*(char * const *)a,*(char * const *)b); }
void crash_report_init(void) {
    char *dir=g_build_filename(g_get_user_data_dir(),"cronq",NULL);
    if(g_mkdir_with_parents(dir,0700)!=0){g_free(dir);return;}
    GPtrArray *reports=g_ptr_array_new_with_free_func(g_free);
    GDir *stream=g_dir_open(dir,0,NULL);
    if(stream){const char *name;while((name=g_dir_read_name(stream))) {
        if(g_str_has_prefix(name,"crash-")&&g_str_has_suffix(name,".txt"))g_ptr_array_add(reports,g_strdup(name));
        }
        g_dir_close(stream);
    }
    g_ptr_array_sort(reports,compare_names);
    for(guint i=0;i+2<reports->len;i++){char *file=g_build_filename(dir,g_ptr_array_index(reports,i),NULL);g_unlink(file);g_free(file);}
    g_ptr_array_free(reports,TRUE);
    g_snprintf(s_report,sizeof(s_report),"%s/crash-%020lld-%d.txt",dir,(long long)g_get_real_time(),(int)getpid());g_free(dir);
    struct sigaction action={0};action.sa_sigaction=report_signal;action.sa_flags=SA_SIGINFO|SA_RESETHAND;
    sigemptyset(&action.sa_mask);
    const int signals[]={SIGSEGV,SIGABRT,SIGBUS,SIGILL,SIGFPE};
    for(size_t i=0;i<G_N_ELEMENTS(signals);i++)sigaction(signals[i],&action,NULL);
}
