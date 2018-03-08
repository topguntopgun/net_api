/*
 * Virtual terminal [aka TeletYpe] interface routine.
 * Copyright (C) 1997, 98 Kunihiro Ishiguro
 *
 * This file is part of GNU Zebra.
 *
 * GNU Zebra is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the
 * Free Software Foundation; either version 2, or (at your option) any
 * later version.
 *
 * GNU Zebra is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with GNU Zebra; see the file COPYING.  If not, write to the Free
 * Software Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA
 * 02111-1307, USA.
 */

#include <zebra.h>

#include <sys/un.h>
#include <setjmp.h>
#include <sys/wait.h>
#include <sys/resource.h>
#include <sys/stat.h>
#include <pthread.h>

#include "linklist.h"
#include "thread.h"
#include "buffer.h"
//#include <version.h>
#include "command.h"
#include "sockunion.h"
#include "memory.h"
#include "str.h"
#include "log.h"
#include "prefix.h"
#include "filter.h"
#include "vty.h"
#include "privs.h"
#include "network.h"
#include <global.h>

#include <arpa/telnet.h>

#include <vtysh.h>

#include "readline/history.h"
#include "vty_user.h"
#include "log.h"
//#include "tfNvramParam.h"
#include "vtyCommon.h"
#include "cdtSysCtrlPub.h"
#include "ipc_if.h"

static void vty_event (enum event, int, struct vty *);

/* Extern host structure from command.c */
extern struct host host;

/* Vector which store each vty structure. */
static vector vtyvec;

static pthread_mutex_t vty_mutex = PTHREAD_MUTEX_INITIALIZER;

/* Vty timeout value. */
static unsigned long vty_timeout_val = VTY_TIMEOUT_DEFAULT;

/* Vty access-class command */
static char *vty_accesslist_name = NULL;

/* Vty access-calss for IPv6. */
static char *vty_ipv6_accesslist_name = NULL;

/* VTY server thread. */
static vector Vvty_serv_thread;

/* Current directory. */
char *vty_cwd = NULL;

/* Configure lock. */
static int vty_config;

/* Login password check. */
static int no_password_check = 0;

/* Restrict unauthenticated logins? */
static const u_char restricted_mode_default = 0;
static u_char restricted_mode = 0;

/* Integrated configuration file path */
char integrate_default[] = SYSCONFDIR INTEGRATE_DEFAULT_CONFIG;

/* Ineractive with user ,stephen.liu 20151028 */
vty_iactive g_active;

static vtysh_client_info *alarm_info = NULL;
static int alarm_info_refs = 0;
#ifdef VTY_EVENT_ALARM
static vector msgbufvec = NULL;
#endif

/*
 * The indices into log_buf are not constrained to log_buf_len - they
 * must be masked before subscripting
 */
#ifdef LOG_BUF_USED
static unsigned log_start;	/* Index into log_buf: next char to be read by syslog() */
static unsigned log_start_prev;
static unsigned log_end;	/* Index into log_buf: most-recently-written-char + 1 */
#define __LOG_BUF_LEN (1 << 20)
static char __log_buf[__LOG_BUF_LEN];
static char *log_buf = __log_buf;
static int log_buf_len = __LOG_BUF_LEN;
static pthread_mutex_t log_buf_mutex = PTHREAD_MUTEX_INITIALIZER;
static pthread_mutex_t log_mutex = PTHREAD_MUTEX_INITIALIZER;
#define LOG_BUF_MASK (log_buf_len-1)
#define LOG_BUF(idx) (log_buf[(idx) & LOG_BUF_MASK])
static char log_print_en = TRUE;
#endif

#ifdef VTYSH_DEBUG
int g_vtysh_init_complete = FALSE;
#endif

int g_vtysh_flag = 0;

#define VTY_PRINT 
/* VTY standard output function. */
int
vty_out (struct vty *vty, const char *format, ...)
{
    va_list args;
    int len = 0;
    int size = 1024;
    char buf[1024];
    char *p = NULL;

    if (vty_shell (vty)) {
        va_start (args, format);
        vprintf(format, args);
        va_end (args);
    }
    else {
        /* Try to write to initial buffer.  */
        va_start (args, format);
        len = vsnprintf(buf, sizeof buf, format, args);
        va_end (args);

        /* Initial buffer is not enough.  */
        if (len < 0 || len >= size) {
            while (1) {
                if (len > -1)
                    size = len + 1;
                else
                    size = size * 2;

                p = XREALLOC (MTYPE_VTY_OUT_BUF, p, size);
                if (!p)
                    return -1;

                va_start (args, format);
                len = vsnprintf(p, size, format, args);
                va_end (args);

                if (len > -1 && len < size)
                    break;
            }
        }

        /* When initial buffer is enough to store all output.  */
        if (!p)
            p = buf;

        /* Pointer p must point out buffer. */
        buffer_put (vty->obuf, (u_char *) p, len);
        //dump_hex_value(__func__, p, len);
        //printf("-------------\n");
        //dump_hex_value(__func__, format, strlen(format));


        /* If p is not different with buf, it is allocated buffer.  */
        if (p != buf)
            XFREE (MTYPE_VTY_OUT_BUF, p);
    }

    return len;
}

/* Start. Add by steven.tian 2015/11/18 */
/* support 1024 bytes */
int
vty_out_line (struct vty *vty, const char *format, ...)
{
    char buf[1025] = {0};
    va_list args;
    int len = 0;

    if (vty == NULL)
    {
        return 0;
    }

    va_start (args, format);
    len = vsnprintf(buf, sizeof(buf), format, args);
    va_end (args);

    if (len < 0)
    {
        return 0;
    }

    vty_out(vty, "%s%s", buf, VTY_NEWLINE);

    return strlen(buf);
}
/* End. Add by steven.tian 2015/11/18 */

/* Start. add by keith.gong 20151022. */
extern void
vty_print_string_line (struct vty *vty, char *pString, unsigned int length, char fill)
{
    int limit, size;

    if (0 == length) {
        vty_out (vty, "%s", pString);
        return;
    }

    size = (int) strlen (pString);
    limit = length < size ? length : size;
    vty_out (vty, pString, limit);

    limit = length - limit;
    while (0 < limit--)
        vty_out (vty, "%c", fill);

    vty_out (vty, "%s", VTY_NEWLINE);
}

/* End. add by keith.gong 20151022*/
/*
    stephe.liu
*/
void
dump_hex_value (const char *func_name, unsigned char *value, int size)
{
    int i;
    VTY_PRINT ("++++++++++size = %d+++func_name:%s\r\n", size, func_name == NULL ? "NULL" : func_name);
    for (i = 0; i < size; i++) {
        VTY_PRINT ("%x ", value[i]);
        if (((i + 1) / 16 > 0)
            && ((i + 1) % 16 == 0))
            VTY_PRINT ("\r\n");
    }
    VTY_PRINT ("\r\n");
    VTY_PRINT ("++++++++++++++++++++++++++++++++++++++\r\n");
}


static int
vty_log_out (struct vty *vty, const char *level, const char *proto_str,
             const char *format, struct timestamp_control *ctl, va_list va)
{
    int ret;
    int len;
    char buf[1024];

    if (!ctl->already_rendered) {
        ctl->len = quagga_timestamp (ctl->precision, ctl->buf, sizeof (ctl->buf));
        ctl->already_rendered = 1;
    }
    if (ctl->len + 1 >= sizeof (buf))
        return -1;
    memcpy (buf, ctl->buf, len = ctl->len);
    buf[len++] = ' ';
    buf[len] = '\0';

    if (level)
        ret = snprintf(buf + len, sizeof (buf) - len, "%s: %s: ", level, proto_str);
    else
        ret = snprintf(buf + len, sizeof (buf) - len, "%s: ", proto_str);
    if ((ret < 0) || ((size_t) (len += ret) >= sizeof (buf)))
        return -1;

    if (((ret = vsnprintf(buf + len, sizeof (buf) - len, format, va)) < 0) ||
        ((size_t) ((len += ret) + 2) > sizeof (buf)))
        return -1;

    buf[len++] = '\r';
    buf[len++] = '\n';

    if (write (vty->fd, buf, len) < 0) {
        if (ERRNO_IO_RETRY (errno))
            /* Kernel buffer is full, probably too much debugging output, so just
               drop the data and ignore. */
            return -1;
        /* Fatal I/O error. */
        vty->monitor = 0;       /* disable monitoring to avoid infinite recursion */
        zlog_warn ("%s: write failed to vty client fd %d, closing: %s", __func__, vty->fd, safe_strerror (errno));
        buffer_reset (vty->obuf);
        /* cannot call vty_close, because a parent routine may still try
           to access the vty struct */
        vty->status = VTY_CLOSE;
        shutdown (vty->fd, SHUT_RDWR);
        return -1;
    }
    return 0;
}

/* Output current time to the vty. */
void
vty_time_print (struct vty *vty, int cr)
{
    char buf[25];

    if (quagga_timestamp (0, buf, sizeof (buf)) == 0) {
        zlog (NULL, LOG_INFO, "quagga_timestamp error");
        return;
    }
    if (cr)
        vty_out (vty, "%s\n", buf);
    else
        vty_out (vty, "%s ", buf);

    return;
}

/* Say hello to vty interface. */
void
vty_hello (struct vty *vty)
{
    if (host.motdfile) {
        FILE *f;
        char buf[4096];

        f = fopen (host.motdfile, "r");
        if (f) {
            while (fgets (buf, sizeof (buf), f)) {
                char *s;
                /* work backwards to ignore trailling isspace() */
                for (s = buf + strlen (buf); (s > buf) && isspace ((int) *(s - 1)); s--);
                *s = '\0';
                vty_out (vty, "%s%s", buf, VTY_NEWLINE);
            }
            fclose (f);
        }
        else
            vty_out (vty, "MOTD file not found%s", VTY_NEWLINE);
    }
    else if (host.motd)
        vty_out (vty, "%s", host.motd);
}

/* Put out prompt and wait input from user. */
char *
vty_prompt (struct vty *vty)
{
    struct utsname names;
    static char buf[256];
    char hostname[64] = {0};

    snprintf(hostname, 60,"\r\n%s",host.name);
    if (!hostname) {
        uname (&names);
        snprintf(hostname, 60, "\r\n%s", names.nodename);
    }
    memset(buf, 0x00, sizeof(buf));//stephen.liu
    switch (vty->node) {
    case TEST_NODE:
        snprintf(buf, sizeof (buf), cmd_prompt (vty->node), hostname, "test");
        break;
  /*
    case SLA_PROFILE_NODE:
        snprintf(buf, sizeof (buf), cmd_prompt (vty->node), hostname, vty->user.env.slaprofile_id);
        break;
    case SIPAGENT_PROFILE_NODE:
        snprintf(buf, sizeof (buf), cmd_prompt (vty->node), hostname, vty->user.env.sipagent_profile_id);
        break;
    case POTS_PROFILE_NODE:
        snprintf(buf, sizeof (buf), cmd_prompt (vty->node), hostname, vty->user.env.pots_profile_id);
        break;
    case DIGITMAP_PROFILE_NODE:
        snprintf(buf, sizeof (buf), cmd_prompt (vty->node), hostname, vty->user.env.digitmap_profile_id);
        break;
    case SIPRIGHT_PROFILE_NODE:
        snprintf(buf, sizeof (buf), cmd_prompt (vty->node), hostname, vty->user.env.sipright_profile_id);
        break;
    case CLASSIFICATION_PROFILE_NODE: 
        snprintf(buf, sizeof (buf), cmd_prompt (vty->node), hostname, vty->user.env.classificationprofile_id);
        break;
    case INTERFACE_MVLAN_NODE:
        snprintf(buf, sizeof (buf), cmd_prompt (vty->node), hostname, vty->user.env.mvlan_id);
        break;
    case INTERFACE_VLANIF_NODE:
        snprintf(buf, sizeof (buf), cmd_prompt (vty->node), hostname, vty->user.env.vlanif_id);
        break;
    case ENABLE_REBOOT_INTERACTION_NODE:
    case CONFIG_REBOOT_INTERACTION_NODE:
        snprintf(buf, sizeof (buf), cmd_prompt (vty->node));
        break;
        */
    default:
        snprintf(buf, sizeof (buf), cmd_prompt (vty->node), hostname);
    }

    if (vty->type == VTY_TERM || vty->type == VTY_TERM_LOCAL || vty->type == VTY_SSH) {
        vty->prompt_len = strlen(buf)-2; // 2, because insert 13 10 to first and second buffer, by stephen.liu 20160121
        if(TERM_TYPE_ANSI == vty->terminal_type){
            vty_out (vty, "%s\033[s", buf);
        }else{
            vty_out (vty, "%s", buf);
        }
    }
    else if (vty->type == VTY_SHELL) {
		vty->prompt_len = strlen(buf)-2;
        return buf;
    }
    return NULL;
}

 void
vty_send_environ (struct vty *vty)
{
    unsigned char cmd[] = { IAC, WILL, TELOPT_NEW_ENVIRON, '\0' };
    vty_out (vty, "%s", cmd);

	 unsigned char cmd1[] = { IAC, DO, TELOPT_SNDLOC, '\0' };
    vty_out (vty, "%s", cmd1);
}


/* Send WILL TELOPT_ECHO to remote server. */
 void
vty_do_echo (struct vty *vty)
{
    unsigned char cmd[] = { IAC, DO, TELOPT_ECHO, '\0' };
    vty_out (vty, "%s", cmd);
}

 void
vty_will_echo (struct vty *vty)
{
    unsigned char cmd[] = { IAC, WILL, TELOPT_ECHO, '\0' };
    vty_out (vty, "%s", cmd);
}

/* Make suppress Go-Ahead telnet option. */
static void
vty_will_suppress_go_ahead (struct vty *vty)
{
    unsigned char cmd[] = { IAC, WILL, TELOPT_SGA, '\0' };
    vty_out (vty, "%s", cmd);
}

/* Make don't use linemode over telnet. */
void
vty_dont_linemode (struct vty *vty)
{
    unsigned char cmd[] = { IAC, DONT, TELOPT_LINEMODE, '\0' };
    vty_out (vty, "%s", cmd);
}

/* Use window size. */
static void
vty_do_window_size (struct vty *vty)
{
    unsigned char cmd[] = { IAC, DO, TELOPT_NAWS, '\0' };
    vty_out (vty, "%s", cmd);
}

/* Use window type. */
static void
vty_do_window_type (struct vty *vty, int param)
{
    unsigned char cmd[] = { IAC, param, TELOPT_TTYPE, '\0' };
    vty_out (vty, "%s", cmd);
}

void vty_do_cr(struct vty *vty)
{
	unsigned char cmd[] = { IAC, DO, TELOPT_NAOCRD, '\0' };
	vty_out (vty, "%s", cmd);
}
void vty_do_lf(struct vty *vty)
{
	unsigned char cmd[] = { IAC, DO, TELOPT_NAOLFD, '\0' };
	vty_out (vty, "%s", cmd);
}


#if 0                           /* Currently not used. */
/* Make don't use lflow vty interface. */
static void
vty_dont_lflow_ahead (struct vty *vty)
{
    unsigned char cmd[] = { IAC, DONT, TELOPT_LFLOW, '\0' };
    vty_out (vty, "%s", cmd);
}
#endif /* 0 */

/* Allocate new vty struct. */
struct vty *
vty_new ()
{
    struct vty *new = XCALLOC (MTYPE_VTY, sizeof (struct vty));

    new->obuf = buffer_new (0); /* Use default buffer size. */
    /***Begin: Add by steven.tian on 2015/12/29*************/
    new->dbuf = buffer_new (0);
    /***End: Add by steven.tian on 2015/12/29*************/
    new->buf = XCALLOC (MTYPE_VTY, VTY_BUFSIZ);
    new->max = VTY_BUFSIZ;
	//stephen.liu, 20161213
    new->temp_buf = XCALLOC (MTYPE_VTY, VTY_BUFSIZ);
    new->temp_max = VTY_BUFSIZ;
    return new;
}

void
vty_login_show (struct vty *vty, char showSelfFlag)
{
    unsigned int idx;
    struct vty *v;
    char *typeStr[] = { "Telnet", "File", "Shell", "Shell Server", "Shell Client", "Console", "SSH" };
    struct timeval time_end;
    int sec;
    char printed = 0;
    char loginFlag;

    gettimeofday (&time_end, NULL);

    for (idx = 0; idx < vector_active (vtyvec); idx++) {
        if ((v = vector_slot (vtyvec, idx)) != NULL) {
            if (!showSelfFlag && v == vty)
                continue;
            //add by stephen.liu 20160822
            if(v->type == VTY_SHELL_SERV)
                continue;

            if (v->node == AUTH_NODE || v->node == USER_NODE || v->node == AUTH_ENABLE_NODE)
            {
                /* 不显示未登陆串口*/
                if(v->type == VTY_TERM_LOCAL)
                    continue;

                loginFlag = 0;
            }
            else
                loginFlag = 1;

            if (!printed) {
                vty_out (vty, "  %-3s %-12s   %-16s   %-17s   %s%s",
                         "ID", "Access-Type", "User-Name", "IP-Address", "Login-Time", VTY_NEWLINE);
                vty_print_string_line (vty, " ", __PROMPT_SEPARATOR_LENGTH__, __PROMPT_SEPARATOR_CHAR__);
                printed = 1;
            }

            if (!loginFlag)
            {
                vty_out (vty, "  %-3d %-12s   %-16s   %-17s   %s%s",
                        idx, typeStr[v->type], "--", v->address, "--", VTY_NEWLINE);
            }
            else
            {
                sec = time_end.tv_sec - v->time_start.tv_sec;
                vty_out (vty, " %c%-3d %-12s   %-16s   %-17s   %02d:%02d:%02d%s",
                        v == vty ? '>' : ' ',
                        idx,
                        typeStr[v->type],
                        v->sign_user.name,
                        v->type == VTY_TERM_LOCAL ? "--" :v->address,
                        sec / 3600, sec % 3600 / 60, sec % 60, VTY_NEWLINE);
            }
        }
    }

    if(printed)
        vty_out (vty, "%s", VTY_NEWLINE);

    return;
}

/* Authentication of vty */
static void
vty_auth (struct vty *vty, char *buf)
{
    enum node_type next_node = 0;
    int fail;
    char *crypt (const char *, const char *);
    struct vty_user *user = NULL;
    vty_user_info_login *login_info = NULL;

    //printf("input:%s   passwd:%s\n", buf, host.password_encrypt);

    switch (vty->node) {
    case USER_NODE:
		if(VTY_USER_MAX_LEN < strlen(buf))
            vty_out (vty, "The length of the user name is invalid!%s", VTY_NEWLINE);
        else if(!strcmp(vty->sign_user.name, "manu") && vty->type != VTY_TERM_LOCAL)
            vty_out (vty, "This account can only be used on the console!%s", VTY_NEWLINE);
        else
        {
	        strcpy (vty->sign_user.name, buf);
	        vty->node = AUTH_NODE;
		}
        return;
    case AUTH_NODE:
        /* ssh 将client 的ip 当做密码传递给vtysh,  由于ssh已经通过验证，vtysh无需再次验证密码*/
        if (vty->type == VTY_SSH)
        {
            user = vty_user_lookup(vty->sign_user.name);
            snprintf(vty->address, sizeof(vty->address), buf);
            vty->fail = 2;
            if(NULL != user)
                user->state = VTY_VERIFY_OK;
        }
        else
            user = (struct vty_user *) vty_user_check_passwd (vty->sign_user.name, buf);
        
        if (NULL != user && user->state == VTY_VERIFY_OK) {
            strcpy (vty->sign_user.password, buf);
            login_info = (vty_user_info_login *) vty_user_add_to_host_hist (user);
            if (login_info) {
                strcpy (login_info->address, vty->address);
                login_info->port = vty->port;
            }
            vty->sign_user.level = user->level;
            vty->sign_user.style = 1;//tfDeviceCustomGet();
            fail = 0;
            next_node = VIEW_NODE;

            if ((vty->sign_user.level & ENUM_ACCESS_OP_MANUFACTURE) == 0)
            {
                /* Login success record */
                char *typeStr[] = { "Telnet", "File", "Shell", "Shell Server", "Shell Client", "Console", "SSH" };
                tflog_oper("[%s@%s:%d] logon via %s successfully",

                    vty->sign_user.name, vty->address[0] ? vty->address : "unknown", vty->fd, typeStr[vty->type]);
            }

            /* console timeout, after login */
            if (vty->type == VTY_TERM || vty->type == VTY_TERM_LOCAL || vty->type == VTY_SSH)
                vty->v_timeout = VTY_TIMEOUT_DEFAULT;
        }
        else {
            vty_out (vty, "The user name or password is invalid!%s", VTY_NEWLINE);
            fail = 1;

            /* Login failed record */
            char *typeStr[] = { "Telnet", "File", "Shell", "Shell Server", "Shell Client", "Console", "SSH" };
            tflog_oper("[%s@%s:%d] logon via %s failed",
                vty->sign_user.name, vty->address[0] ? vty->address : "unknown", vty->fd, typeStr[vty->type]);
        }

        break;
    }

    if (!fail) {
        vty->fail = 0;
        vty->node = next_node;  /* Success ! */

#if VTYSH_ENABLE_CONSOLE
        /* Add by keith.gong 20151022 */
        if (vty_shell (vty))
            clear_history ();
#endif
        gettimeofday (&vty->time_start, NULL);
        vty_out (vty, "%s", VTY_NEWLINE);
        //vty_logo_show(vty);
        vty_login_show (vty, 1);
    }
    else {
        vty->fail++;
        if (vty->fail >= 3) {
#if VTYSH_ENABLE_CONSOLE
            if (vty_shell (vty)) {
                vty_out (vty, "%s%s%s", VTY_NEWLINE, VTY_NEWLINE, VTY_NEWLINE);
                vty->fail = 0;
                clear_history ();
                vty->node = USER_NODE;
                sleep (5);
            }
            else
#endif
                vty->status = VTY_CLOSE;
        }
        else {
            vty->node = USER_NODE;
            vty_out (vty, "%s", VTY_NEWLINE);
        }
    }
}

static int vty_execte_cmd_permit(void)
{
    short retCode = 0;
    int   rc = 0;
    
    SysOperStatus_t operStatus;
    memset(&operStatus, 0, sizeof(operStatus));
    operStatus.type = SYS_OPER_STATUS_TYPE_INTERACTION;

    if (ipc_if_get_thismoid() != MODULE_SYSCTRL)
    {
        rc = ipc_if_get_cmd_result(MODULE_SYSCTRL, IPC_SYS_OPER_STATUS_GET, (char*)&operStatus, sizeof(operStatus),
                (char*)&operStatus, sizeof(operStatus), &retCode);

        if (rc || retCode || !operStatus.val.status)
        {
            rc = VTY_EXEC_DENY;
        }
        else
        {
            rc = VTY_EXEC_PERMIT;
        }
    }
    else
    {
        rc = VTY_EXEC_PERMIT;
    }

    return rc;
}

static int vty_is_terminal(struct vty *vty)
{
    if (!vty)
        return 0;
    
    if ((vty->type == VTY_TERM)
        || (vty->type == VTY_SHELL)
        || (vty->type == VTY_TERM_LOCAL)
        || (vty->type == VTY_SSH))
    {
        return 1;
    }

    return 0;
}

static int vty_command_err_msg(struct vty *vty, int err)
{
    char task_name[128];

	memset(task_name, 0x00, sizeof(task_name));
	
    if (!vty)
        return -1;

    #if 0
    switch (err) 
    {
        case CMD_WARNING:
            if (vty->type == VTY_FILE)
                vty_out (vty, "%s Warning...%s", (char *)getNameByPid(getpid(), task_name), VTY_NEWLINE);
            break;
        case CMD_ERR_AMBIGUOUS:
            vty_out (vty, "%s%% Ambiguous command.%s", (char *)getNameByPid(getpid(), task_name), VTY_NEWLINE);
            break;
        case CMD_ERR_NO_MATCH:
            vty_out (vty, "%s%% [%s] vty[node:%d],Unknown command: %s%s", (char *)getNameByPid(getpid(), task_name), protocolname, vty->node, buf, VTY_NEWLINE);
            break;
        case CMD_ERR_INCOMPLETE:
            vty_out (vty, "%s%% Command incomplete.%s", (char *)getNameByPid(getpid(), task_name),  VTY_NEWLINE);
            break;
        case CMD_ERR_NOTHING_TODO:
            //printf("nothing to do.\n");
            //vty_out(vty, "no%s", VTY_NEWLINE);
            break;
    }
    #else
    switch (err) 
    {
        case CMD_WARNING:
            if (vty->type == VTY_FILE)
                vty_out (vty, "Warning...%s", VTY_NEWLINE);
            break;
        case CMD_ERR_AMBIGUOUS:
            vty_out (vty, "Ambiguous command.%s", VTY_NEWLINE);
            break;
        case CMD_ERR_NO_MATCH:
            vty_out (vty, "Unknown command: %s%s", vty->buf, VTY_NEWLINE);
            break;
        case CMD_ERR_INCOMPLETE:
            vty_out (vty, "Command incomplete.%s", VTY_NEWLINE);
            break;
        case CMD_ERR_NOTHING_TODO:
            //printf("nothing to do.\n");
            //vty_out(vty, "no%s", VTY_NEWLINE);
            break;
    }
    #endif

    return 0;
}

/* Command execution over the vty interface. */
static int
vty_command (struct vty *vty, char *buf)
{
    int ret;
    vector vline;
    const char *protocolname;
	
    /* Split readline string up into the vector */
    vline = cmd_make_strvec (buf);

    if (vline == NULL)
        return CMD_SUCCESS;

#ifdef CONSUMED_TIME_CHECK
    {
        RUSAGE_T before;
        RUSAGE_T after;
        unsigned long realtime, cputime;

        GETRUSAGE (&before);
#endif /* CONSUMED_TIME_CHECK */

        /* 检查是否禁止命令执行 */
        /* 例如:已经在等待设备重启 */
        if (vty_is_terminal(vty) && (VTY_EXEC_DENY == vty_execte_cmd_permit()))
        {
            vty_out_line(vty, "  Error: Command execution is rejected!");
            return CMD_SUCCESS;
        }

        ret = cmd_execute_command (vline, vty, NULL, 0);
        /*fprintf(stderr, "ret=%d\r\n", ret);  //stephen.liu*/

        /* Get the name of the protocol if any */
        if (zlog_default)
            protocolname = zlog_proto_names[zlog_default->protocol];
        else
            protocolname = zlog_proto_names[ZLOG_NONE];

#ifdef CONSUMED_TIME_CHECK
        GETRUSAGE (&after);
        if ((realtime = thread_consumed_time (&after, &before, &cputime)) > CONSUMED_TIME_CHECK)
            /* Warn about CPU hog that must be fixed. */
            zlog_warn ("SLOW COMMAND: command took %lums (cpu time %lums): %s", realtime / 1000, cputime / 1000, buf);
    }
#endif /* CONSUMED_TIME_CHECK */

    if (ret != CMD_SUCCESS)
        vty_command_err_msg(vty, ret);

    cmd_free_strvec (vline);

    return ret;
}

static const char telnet_backward_char = 0x08;
static const char telnet_space_char = ' ';

#define VTY_CHANGE_LINE_CHECK(vty)  (0  == (vty->cp -(vty->width - vty->prompt_len))%vty->width)
/* Basic function to write buffer to vty. */
static void
vty_write (struct vty *vty, const char *buf, size_t nbytes)
{
    if ((vty->node == AUTH_NODE) || (vty->node == AUTH_ENABLE_NODE))
        return;

    /* Should we do buffering here ?  And make vty_flush (vty) ? */
    buffer_put (vty->obuf, buf, nbytes);
}

/* Ensure length of input buffer.  Is buffer is short, double it. */
static void
vty_ensure (struct vty *vty, int length)
{
    if (vty->max <= length) {
        vty->max *= 2;
        vty->buf = XREALLOC (MTYPE_VTY, vty->buf, vty->max);
    }
}

/* Ensure length of input buffer.  Is buffer is short, double it. */
static void
vty_temp_ensure (struct vty *vty, int length)
{
    if (vty->temp_max <= length) {
        vty->temp_max *= 2;
        vty->temp_buf = XREALLOC (MTYPE_VTY, vty->temp_buf, vty->temp_max);
    }
}

static void vty_down_one_line(struct vty *vty)
{
    int i = 0;

    if((vty->terminal_type == TERM_TYPE_VT100)||
        (vty->terminal_type == TERM_TYPE_vt102)||
        (vty->terminal_type == TERM_TYPE_vt100)){
        vty_out(vty, "\r\n");
    }else if(vty->terminal_type == TERM_TYPE_ANSI){
        //printf("down ansi.\r\n");
        vty_out(vty, "\033[1B");
        for (i = 0; i < vty->width; i++)
            vty_write (vty, &telnet_backward_char, 1);
    }else if(vty->terminal_type == TERM_TYPE_linux){
        vty_out(vty, "\r\n");
        //vty_out(vty, "\033[1B");
        //for (i = 0; i < vty->width; i++)
        //    vty_write (vty, &telnet_backward_char, 1);
    }
}


static void vty_up_one_line(struct vty *vty)
{
    //printf("1vty up line: cmd_lines=%d\r\n", vty->cmd_lines);
    if((vty->terminal_type == TERM_TYPE_ANSI)||
        (vty->terminal_type == TERM_TYPE_VT100)||
        (vty->terminal_type == TERM_TYPE_vt102)||
        (vty->terminal_type == TERM_TYPE_vt100)){
        vty_out(vty, "\033[1A");
        if(vty->cmd_lines)
            vty->cmd_lines--;
    }else{
        vty_out(vty, "\033[1A");
        if(vty->cmd_lines)
            vty->cmd_lines--;
    }
    //printf("2vty up line: cmd_lines=%d\r\n", vty->cmd_lines);
   
    //vty->cp--;
}


static void vty_cursor_to_end(struct vty *vty)
{
    char buf[32];

    memset (buf, 0x00, sizeof (buf));
    sprintf(buf, "\033[%dC", vty->width - 1);
    vty_out (vty, buf);
}

static void vty_cursor_upline_end(struct vty *vty)
{
        if((vty->terminal_type == TERM_TYPE_ANSI)||
            (vty->terminal_type == TERM_TYPE_VT100)||
            (vty->terminal_type == TERM_TYPE_vt102)||
            (vty->terminal_type == TERM_TYPE_vt100)){
            //vty_out(vty, "\033[%dD", vty->width + 1);
            vty_out(vty, "\033[1A");
            if(vty->cmd_lines)
                vty->cmd_lines--;
        }else{
            //vty_out(vty, "\033[%dD", vty->width + 1);
            vty_out(vty, "\033[1A");
            if(vty->cmd_lines)
                vty->cmd_lines--;
        }
        vty_out(vty, "\033[%dD", vty->width + 1);
        vty_out(vty, "\033[%dC", vty->width - 1);
}

static void
vty_cursor_to_begin_line (struct vty *vty)
{
    char buf[32];

    memset (buf, 0x00, sizeof (buf));
    sprintf(buf, "\033[%dD", vty->width - 1);
    vty_out (vty, buf);
}

static void
vty_cursor_move_left_pos (struct vty *vty, int pos)
{
    char buf[32];

    memset (buf, 0x00, sizeof (buf));
    sprintf(buf, "\033[80D");
    vty_out (vty, buf);
}

static void
vty_cursor_move_right_pos (struct vty *vty, int pos)
{
    char buf[32];

    memset (buf, 0x00, sizeof (buf));
    sprintf(buf, "\033[%dC", pos);
    vty_out (vty, buf);
}

static void vty_reset_cmdlines(struct vty*vty)
{
    /* reset cmd_lines , stephen.liu 20160810 */
    if ((vty->length + vty->prompt_len) < vty->width)
        vty->cmd_lines = 0;
    else {
        vty->cmd_lines = (vty->length - (vty->width - vty->prompt_len)) / vty->width + 1;
    }
}

static void
vty_get_term_type (struct vty *vty)
{
    unsigned char cmd[] = { IAC, SB, TELOPT_TTYPE, 1, IAC, SE, '\0' };
    vty_out (vty, "%s", cmd);
}



static const char telnet_newline_char1 = '\r';
static const char telnet_newline_char2 = '\n';

/* Basic function to insert character into vty. */
static void
vty_self_insert (struct vty *vty, char c)
{
    int i;
    int length;
    int flag = 0;
    unsigned int uplines = 0, al = 0, sit = 0;
    unsigned int first_line_len = vty->width - vty->prompt_len;
    unsigned int last_line_len = (vty->length - first_line_len)%vty->width;
    unsigned int row = vty->cmd_lines;
    unsigned int col = (vty->cp - first_line_len)%vty->width;

    vty_ensure (vty, vty->length + 1);
    length = vty->length - vty->cp;
    memmove (&vty->buf[vty->cp + 1], &vty->buf[vty->cp], length);
    vty->buf[vty->cp] = c;

    vty_write (vty, &vty->buf[vty->cp], length + 1);
    vty->length++;
    vty->cp++;
    vty->new_temp_hist = TRUE;
    //for change lines down, cp == lenght
    /* stephen.liu 20160810 */
    VTY_PRINT("00, first_line_len=%d, vty->cp=%d\r\n", first_line_len, vty->cp);
    if( ((vty->terminal_type == TERM_TYPE_ANSI)||
        (vty->terminal_type == TERM_TYPE_linux)||
        (vty->terminal_type == TERM_TYPE_VT100)||
        (vty->terminal_type == TERM_TYPE_vt102)||
        (vty->terminal_type == TERM_TYPE_vt100)) &&(!length)){
        VTY_PRINT ("type=%s\r\n", vty->terminal_type == TERM_TYPE_ANSI?"ansi":"linux");
        if(vty->cp == first_line_len){
            vty_down_one_line (vty);
            vty->cmd_lines++;
        }else if(vty->cp > first_line_len){
            if(0 ==  ((vty->cp - first_line_len) % vty->width)){
                vty_down_one_line (vty);
                vty->cmd_lines++;
            }
        }
    }
    
    //just because ansi's '\b' can't backspace and up one line
    //cp != length
    VTY_PRINT ("insert before:cp=%d, width=%d, line=%d,length=%d, len=%d prompt=%d\r\n", vty->cp,
            vty->width, vty->cmd_lines, vty->length, length, vty->prompt_len);
    VTY_PRINT("aaa  al=%d\r\n", al);
    if(((TERM_TYPE_ANSI == vty->terminal_type) ||
        (TERM_TYPE_VT100== vty->terminal_type)||
        (TERM_TYPE_linux== vty->terminal_type)||
        (vty->terminal_type == TERM_TYPE_vt102)||
        (vty->terminal_type == TERM_TYPE_vt100))
        && length){

        //check the last character if change line
        if(vty->length == (first_line_len+1)){
            VTY_PRINT("d 1\r\n");
            //vty_out(vty, "\033[1B");  //down line
        }else if(vty->length > (first_line_len+1)){
            //printf("d 2\r\n");
            if(0 ==  ((vty->length - (first_line_len+1)) % vty->width)){
                //printf("d 3\r\n");
                //vty_out(vty, "\033[1B");  //down line
            }
        }
        
        //check the cp character if change line
        #if 1
        if(vty->cp == first_line_len){
            VTY_PRINT("d 11\r\n");
            //vty_out(vty, "\033[1B");  //down line
            vty->cmd_lines++;
        }else if(vty->cp > first_line_len){
            VTY_PRINT("d 12\r\n");
            if(0 ==  ((vty->cp - first_line_len) % vty->width)){
                VTY_PRINT("d 13\r\n");
                //vty_out(vty, "\033[1B");  //down line
                vty->cmd_lines++;
            }
        }
        #endif
        if(vty->length <= first_line_len){
            al = 0;
        }else
            al = (vty->length - (first_line_len+1))/vty->width + 1;
        uplines = al - vty->cmd_lines ;
        VTY_PRINT("ansi pro.al=%d,vty->cmd_lines=%d\r\n", al, vty->cmd_lines);
        //scroll back to cp
        if(uplines){
            VTY_PRINT("0 21\r\n");
            if(vty->cp == (first_line_len)){
                VTY_PRINT("0 uplines=%d 22\r\n", uplines);
                sit = vty->width - 1;
            }else if(vty->cp < (first_line_len)){
                sit = first_line_len - vty->cp - 1;
            }else{
                VTY_PRINT("0 cp > first line len. \r\n");
                sit = vty->width-(vty->cp - first_line_len)%vty->width-1;
            }
            vty_out(vty, "\033[%dA", uplines);  //up lines
            vty_out(vty, "\033[%dD", vty->width + 1);
            vty_out(vty, "\033[%dC", vty->width - 1);  //end line
            VTY_PRINT("0 sit=%d\r\n", sit);
            if(sit > vty->width){
                VTY_PRINT("error sit=%d.\r\n", sit);
                exit(0);
            }
            for (i = 0; i < sit; i++){
                vty_write (vty, &telnet_backward_char, 1);
            }
        }else{
            VTY_PRINT("0 22\r\n");
            if((vty->length == first_line_len)||
                ((vty->length>first_line_len)&&((vty->length-first_line_len)%vty->width==0))){
                vty_out(vty, "\033[%dD", vty->width + 1);
                vty_out(vty, "\033[%dC", vty->width - 1);  //end line, as the last char input, cursor will be set at begin
                sit = length-1;
            }else{
                sit = length;
            }
            for (i = 0; i < sit; i++){
                vty_write (vty, &telnet_backward_char, 1);
            }
        }
    }else
    for (i = 0; i < length; i++){
        vty_write (vty, &telnet_backward_char, 1);
    }

    VTY_PRINT("length=%d\r\n", length);

    VTY_PRINT ("insert:cp=%d, width=%d, line=%d,length=%d, prompt=%d\r\n", vty->cp,
            vty->width, vty->cmd_lines, vty->length, vty->prompt_len);
}

/* Self insert character 'c' in overwrite mode. */
static void
vty_self_insert_overwrite (struct vty *vty, char c)
{
    vty_ensure (vty, vty->length + 1);
    vty->buf[vty->cp++] = c;

    if (vty->cp > vty->length)
        vty->length++;

    if ((vty->node == AUTH_NODE) || (vty->node == AUTH_ENABLE_NODE))
        return;

    vty_write (vty, &c, 1);
}

/* Insert a word into vty interface with overwrite mode. */
static void
vty_insert_word_overwrite (struct vty *vty, char *str)
{
    int len = strlen (str);
    vty_write (vty, str, len);
    strcpy (&vty->buf[vty->cp], str);
    vty->cp += len;
    vty->length = vty->cp;
}

/* Forward character. */
static void
vty_forward_char (struct vty *vty)
{
    if (vty->cp < vty->length) {
        vty_write (vty, &vty->buf[vty->cp], 1);
        vty->cp++;
        
        if(0  == (vty->cp -(vty->width - vty->prompt_len))%vty->width){
            vty_down_one_line(vty);
            vty->cmd_lines++;
        }
    }
}

/* Backward character. */
static void
vty_backward_char (struct vty *vty)
{
    if (vty->cp > 0) {
        if(vty->cmd_lines){
            if(VTY_CHANGE_LINE_CHECK(vty)){
                VTY_PRINT("backword char.\r\n");
                vty_cursor_upline_end(vty);
            }else
            vty_write (vty, &telnet_backward_char, 1);
        }else
            vty_write (vty, &telnet_backward_char, 1);
        vty->cp--;
    }
}

/* Move to the beginning of the line. */
static void
vty_beginning_of_line (struct vty *vty)
{
    while (vty->cp)
        vty_backward_char (vty);
}

/* Move to the end of the line. */
static void
vty_end_of_line (struct vty *vty)
{
    while (vty->cp < vty->length)
        vty_forward_char (vty);
}

static void vty_kill_line_from_beginning (struct vty *);
static void vty_redraw_line (struct vty *);

/* Print command line history.  This function is called from
   vty_next_line and vty_previous_line. */
static void
vty_history_print (struct vty *vty)
{
    int length;

    vty_kill_line_from_beginning (vty);

    /* Get previous line from history buffer */
    length = strlen (vty->hist[vty->hp]);
    memcpy (vty->buf, vty->hist[vty->hp], length);
    vty->cp = vty->length = length;
    VTY_PRINT("vty->buf:]%s[, srchist buf:]%s[\r\n", vty->buf, vty->hist[vty->hp]);

    /* reset cmd_lines , stephen.liu 20160810 */
    vty_reset_cmdlines(vty);
    
    /* Redraw current line */
    vty_redraw_line (vty);
}

static void
vty_temp_history_print (struct vty *vty)
{
    int length;

    vty_kill_line_from_beginning (vty);
    if(!vty->temp_length)
        return;

    /* Get previous line from history buffer */
    length = strlen (vty->temp_buf);
    //printf("%s %d, len=%d, vty->length=%d, vty->cp=%d\n"
    //    , __func__, __LINE__, length, vty->length, vty->cp);
    memcpy (vty->buf, vty->temp_buf, length);
    vty->cp = vty->length = length;

    /* reset cmd_lines , stephen.liu 20160810 */
    vty_reset_cmdlines(vty);
    
    /* Redraw current line */
    vty_redraw_line (vty);
}

void vty_clear_temp_history(struct vty *vty)
{
    int length;
    if(vty->temp_buf){
        length = strlen (vty->temp_buf);
        memset(vty->temp_buf, 0x00, strlen(vty->temp_buf));
        vty->temp_length = 0;
        vty->temp_cp = 0;
    }
}

void vty_add_temp_history(struct vty *vty)
{
    VTY_PRINT("%s %d, len=%d, new_temp:%d, buf]%s[\r\n", 
                __func__, __LINE__, vty->length, vty->new_temp_hist
                , vty->buf);
    if(vty->length && (vty->new_temp_hist)){
        vty->new_temp_hist = FALSE;
        vty_temp_ensure(vty,vty->length);
        memset(vty->temp_buf, 0x00, vty->temp_max);
        memcpy(vty->temp_buf , vty->buf,  vty->length);
        vty->temp_cp = vty->cp;
        vty->temp_length = vty->length;
    }
}

/* Show next command line history. */
static void
vty_next_line (struct vty *vty)
{
    int try_index;

    if (vty->hp == vty->hindex){
        vty_add_temp_history(vty);
        vty_kill_line_from_beginning (vty);
        return;
    }

    /* Try is there history exist or not. */
    try_index = vty->hp;

    if (try_index == (VTY_MAXHIST - 1))
        try_index = 0;
    else
        try_index++;
//close by stephen.liu 20161212
#if 0
    /* Start add by keith.gong 20151022 */
    if (try_index == vty->hindex) {
        vty_kill_line_from_beginning (vty);
        vty->hp = try_index;
        return;
    }
    /* End add by keith.gong 20151022 */
#endif
    /* If there is not history return. */
    if (vty->hist[try_index] == NULL){
	vty->hp = try_index;
        vty_temp_history_print(vty);
        return;
    }else
        vty->hp = try_index;

    vty_history_print (vty);
}

/* Show previous command line history. */
static void
vty_previous_line (struct vty *vty)
{
    int try_index;

    //add by stephen.liu, 20161212, for save the temp data
    vty_add_temp_history(vty);
    if(vty->buf[0] =='\0'){
        VTY_PRINT("%s %d\r\n", __func__, __LINE__);
    }
    if(vty->buf[0] == '\0' && vty->length == 0){
        if(vty->temp_length){
            vty_temp_history_print(vty);
            return;
        }
    }

    try_index = vty->hp;
    if (try_index == 0)
        try_index = VTY_MAXHIST - 1;
    else
        try_index--;
//stephen.liu closed, 20161212		
#if 0
    /* Start add by keith.gong 20151022 */
    if (try_index == vty->hindex)
        return;
    /* End add by keith.gong 20151022 */
#endif
    if (vty->hist[try_index] == NULL){
        if(vty->hp == vty->hindex)
            vty->hp = try_index;
        return;
    }else
        vty->hp = try_index;

    vty_history_print (vty);
}

/* This function redraw all of the command line character. */
static void
vty_redraw_line (struct vty *vty)
{
    int i = 0, pos = 0;
    int cmdlines = vty->cmd_lines;
    int first_line_len = vty->width - vty->prompt_len;
    VTY_PRINT("vty->buf:]%s[\r\n", vty->buf);
    VTY_PRINT("%s cmd_lines=%d, length=%d\r\n", __func__, vty->cmd_lines, vty->length);
	//stephen.liu 20161212
    if(vty->cmd_lines){
        pos = vty->width-vty->prompt_len;
        vty_write (vty, vty->buf, pos);
        vty_down_one_line (vty);   
        for(i=1;i <= cmdlines;i++){
            if((vty->length-pos) < vty->width){
                vty_write (vty, &vty->buf[pos], vty->length-pos);
                VTY_PRINT("r1=%d , pos=%d, cmd_lines=%d\r\n", i, pos, vty->cmd_lines);
            }else{
                vty_write (vty, &vty->buf[pos], vty->width);
                pos += vty->width;
                vty_down_one_line (vty);
                VTY_PRINT("r2 pos=%d\r\n", pos);
            }
        }
    }else{
        vty_write (vty, vty->buf, vty->length);
        VTY_PRINT("vty->buf:]%s[\r\n", vty->buf);
        if(vty->length == first_line_len){
            vty_down_one_line (vty);   
        }
    }
    vty->cp = vty->length;
    vty_reset_cmdlines(vty);
}

/* Forward word. */
static void
vty_forward_word (struct vty *vty)
{
    while (vty->cp != vty->length && vty->buf[vty->cp] != ' ')
        vty_forward_char (vty);

    while (vty->cp != vty->length && vty->buf[vty->cp] == ' ')
        vty_forward_char (vty);
}

/* Backward word without skipping training space. */
static void
vty_backward_pure_word (struct vty *vty)
{
    while (vty->cp > 0 && vty->buf[vty->cp - 1] != ' ')
        vty_backward_char (vty);
}

/* Backward word. */
static void
vty_backward_word (struct vty *vty)
{
    while (vty->cp > 0 && vty->buf[vty->cp - 1] == ' ')
        vty_backward_char (vty);

    while (vty->cp > 0 && vty->buf[vty->cp - 1] != ' ')
        vty_backward_char (vty);
}

/* When '^D' is typed at the beginning of the line we move to the down
   level. */
void
vty_down_level (struct vty *vty)
{
    vty_out (vty, "%s", VTY_NEWLINE);
    (*config_exit_cmd.func) (NULL, vty, 0, NULL);
    vty_prompt (vty);
    vty->cp = 0;
}

/* When '^Z' is received from vty, move down to the enable mode. */
static void
vty_end_config (struct vty *vty)
{
    vty_out (vty, "%s", VTY_NEWLINE);

    switch (vty->node) {
#if 000
    case VIEW_NODE:
    case ENABLE_NODE:
    case RESTRICTED_NODE:
        /* Nothing to do. */
        break;
    case CONFIG_NODE:
    case INTERFACE_NODE:
    case TEST_NODE:
    case INTERFACE_GE_NODE:
    case ACL_BASIC_NODE:
    case ACL_ADV_NODE:
    case ACL_LINK_NODE:
    case ACL_USER_NODE:
    case ACL_NODE:
        vty_config_unlock (vty);
        vty->node = ENABLE_NODE;
        break;
    default:
        /* Unknown node, we have to ignore it. */
        break;
#else
    case VIEW_NODE:
    case RESTRICTED_NODE:
        /* Nothing to do. */
        break;
    default:
        vty_config_unlock (vty);
        vty->node = ENABLE_NODE;
        break;
#endif
    }

    vty_prompt (vty);
    vty->cp = 0;
}

/* Delete a charcter at the current point. */
static void
vty_delete_char (struct vty *vty)
{
    int i;
    int size;
    int flag = 0;
    unsigned int uplines = 0, al = 0, sit = 0;
    unsigned int first_line_len = vty->width - vty->prompt_len;
    
    if (vty->length == 0) {
        //stephen.liu 20160810
        //vty_down_level (vty);
        return;
    }
    VTY_PRINT("%s %d, vty->cp=%d, vty->length=%d\r\n", __func__, __LINE__, vty->cp, vty->length);

    if (vty->cp == vty->length){
        //not return, 20161215, stephen.liu
        //return;                 /* completion need here? */
        VTY_PRINT("%s %d, vty->cp=%d\r\n", __func__, __LINE__, vty->cp);
    }
    size = vty->length - vty->cp;

    vty->length--;
    memmove (&vty->buf[vty->cp], &vty->buf[vty->cp + 1], size - 1);
    vty->buf[vty->length] = '\0';
    vty->new_temp_hist = TRUE;

    if (vty->node == AUTH_NODE || vty->node == AUTH_ENABLE_NODE)
        return;
	//stephen.liu 20161212 modified
    VTY_PRINT ("[delete char s]length:%d, cp:%d, prompt:%d, size=%d,width=%d, line=%d, size=%d\r\n",
            vty->length, vty->cp, vty->prompt_len, size, vty->width, vty->cmd_lines, size);
    #if 1
    //compatible for different terminal type
    if( (vty->terminal_type == TERM_TYPE_ANSI)||
        (vty->terminal_type == TERM_TYPE_linux)||
        (vty->terminal_type == TERM_TYPE_VT100)||
        (vty->terminal_type == TERM_TYPE_vt102)||
        (vty->terminal_type == TERM_TYPE_vt100)) {
        if (1  >= size ) {
            if((vty->cp+1) >= first_line_len)
            if (0 ==(((vty->cp+1) - first_line_len) % vty->width)) {
                VTY_PRINT("d1, size=%d\r\n", size);
                vty_write (vty, &telnet_space_char, 1);
                vty_out(vty, "\033[%dD", vty->width + 1);
                vty_out(vty, "\033[%dC", vty->width - 1);
                flag = 1;
            }
        }else if(size > 1){
            if(vty->length < first_line_len){
                al = 0;
            }else
                al = (vty->length - (first_line_len))/vty->width + 1;
            uplines = al - vty->cmd_lines ;
            VTY_PRINT("delete char ]ansi pro.al=%d,vty->cmd_lines=%d\r\n", al, vty->cmd_lines);
            //scroll back to cp 
            if(uplines){
                if(vty->cp == (first_line_len)){
                    VTY_PRINT("0 uplines=%d 22\r\n", uplines);
                    sit = 0;
                }else if(vty->cp < (first_line_len)){
                    sit = first_line_len - vty->cp - 1;
                }else{
                    VTY_PRINT("0 cp > first line len. \r\n");
                    sit = vty->width-(vty->cp - first_line_len)%vty->width-1;
                }
                vty_write (vty, &vty->buf[vty->cp], size - 1);
                vty_write (vty, &telnet_space_char, 1);
                
                vty_out(vty, "\033[%dA", uplines);  //up lines
                vty_out(vty, "\033[%dD", vty->width + 1);
                vty_out(vty, "\033[%dC", vty->width - 1);  //end line
                VTY_PRINT("0 sit=%d\r\n", sit);
                if(sit > vty->width){
                    VTY_PRINT("error sit=%d.\r\n", sit);
                    exit(0);
                }
                for (i = 0; i < sit; i++){
                    vty_write (vty, &telnet_backward_char, 1);
                }
                flag = 1;
            }
        }
        if (!flag) {
            vty_write (vty, &vty->buf[vty->cp], size - 1);
            vty_write (vty, &telnet_space_char, 1);
            
            if(((vty->length+1) == first_line_len)||
                (((vty->length+1)>first_line_len)&&(((vty->length+1)-first_line_len)%vty->width==0))){
                VTY_PRINT("no flag.\r\n");
                vty_out(vty, "\033[%dD", vty->width + 1);
                vty_out(vty, "\033[%dC", vty->width - 1);  //end line
                if(first_line_len > (vty->cp+1)){
                    sit = first_line_len - vty->cp - 1;
                }else{
                    sit = vty->width - (vty->cp-first_line_len)%vty->width - 1;
                }
            }else{
                sit = size;
            }
            if(sit > vty->width){
                VTY_PRINT("sit > vty->width. sit=%d\r\n", sit);
                exit(0);
            }
            for (i = 0; i < sit; i++)
                vty_write (vty, &telnet_backward_char, 1);
        }
    }else{
        VTY_PRINT("%s %d, size=%d, len=%d, cp=%d\r\n", __func__, __LINE__, vty->length, vty->cp);
        vty_write (vty, &vty->buf[vty->cp], size - 1);
        vty_write (vty, &telnet_space_char, 1);
        
        for (i = 0; i < size; i++)
          vty_write (vty, &telnet_backward_char, 1);
    }
    #endif
    
    VTY_PRINT ("length:%d, cp:%d, prompt:%d, size=%d,width=%d, line=%d\r\n",
            vty->length, vty->cp, vty->prompt_len, size, vty->width,
            vty->cmd_lines);
}

/* Delete a character before the point. */
static void
vty_delete_backward_char (struct vty *vty)
{
    if (vty->cp == 0)
        return;

    vty_backward_char (vty);
    vty_delete_char (vty);
}

/* Kill rest of line from current point. */
static void
vty_kill_line (struct vty *vty)
{
    int i;
    int size;
    int first_line_len = vty->width - vty->prompt_len;
    int cmd_lines = 0;

    size = vty->length - vty->cp;

    if (size == 0)
        return;
    VTY_PRINT("%s %d, size=%d, len=%d, cp=%d, width=%d, first_line_len=%d\r\n", __func__, __LINE__, 
                    size, vty->length, vty->cp, vty->width, first_line_len);

    if(TERM_TYPE_ANSI == vty->terminal_type){
        vty_out(vty, "\033[u");
        vty_out(vty, "\033[s");
    }
    if(size <= first_line_len){
        for (i = 0; i < size; i++){
            vty_write (vty, &telnet_space_char, 1);
        }
    }else{
        for (i = 0; i < size; i++){
            if((i == first_line_len)||((i-first_line_len)%vty->width==0)){
                //vty_out(vty, "\r\n");
            }
            vty_write (vty, &telnet_space_char, 1);
        }
    }
    if(TERM_TYPE_ANSI == vty->terminal_type){
        vty_out(vty, "\033[u");
    }else{
#if 1
        if(size < first_line_len){
            VTY_PRINT("%s %d, size=%d\r\n", __func__, __LINE__, size);
            for (i = 0; i < size; i++)
                vty_write (vty, &telnet_backward_char, 1);
        }else{
            VTY_PRINT("%s %d, size=%d, cmdline=%d\r\n", __func__, __LINE__, size, vty->cmd_lines);
            #if 0
            for (i = 0; i < size; i++){
                if((i > first_line_len)&&((i-first_line_len)%vty->width==0)){
                    //vty_out(vty, "\033[1A");
                    cmd_lines++;
                }
                //vty_write (vty, &telnet_backward_char, 1);
            }
            #endif
            cmd_lines = (i-first_line_len)/vty->width + 1;
            #if 1
            VTY_PRINT("+++++++++++++cmd_lines=%d\r\n", cmd_lines);
            vty_out(vty, "\033[%dD", vty->width);
            if((size  != first_line_len)&&((size-first_line_len)%vty->width != 0))
                vty_out(vty, "\033[%dA", cmd_lines);
            else if((size > first_line_len)&&((size-first_line_len)%vty->width == 0)){
                vty_out(vty, "\033[%dA", cmd_lines-1);
            }
            vty_out(vty, "\033[%dC", vty->prompt_len);
            //for(i = 0; i < first_line_len-1; i++){
            //    vty_write (vty, &telnet_backward_char, 1);
            //}
            #endif
        }
#endif
    }

    memset (&vty->buf[vty->cp], 0, size);
    vty->length = vty->cp;
}

#if 0
{
    int i;
    int size;

    size = vty->length - vty->cp;

    if (size == 0)
        return;

    for (i = 0; i < size; i++)
        vty_write (vty, &telnet_space_char, 1);
    for (i = 0; i < size; i++)
        vty_write (vty, &telnet_backward_char, 1);

    memset (&vty->buf[vty->cp], 0, size);
    vty->length = vty->cp;
}
#endif

/* Kill line from the beginning. */
static void
vty_kill_line_from_beginning (struct vty *vty)
{
    vty_beginning_of_line (vty);
    vty_kill_line (vty);
}

/* Delete a word before the point. */
static void
vty_forward_kill_word (struct vty *vty)
{
    while (vty->cp != vty->length && vty->buf[vty->cp] == ' ')
        vty_delete_char (vty);
    while (vty->cp != vty->length && vty->buf[vty->cp] != ' ')
        vty_delete_char (vty);
}

/* Delete a word before the point. */
static void
vty_backward_kill_word (struct vty *vty)
{
    while (vty->cp > 0 && vty->buf[vty->cp - 1] == ' ')
        vty_delete_backward_char (vty);
    while (vty->cp > 0 && vty->buf[vty->cp - 1] != ' ')
        vty_delete_backward_char (vty);
}

/* Transpose chars before or at the point. */
static void
vty_transpose_chars (struct vty *vty)
{
    char c1, c2;

    /* If length is short or point is near by the beginning of line then
       return. */
    if (vty->length < 2 || vty->cp < 1)
        return;

    /* In case of point is located at the end of the line. */
    if (vty->cp == vty->length) {
        c1 = vty->buf[vty->cp - 1];
        c2 = vty->buf[vty->cp - 2];

        vty_backward_char (vty);
        vty_backward_char (vty);
        vty_self_insert_overwrite (vty, c1);
        vty_self_insert_overwrite (vty, c2);
    }
    else {
        c1 = vty->buf[vty->cp];
        c2 = vty->buf[vty->cp - 1];

        vty_backward_char (vty);
        vty_self_insert_overwrite (vty, c1);
        vty_self_insert_overwrite (vty, c2);
    }
}

/* Do completion at vty interface. */
static int
vty_complete_command (struct vty *vty)
{
    int i;
    int ret, len = 0;
    char **matched = NULL;
    vector vline;

    if (vty->node == USER_NODE || vty->node == AUTH_NODE || vty->node == AUTH_ENABLE_NODE)
        return 0;

    vline = cmd_make_strvec (vty->buf);
    if (vline == NULL)
        return 0;

    /* In case of 'help \t'. */
    if (isspace ((int) vty->buf[vty->length - 1]))
        vector_set (vline, '\0');

    matched = cmd_complete_command (vline, vty, &ret);

    cmd_free_strvec (vline);

#ifndef _ENABLE_VTY_SPACE_EXPAND_   /* Start. Add by keith.gong. 20151022 */
    vty_out (vty, "%s", VTY_NEWLINE);
#endif /* End. Add by keith.gong. 20151022 */
    switch (ret) {
    case CMD_ERR_AMBIGUOUS:
        vty_out (vty, "%% Ambiguous command.%s", VTY_NEWLINE);
        vty_prompt (vty);
        vty_redraw_line (vty);
        break;
    case CMD_ERR_NO_MATCH:
        #if 0
        printf("%s %d,matched=%s, cp:%d, buf:%s\r\n"
                , __func__, __LINE__, matched[0], vty->cp, vty->buf);
        #endif
        /* vty_out (vty, "%% There is no matched command.%s", VTY_NEWLINE); */
#ifndef _ENABLE_VTY_SPACE_EXPAND_   /* Start. Add by keith.gong. 20151022 */
        vty_prompt (vty);
        vty_redraw_line (vty);
#endif
        break;
    case CMD_COMPLETE_FULL_MATCH:
        //stephen.liu add, if you don't wanna see it, delete it, say nothing to me.
        //20160831
        #if 0
        if(matched)
            printf("%s %d,matched=%s, cp:%d, buf:%s,len=%d\r\n"
                    , __func__, __LINE__
                    , matched[0], vty->cp, vty->buf, vty->length);
        else{
            printf("%s %d,cp:%d\r\n", __func__, __LINE__, vty->cp);
        }
        #endif
#ifdef _ENABLE_VTY_SPACE_EXPAND_    /* Start. Add by keith.gong. 20151022 */
        if (matched) {
            //stephen.liu, if full matched, we'll not redraw the last command string
            if(strcmp(&vty->buf[vty->length-strlen(matched[0])], matched[0])){
                len = vty->length;
                while(--len){
                    if(vty->buf[len] == ' ')
                        break;
                }
                //printf("len = %d, length=%d\r\n", len, vty->length);
                //vty_backward_pure_word (vty);
                if(len > 0){
                    vty_insert_word_overwrite (vty, matched[0]+(vty->length-len-1));
                }else{
                    vty_backward_pure_word (vty);
                    vty_insert_word_overwrite (vty, matched[0]);
                }
                
                /* reset cmd_lines , stephen.liu 20161125 */
                vty_reset_cmdlines(vty);
            }
            vty_end_of_line(vty);
            vty_self_insert (vty, ' ');  //stephen.liu
            _vty_flush(vty); //add by stephen.liu
            XFREE (MTYPE_TMP, matched[0]);
        }
        else{
            vty_end_of_line(vty);
            vty_self_insert (vty, ' ');
        }
#else
        vty_prompt (vty);
        vty_redraw_line (vty);
        vty_backward_pure_word (vty);
        vty_insert_word_overwrite (vty, matched[0]);
        vty_self_insert (vty, ' ');
        XFREE (MTYPE_TMP, matched[0]);
#endif
        break;
    case CMD_COMPLETE_MATCH:
        #if 0
        printf("%s %d,matched=%s, cp:%d, buf:%s\r\n"
                , __func__, __LINE__, matched[0], vty->cp, vty->buf);
        #endif
#ifndef _ENABLE_VTY_SPACE_EXPAND_   /* Start. Add by keith.gong. 20151022 */
        vty_prompt (vty);
        vty_redraw_line (vty);
#endif
		if (matched) {
            //stephen.liu, if full matched, we'll not redraw the last command string
            if(strcmp(&vty->buf[vty->length-strlen(matched[0])], matched[0])){
                len = vty->length;
                while(--len){
                    if(vty->buf[len] == ' ')
                        break;
                }
                //printf("len = %d, length=%d\r\n", len, vty->length);
                //vty_backward_pure_word (vty);
                if(len > 0){
                    vty_insert_word_overwrite (vty, matched[0]+(vty->length-len-1));
                }else{
                    vty_backward_pure_word (vty);
                    vty_insert_word_overwrite (vty, matched[0]);
                }
                
                /* reset cmd_lines , stephen.liu 20161125 */
                vty_reset_cmdlines(vty);
            }else{
                vty_backward_pure_word (vty);
                vty_insert_word_overwrite (vty, matched[0]);
            }
        }else{
	        vty_backward_pure_word (vty);
	        vty_insert_word_overwrite (vty, matched[0]);
		}
        //XFREE (MTYPE_TMP, matched[0]);
        vector_only_index_free (matched);
        return;
        break;
    case CMD_COMPLETE_LIST_MATCH:
        #if 0
        printf("%s %d,matched=%s, cp:%d, buf:%s\r\n"
                , __func__, __LINE__, matched[0], vty->cp, vty->buf);
        #endif
#ifdef _ENABLE_VTY_SPACE_EXPAND_    /* Start. Add by keith.gong. 20151022 */
        vty_out (vty, "%s", VTY_NEWLINE);
#endif
        for (i = 0; matched[i] != NULL; i++) {
            if (i != 0 && ((i % 6) == 0))
                vty_out (vty, "%s", VTY_NEWLINE);
            vty_out (vty, "%-10s ", matched[i]);
            XFREE (MTYPE_TMP, matched[i]);
        }
        vty_out (vty, "%s", VTY_NEWLINE);

        vty_prompt (vty);
        vty_redraw_line (vty);

        if (matched)
            vector_only_index_free (matched);
        return CMD_COMPLETE_LIST_MATCH;
        break;
    case CMD_ERR_NOTHING_TODO:
#ifndef _ENABLE_VTY_SPACE_EXPAND_   /* Start. Add by keith.gong. 20151022 */
        vty_prompt (vty);
        vty_redraw_line (vty);
#endif
        break;
    default:
        break;
    }

    if (matched)
        vector_only_index_free (matched);

    return 0;
}

static void
vty_describe_fold (struct vty *vty, int cmd_width, unsigned int desc_width, struct cmd_token *token)
{
    char *buf;
    const char *cmd, *p;
    int pos;

    cmd = token->cmd[0] == '.' ? token->cmd + 1 : token->cmd;

    if (desc_width <= 0) {
        vty_out (vty, " %-*s - %s%s", cmd_width, cmd, token->desc, VTY_NEWLINE);
        return;
    }

    buf = XCALLOC (MTYPE_TMP, strlen (token->desc) + 1);

    for (p = token->desc; strlen (p) > desc_width; p += pos + 1) {
        for (pos = desc_width; pos > 0; pos--)
            if (*(p + pos) == ' ')
                break;

        if (pos == 0)
            break;

        strncpy (buf, p, pos);
        buf[pos] = '\0';
        vty_out (vty, " %-*s %c %s%s", cmd_width, cmd, *cmd ? '-' : ' ', buf, VTY_NEWLINE);

        cmd = "";
    }

    vty_out (vty, " %-*s   %s%s", cmd_width, cmd, p, VTY_NEWLINE);

    XFREE (MTYPE_TMP, buf);
}

/* Describe matched command function. */
static void
vty_describe_command (struct vty *vty)
{
    int ret;
    vector vline;
    vector describe;
    unsigned int i, width, desc_width;
    struct cmd_token *token, *token_cr = NULL;

    vline = cmd_make_strvec (vty->buf);

	//stephen.liu add , 20161212, as describe is the last string
    if(vty->cp != vty->length)
        return ;
     //
    /* In case of '> ?'. */
    if (vline == NULL) {
        vline = vector_init (1);
        vector_set (vline, '\0');
    }
    else if (isspace ((int) vty->buf[vty->length - 1]))
        vector_set (vline, '\0');

    describe = cmd_describe_command (vline, vty, &ret);

    vty_out (vty, "%s", VTY_NEWLINE);

    /* Ambiguous error. */
    switch (ret) {
    case CMD_ERR_AMBIGUOUS:
        vty_out (vty, "%% Ambiguous command.%s", VTY_NEWLINE);
        goto out;
        break;
    case CMD_ERR_NO_MATCH:
        vty_out (vty, "%% There is no matched command.%s", VTY_NEWLINE);
        goto out;
        break;
    }

    /* Get width of command string. */
    width = 0;
    for (i = 0; i < vector_active (describe); i++)
        if ((token = vector_slot (describe, i)) != NULL) {
            unsigned int len;

            if (token->cmd[0] == '\0')
                continue;

            len = strlen (token->cmd);
            if (token->cmd[0] == '.')
                len--;

            if (width < len)
                width = len;
        }

    width = width < 20 ? 20 : width;    /* Add by keith.gong 20151022 */

    /* Get width of description string. */
    desc_width = vty->width - (width + 6);

    /* Print out description. */
    for (i = 0; i < vector_active (describe); i++)
        if ((token = vector_slot (describe, i)) != NULL) {
            if (token->cmd[0] == '\0')
                continue;

            if (strcmp (token->cmd, command_cr) == 0) {
                token_cr = token;
                continue;
            }

            if (!token->desc)
                vty_out (vty, " %-s%s", /* Modified by keith.gong 20151022 */
                         token->cmd[0] == '.' ? token->cmd + 1 : token->cmd, VTY_NEWLINE);
            else if (desc_width >= strlen (token->desc))
                vty_out (vty, " %-*s - %s%s", width,    /* Modified by keith.gong 20151022 */
                         token->cmd[0] == '.' ? token->cmd + 1 : token->cmd, token->desc, VTY_NEWLINE);
            else
                vty_describe_fold (vty, width, desc_width, token);

#if 0
            vty_out (vty, "  %-*s %s%s", width
                     desc->cmd[0] == '.' ? desc->cmd + 1 : desc->cmd, desc->str ? desc->str : "", VTY_NEWLINE);
#endif /* 0 */
        }

    if ((token = token_cr)) {
        if (!token->desc)
            vty_out (vty, " %-s%s", /* Modified by keith.gong 20151022 */
                     token->cmd[0] == '.' ? token->cmd + 1 : token->cmd, VTY_NEWLINE);
        else if (desc_width >= strlen (token->desc))
            vty_out (vty, " %-*s - %s%s", width,    /* Modified by keith.gong 20151022 */
                     token->cmd[0] == '.' ? token->cmd + 1 : token->cmd, token->desc, VTY_NEWLINE);
        else
            vty_describe_fold (vty, width, desc_width, token);
    }

    vty_out (vty, "%s", VTY_NEWLINE);

  out:
    cmd_free_strvec (vline);
    if (describe)
        vector_free (describe);

    vty_prompt (vty);
    vty_redraw_line (vty);
}

void
vty_clear_buf (struct vty *vty)
{
    memset (vty->buf, 0, vty->max);
}

/* ^C stop current input and do not add command line to the history. */
static void
vty_stop_input (struct vty *vty)
{
    vty->cp = vty->length = 0;
    vty_clear_buf (vty);
    vty_out (vty, "%s", VTY_NEWLINE);

#if 000
    switch (vty->node) {
    case VIEW_NODE:
    case ENABLE_NODE:
    case RESTRICTED_NODE:
        /* Nothing to do. */
        break;
    case CONFIG_NODE:
    case INTERFACE_NODE:
    case TEST_NODE:
    case INTERFACE_GE_NODE:
        vty_config_unlock (vty);
        vty->node = ENABLE_NODE;
        break;
    default:
        /* Unknown node, we have to ignore it. */
        break;
    }
#endif

    vty_prompt (vty);

    /* Set history pointer to the latest one. */
    vty->hp = vty->hindex;
}

/* Add current command line to the history buffer. */
static void
vty_hist_add (struct vty *vty)
{
    int index;
    int i = 0;

    if (vty->length == 0)
        return;

    index = vty->hindex ? vty->hindex - 1 : VTY_MAXHIST - 1;

    /* Ignore the same string as previous one. */
    if (vty->hist[index])
        if (strcmp (vty->buf, vty->hist[index]) == 0) {
            vty->hp = vty->hindex;
            return;
        }

    /* Insert history entry. */
    if (vty->hist[vty->hindex])
        XFREE (MTYPE_VTY_HIST, vty->hist[vty->hindex]);
    vty->hist[vty->hindex] = XSTRDUP (MTYPE_VTY_HIST, vty->buf);
    //vty->hist[vty->hindex][vty->length-1] = '\0';
    for(i = vty->length-1; i > 0; i--){
        if(vty->hist[vty->hindex][i] == ' '){
            vty->hist[vty->hindex][i] = '\0';
        }else if(vty->hist[vty->hindex][i])
            break;
    }
    VTY_PRINT("hist add--vty->length=%d, vty->buf:]%s[, srchist buf:]%s[\r\n"
                , vty->length, vty->buf, vty->hist[vty->hindex]);
    /* History index rotation. */
    vty->hindex++;
    if (vty->hindex == VTY_MAXHIST)
        vty->hindex = 0;

    vty->hp = vty->hindex;
}

/*  #define TELNET_OPTION_DEBUG*/

/* Get telnet window size. */
static int
vty_telnet_option (struct vty *vty, unsigned char *buf, int nbytes)
{
    int i;
#ifdef TELNET_OPTION_DEBUG

    for (i = 0; i < nbytes; i++) {
        switch (buf[i]) {
        case IAC:
            vty_out (vty, "IAC ");
            break;
        case WILL:
            vty_out (vty, "WILL ");
            break;
        case WONT:
            vty_out (vty, "WONT ");
            break;
        case DO:
            vty_out (vty, "DO ");
            break;
        case DONT:
            vty_out (vty, "DONT ");
            break;
        case SB:
            vty_out (vty, "SB ");
            break;
        case SE:
            vty_out (vty, "SE ");
            break;
        case TELOPT_ECHO:
            vty_out (vty, "TELOPT_ECHO %s", VTY_NEWLINE);
            break;
        case TELOPT_SGA:
            vty_out (vty, "TELOPT_SGA %s", VTY_NEWLINE);
            break;
        case TELOPT_NAWS:
            vty_out (vty, "TELOPT_NAWS %s", VTY_NEWLINE);
            break;
		case TELOPT_NEW_ENVIRON:
            vty_out (vty, "TELOPT_NEW_ENVIRON %s", VTY_NEWLINE);
            break;
        default:
            vty_out (vty, "%x ", buf[i]);
            break;
        }
    }
    vty_out (vty, "%s", VTY_NEWLINE);

#endif /* TELNET_OPTION_DEBUG */
    for (i = 0; i < nbytes; i++) {
        switch (buf[i]) {
		case TELOPT_TTYPE:
			//vty_out (vty, "[%d]TELOPT_NAWS %s", i, VTY_NEWLINE);
			if((vty->terminal_type == TERM_TYPE_NONE)&&(!vty->tflag)){
				vty_get_term_type(vty);
				vty->tflag = 1;
			}
			break;
		}
	}

    switch (buf[0]) {
    case SB:
        vty->sb_len = 0;
        vty->iac_sb_in_progress = 1;
        return 0;
        break;
    case SE:
        {
            if (!vty->iac_sb_in_progress)
                return 0;

            if ((vty->sb_len == 0) || (vty->sb_buf[0] == '\0')) {
                vty->iac_sb_in_progress = 0;
                return 0;
            }
            switch (vty->sb_buf[0]) {
            case TELOPT_TTYPE:
				//add by stephen.liu , 20161212
                #if 1
                for (i = 0; i < vty->sb_len; i++)
                    VTY_PRINT ("%x ", vty->sb_buf[i]);
                VTY_PRINT ("\n");
                #endif
                vty->sb_buf[vty->sb_len] = '\0';
                if(!memcmp(&vty->sb_buf[2], "ANSI", 4)||!memcmp(&vty->sb_buf[2], "ansi", 4)){
                    vty->terminal_type = TERM_TYPE_ANSI;
                    VTY_PRINT ("TERM_TYPE_ANSI\n");
                }else if(!memcmp(&vty->sb_buf[2], "linux", 5)){
                    VTY_PRINT ("TERM_TYPE_=%s\n", &vty->sb_buf[2]);
                    vty->terminal_type =  TERM_TYPE_linux;
                }else if(!memcmp(&vty->sb_buf[2], "VT100", 5)){
                    VTY_PRINT ("TERM_TYPE_=%s\n", &vty->sb_buf[2]);
                    vty->terminal_type =  TERM_TYPE_VT100;
                }else if(!memcmp(&vty->sb_buf[2], "vt102", 5)){
                    VTY_PRINT ("TERM_TYPE_=%s\n", &vty->sb_buf[2]);
                    vty->terminal_type =  TERM_TYPE_vt102;
                }else if(!memcmp(&vty->sb_buf[2], "vt100", 5)){
                    VTY_PRINT ("TERM_TYPE_=%s\n", &vty->sb_buf[2]);
                    vty->terminal_type =  TERM_TYPE_vt100;
                }else{
                    vty->terminal_type = TERM_TYPE_OTHER;
                    VTY_PRINT ("TERM_TYPE_OTHER\n");
                    VTY_PRINT ("TERM_TYPE_=%s\n", &vty->sb_buf[2]);
                }
                #if 0
				if((vty->sb_buf[2] == 'A')&&(vty->sb_buf[3] == 'N')
					&&(vty->sb_buf[4] == 'S')&&(vty->sb_buf[5] == 'I')){
					vty->terminal_type = TERM_TYPE_ANSI;
				}else{
					vty->terminal_type = TERM_TYPE_OTHER;
				}
                #endif
				break;
            case TELOPT_NAWS:
                #if 1
				vty->width = ((vty->sb_buf[1] << 8)|vty->sb_buf[2]);
				vty->height = ((vty->sb_buf[3] << 8)|vty->sb_buf[4]);
                #endif
                #if 0
                if (vty->sb_len != TELNET_NAWS_SB_LEN)
                    zlog_warn ("RFC 1073 violation detected: telnet NAWS option "
                               "should send %d characters, but we received %lu",
                               TELNET_NAWS_SB_LEN, (u_long) vty->sb_len);
                else if (sizeof (vty->sb_buf) < TELNET_NAWS_SB_LEN)
                    zlog_err ("Bug detected: sizeof(vty->sb_buf) %lu < %d, "
                              "too small to handle the telnet NAWS option",
                              (u_long) sizeof (vty->sb_buf), TELNET_NAWS_SB_LEN);
                else {
                    vty->width = ((vty->sb_buf[1] << 8) | vty->sb_buf[2]);
                    vty->height = ((vty->sb_buf[3] << 8) | vty->sb_buf[4]);
				#endif
#ifdef TELNET_OPTION_DEBUG
                    vty_out (vty, "TELNET NAWS window size negotiation completed: "
                             "width %d, height %d%s", vty->width, vty->height, VTY_NEWLINE);
#endif
                //}
                break;
            }
            vty->iac_sb_in_progress = 0;
            return 0;
            break;
        }
    default:
        break;
    }
    return 1;
}

execute_func_to_all_daemon g_func_to_all_daemon;
extern excute_func g_exec_daemon;

/* Execute current command line. */
int
vty_execute (struct vty *vty)
{
    int ret;

    ret = CMD_SUCCESS;

    switch (vty->node) {
    case USER_NODE:
    case AUTH_NODE:
    case AUTH_ENABLE_NODE:
        vty_auth (vty, vty->buf);
        break;
    default:
        ret = vty_command (vty, vty->buf);
        if (vty_is_terminal(vty))
        {
            /* record operation log */
            if (vty->length != 0)
            {
                if (!isalpha(vty->buf[0]) && !isspace(vty->buf[0]))
                {
                    break;
                }

                int cmdlen = vty->length;

                while(cmdlen > 0 && isspace(vty->buf[cmdlen - 1]))
                {
                    cmdlen--;
                }

                if (cmdlen == 0)
                {
                    break;
                }

                /* skip 'yes' and 'no' */
                if ((vty->buf[0] == 'y' && (strncmp(vty->buf, "y", cmdlen) == 0 || strncmp(vty->buf, "yes", cmdlen) == 0))
                    || (vty->buf[0] == 'n' && (strncmp(vty->buf, "n", cmdlen) == 0 || strncmp(vty->buf, "no", cmdlen) == 0)))
                {
                    break;
                }

                vty_hist_add (vty);
                VTY_PRINT("%s--vty->buf:]%s[, srchist buf:]%s[\r\n", __func__, vty->buf, vty->hist[vty->hindex]);

                /* ship 'show xxx' */
                if (vty->buf[0] == 's' && strncmp(vty->buf, "show", strlen("show")) == 0)
                {
                    break;
                }

                if ((vty->sign_user.level & ENUM_ACCESS_OP_MANUFACTURE) == 0)
                {
                    tflog_oper("[%s@%s] %s: %s", vty->address[0] ? vty->address : "unknown",
                            vty->sign_user.name, ret == CMD_SUCCESS ? "cmd" : "failure cmd", vty->buf);
                }
            }
        }
        break;
    }

    /* Clear command line buffer. */
    vty->cp = vty->length = 0;
    vty->cmd_lines = 0; //stephen.liu, 20160810
    vty_clear_buf (vty);

    if ((vty->status != VTY_CLOSE) && (vty->type != VTY_SHELL) && (vty->type != VTY_FILE)){
        vty_prompt (vty);
    }
    //at VTY_SHELL, we must clean online state in history
    //stephen.liu 20151014, add
    if ((vty->node == USER_NODE) && (vty->type == VTY_SHELL)) {
        vty_user_update_hist_state (vty->port, VTY_LOGOUT);
    }

    return ret;
}


vtysh_cmd_callback g_vtysh_exec_command;



#define CONTROL(X)  ((X) - '@')
#define VTY_NORMAL     0
#define VTY_PRE_ESCAPE 1
#define VTY_ESCAPE     2

/* Escape character command map. */
static void
vty_escape_map (unsigned char c, struct vty *vty)
{
    switch (c) {
    case ('A'):
        vty_previous_line (vty);
        break;
    case ('B'):
        vty_next_line (vty);
        break;
    case ('C'):
#if 0
        //stephen.liu 20160810
        printf("C:vty->cp=%d,vty->width=%d, vty->prompt_len=%d, vty->cmd_lines=%d\r\n"
            ,vty->cp, vty->width, vty->prompt_len, vty->cmd_lines);
		if(0 == vty->cmd_lines){
			if(0 == (vty->cp % (vty->width - vty->prompt_len)))
				vty_down_one_line(vty);
		}else{
			if(0 == ((vty->cp - (vty->width - vty->prompt_len)) % vty->width))
				vty_down_one_line(vty);
		}
#endif /* 0 */
        vty_forward_char (vty);
        break;
    case ('D'):
#if 0
        //stephen.liu 20160810
        //compatible for different terminal type
        printf("D:vty->cp=%d,vty->width=%d, vty->prompt_len=%d, vty->cmd_lines=%d\r\n"
            ,vty->cp, vty->width, vty->prompt_len, vty->cmd_lines);
        if(vty->terminal_type == TERM_TYPE_ANSI){
            if(vty->cmd_lines){
                if((0 == ((vty->cp - (vty->width - vty->prompt_len)) % vty->width))&&(vty->cp)){
                    vty_up_one_line(vty);
                    vty_cursor_to_end(vty);

                    //vty_backward_char (vty);
                    //vty_cursor_to_end(vty);
                }
            }
        }
#endif /* 0 */
        vty_backward_char (vty);
        break;
    default:
        break;
    }

    /* Go back to normal mode. */
    vty->escape = VTY_NORMAL;
}

/* Quit print out to the buffer. */
static void
vty_buffer_reset (struct vty *vty)
{
    buffer_reset (vty->obuf);
    vty_prompt (vty);
    vty_redraw_line (vty);
}

/* Read data via vty socket. */
static int
vty_read (struct thread *thread)
{
    int i,j=0,quotes=0;
    int nbytes;
    unsigned char buf[VTY_READ_BUFSIZ];
#if DEBUG_VTY
        char task_name[128];
#endif
    int rv = 0;

    int vty_sock = THREAD_FD (thread);
    struct vty *vty = THREAD_ARG (thread);
    vty->t_read = NULL;

    /* Read raw data from socket */
    if ((nbytes = read (vty->fd, buf, VTY_READ_BUFSIZ)) <= 0) {
        if (nbytes < 0) {
            if (ERRNO_IO_RETRY (errno)) {
                vty_event (VTY_READ, vty_sock, vty);
                return 0;
            }
            vty->monitor = 0;   /* disable monitoring to avoid infinite recursion */
            zlog_warn ("%s: read error on vty client fd %d, closing: %s", __func__, vty->fd, safe_strerror (errno));
        }

        buffer_reset (vty->obuf);
        vty->status = VTY_CLOSE;
        fprintf(stderr, "%d\r\n", __LINE__);
    }

    for (i = 0; i < nbytes; i++) {
        if (buf[i] == IAC) {
            if (!vty->iac) {
                vty->iac = 1;
                continue;
            }
            else {
                vty->iac = 0;
            }
        }

        if (vty->iac_sb_in_progress && !vty->iac) {
            if (vty->sb_len < sizeof (vty->sb_buf))
                vty->sb_buf[vty->sb_len] = buf[i];
            vty->sb_len++;
            continue;
        }

        if (vty->iac) {
            /* In case of telnet command */
            int ret = 0;
            ret = vty_telnet_option (vty, buf + i, nbytes - i);
            vty->iac = 0;
            i += ret;
            continue;
        }

        if (vty->status == VTY_MORE) {
            switch (buf[i]) {
            case CONTROL ('C'):
            case 'q':
            case 'Q':
                vty_buffer_reset (vty);
                break;
#if 0                           /* More line does not work for "show ip bgp".  */
            case '\n':
            case '\r':
                vty->status = VTY_MORELINE;
                break;
#endif
            default:
                break;
            }
            continue;
        }

        /* Escape character. */
        if (vty->escape == VTY_ESCAPE) {
            vty_escape_map (buf[i], vty);
            continue;
        }


        /* Pre-escape status. */
        if (vty->escape == VTY_PRE_ESCAPE) {
            switch (buf[i]) {
            case '[':
                ///vty->escape = VTY_ESCAPE;
                vty->escape = VTY_NORMAL;
                break;
            case 'b':
                vty_backward_word (vty);
                vty->escape = VTY_NORMAL;
                break;
            case 'f':
                vty_forward_word (vty);
                vty->escape = VTY_NORMAL;
                break;
            case 'd':
                vty_forward_kill_word (vty);
                vty->escape = VTY_NORMAL;
                break;
            case CONTROL ('H'):
            case 0x7f:
                vty_backward_kill_word (vty);
                vty->escape = VTY_NORMAL;
                break;
            default:
                vty->escape = VTY_NORMAL;
                break;
            }
            continue;
        }

        switch (buf[i]) {
        case CONTROL ('A'):
            vty_beginning_of_line (vty);
            break;
        case CONTROL ('B'):
#if 0
            //stephen.liu 20160810
            //compatible for different terminal type
            if(vty->terminal_type == TERM_TYPE_ANSI){
                if(vty->cmd_lines){
                    if((0 == ((vty->cp - (vty->width - vty->prompt_len)+1) % vty->width))&&(vty->cp)){
                        vty_up_one_line(vty);
                        vty_cursor_to_end(vty);

                    }
                }
            }
#endif /* 0 */
            vty_backward_char (vty);
            break;
        case CONTROL ('C'):
            vty_stop_input (vty);
            break;
        case CONTROL ('D'):
			//stephen.liu 20161212
            //vty_delete_char (vty);
            break;
        case CONTROL ('E'):
            vty_end_of_line (vty);
            break;
        case CONTROL ('F'):
            vty_forward_char (vty);
            break;
        case CONTROL ('H'):
        case 0x7f:
            vty_delete_backward_char (vty);
            break;
        case CONTROL ('K'):
            vty_kill_line (vty);
            break;
        case CONTROL ('N'):
            vty_next_line (vty);
            break;
        case CONTROL ('P'):
            vty_previous_line (vty);
            break;
        case CONTROL ('T'):
            //stephen.liu 20160810
            //vty_transpose_chars (vty);
            break;
        case CONTROL ('U'):
            vty_kill_line_from_beginning (vty);
            break;
        case CONTROL ('W'):
            vty_backward_kill_word (vty);
            break;
        case CONTROL ('Z'):
            //vty_end_config (vty);
            //stephen.liu , 20160810
            if(g_vtysh_flag)
                g_vtysh_exec_command (vty, "end");
            break;
            //1 Marked by stephen.liu 20151010
            //1 For in windows telnet, it send "\r\n", cause vty_out twice VTY_NEWLINE
            //case '\n':
        case '\r':
            if (vty->node != USER_NODE && vty->node != AUTH_NODE && vty->node != AUTH_ENABLE_NODE)
            {
                if (vty->length > 0 && vty->buf[vty->length - 1] != ' '){
                    rv = vty_complete_command (vty);
                    if(CMD_COMPLETE_LIST_MATCH == rv)
                        break;
                }
            }
            vty_clear_temp_history(vty);
            vty_end_of_line(vty);//stephen.liu add, 20161212
            vty_out (vty, "%s", VTY_NEWLINE);
            vty_execute (vty);
            break;
#ifdef _ENABLE_VTY_SPACE_EXPAND_    /* Start. Add by keith.gong. 20151022 */
        case ' ':
            quotes = 0;
            for(j=0;j<vty->length;j++){
                if(vty->buf[j] == '\"'){
                    quotes++;
                }
            }
            if(1 == (quotes%2)){
                vty_self_insert (vty, buf[i]);
                break;
            }
            if (vty->node == USER_NODE || vty->node == AUTH_NODE || vty->node == AUTH_ENABLE_NODE)
                vty_self_insert (vty, ' ');
            else {
                /* input auto complete */
                if (vty->cp > 0 && vty->buf[vty->cp - 1] != ' ' && vty->cp == vty->length)
                    vty_complete_command (vty);
                else /* Space already exist, or insert a space */
                    vty_self_insert (vty, ' ');
            }
            break;
#endif
        //add for \"STRING\", stephen.liu 20161011
        case '\"':
            //printf("%s %d, str_ctrl:%d\n", __func__, __LINE__, vty->string_ctrl);
            vty_self_insert (vty, buf[i]);
            break;
        case '\t':
            vty_complete_command (vty);         
            break;
        case '?':
            //for \"STRING\", stephen.liu 20161011
            quotes = 0;
            for(j=0;j<vty->length;j++){
                if(vty->buf[j] == '\"'){
                    quotes++;
                }
            }
            if(1 == (quotes%2)){
                vty_self_insert (vty, buf[i]);
                break;
            }
            if (vty->node == USER_NODE || vty->node == AUTH_NODE || vty->node == AUTH_ENABLE_NODE)
                vty_self_insert (vty, buf[i]);
            else
                vty_describe_command (vty);
            break;
        case '\033':
            if (i + 1 < nbytes && buf[i + 1] == '[') {
                vty->escape = VTY_ESCAPE;
                i++;
            }
            else
                vty->escape = VTY_PRE_ESCAPE;
            break;
        default:
            if (buf[i] > 31 && buf[i] < 127)
                vty_self_insert (vty, buf[i]);
            break;
        }
    }

    /* Check status. */
    if (vty->status == VTY_CLOSE)
        vty_close (vty);
    else {
        vty_event (VTY_WRITE, vty_sock, vty);
        vty_event (VTY_READ, vty_sock, vty);
    }

    return 0;
}

/* Flush buffer to the vty. */
static int
vty_flush (struct thread *thread)
{
    int erase;
    buffer_status_t flushrc;
    int vty_sock = THREAD_FD (thread);
    struct vty *vty = THREAD_ARG (thread);

    vty->t_write = NULL;

    /*fprintf(stderr, "thread=0x%p iswrite=%d\r\n", thread, FD_ISSET(vty->fd, &thread->master->writefd));*/

    /* Tempolary disable read thread. */
    if ((vty->lines == 0) && vty->t_read) {
        thread_cancel (vty->t_read);
        vty->t_read = NULL;
    }

    /* Function execution continue. */
    erase = ((vty->status == VTY_MORE || vty->status == VTY_MORELINE));
    //printf("%s %d, height=%d, vty->width=%d, lines=%d\r\n"
    //    , __func__, __LINE__, vty->height, vty->width, vty->lines); //stephen.liu 20160815

    /* N.B. if width is 0, that means we don't know the window size. */
    if ((vty->lines == 0) || (vty->width == 0)){
        flushrc = buffer_flush_available (vty->obuf, vty->fd);
    }else if (vty->status == VTY_MORELINE)
        flushrc = buffer_flush_window (vty->obuf, vty->fd, vty->width, 1, erase, 0);
    else{

        //printf("%s %d, height=%d\r\n", __func__, __LINE__, vty->height);
        //stephen.liu modified, 20160815
        flushrc = buffer_flush_window (vty->obuf, vty->fd, vty->width, vty->height, erase, 0);
        //flushrc = buffer_flush_window (vty->obuf, vty->fd, vty->width,
        //                               vty->lines >= 0 ? vty->lines : vty->height, erase, 0);
    }
    switch (flushrc) {
    case BUFFER_ERROR:
        vty->monitor = 0;       /* disable monitoring to avoid infinite recursion */
        zlog_warn ("buffer_flush failed on vty client fd %d, closing", vty->fd);
        buffer_reset (vty->obuf);
        vty_close (vty);
        return 0;
    case BUFFER_EMPTY:
        if (vty->status == VTY_CLOSE)
            vty_close (vty);
        else {
            vty->status = VTY_NORMAL;
            if (vty->lines == 0)
                vty_event (VTY_READ, vty_sock, vty);
        }
        break;
    case BUFFER_PENDING:
        /* There is more data waiting to be written. */
        //vty->status = VTY_MORELINE;
        vty->status = VTY_MORE;
        if (vty->lines == 0)
            vty_event (VTY_WRITE, vty_sock, vty);
        break;
    }

    return 0;
}

/* Flush buffer to the vty. */
int
_vty_flush (struct vty *vty)
{
    int erase;
    buffer_status_t flushrc;
    int vty_sock = vty->fd;

    if (vty->t_write)
    {
        thread_cancel (vty->t_write);
        vty->t_write = NULL;
    }

    /* Tempolary disable read thread. */
    if ((vty->lines == 0) && vty->t_read) {
        thread_cancel (vty->t_read);
        vty->t_read = NULL;
    }

    /* Function execution continue. */
    erase = ((vty->status == VTY_MORE || vty->status == VTY_MORELINE));
    //printf("%s %d, height=%d, vty->width=%d, lines=%d\r\n"
    //    , __func__, __LINE__, vty->height, vty->width, vty->lines);  //stephen.liu 20160815

    /* N.B. if width is 0, that means we don't know the window size. */
    if ((vty->lines == 0) || (vty->width == 0))
        flushrc = buffer_flush_available (vty->obuf, vty->fd);
    else if (vty->status == VTY_MORELINE)
        flushrc = buffer_flush_window (vty->obuf, vty->fd, vty->width, 1, erase, 0);
    else{
        flushrc = buffer_flush_window (vty->obuf, vty->fd, vty->width, vty->height, erase, 0);
        //flushrc = buffer_flush_window (vty->obuf, vty->fd, vty->width,
        //                               vty->lines >= 0 ? vty->lines : vty->height, erase, 0);
    }
    switch (flushrc) {
    case BUFFER_ERROR:
        vty->monitor = 0;       /* disable monitoring to avoid infinite recursion */
        vty->status = VTY_CLOSE;
        break;
    case BUFFER_EMPTY:
        if (vty->status != VTY_CLOSE)
        {
            vty->status = VTY_NORMAL;
            if (vty->lines == 0){
                if(vty->type != VTY_SHELL_SERV)
                    vty_event (VTY_READ, vty_sock, vty);
            }
        }
        break;
    case BUFFER_PENDING:
        /* There is more data waiting to be written. */
        vty->status = VTY_MORE;
        if (vty->lines == 0)
            vty_event (VTY_WRITE, vty_sock, vty);
        break;
    }

    return 0;
}


/* stephen.liu 20150930 */
/* print information to all vty */
int
vty_out_to_all (char *str)
{
    int i;
    struct vty *vty;

    if (NULL == str) {
        return -1;
    }

    if (!vtyvec)
    {
        return -1;
    }

    for (i = 0; i < vector_active (vtyvec); i++)
        if (((vty = vector_slot (vtyvec, i)) != NULL)) {
            //printf("vty:%d\n", vty->fd);
            //if(vty_user_check_print_log_flag(vty->sign_user.name,LOG_MODULE_DEBUG))
            if (vty->type != VTY_FILE && vty->type != VTY_SHELL_SERV)
            {
                /*vty_out (vty, "%s%s%s", VTY_NEWLINE, str, VTY_NEWLINE);*/
                vty_out (vty, "%s", str);
                _vty_flush (vty);
            }
        }

    return 0;
}

int
alarm_to_vty (char *str)
{
    int i;
    struct vty *vty;
    vtysh_client_info *vtysh_info = NULL;
    int alarmed = FALSE;

    if (NULL == str) {
        printf("str is null.\n");
        return -1;
    }

    if (vtyvec == NULL)
        return -1;

    for (i = 0; i < vector_active (vtyvec); i++)
        if (((vty = vector_slot (vtyvec, i)) != NULL)) {
            //fprintf(stderr, "vty:%d\n", vty->fd);
            if (vty->type == VTY_SHELL_SERV)
            {
                vtysh_info = (vtysh_client_info *) vty->vtysh_info;
                if (!alarmed && vtysh_info != NULL && vtysh_info->fd != -1)
                {
                    writen (vtysh_info->fd, (u_char *) str, strlen ((char *) str));
                    alarmed = TRUE;
                }
            }
            else if (vty->type != VTY_FILE)
            {
                vty_out (vty, "%s", str);
                _vty_flush (vty);
            }
        }

    return 0;
}


static void
vclient_close (vtysh_client_info * vclient)
{
    if (vclient->fd >= 0) {
        fprintf(stderr, "Warning: closing connection to %s because of an I/O error!\n", vclient->name);
        close (vclient->fd);
        vclient->fd = -1;
    }
}

/* If you want to add a daemon, you must add to here.
    stephen.liu
    20150930
    VTY_DAEMON_MAX
*/
vtysh_client_info g_vty_daemon[] = {
    {.fd = -1,.name = "test",.flag = VTYSH_TEST,.path = TEST_VTYSH_PATH}
    ,
    {.fd = -1,.name = "gtf",.flag = VTYSH_GTF,.path = GVTYSH_PATH}
    ,
    {.fd = -1,.name = "swAcl",.flag = VTYSH_QOSACL,.path = SWACL_VTYSH_PATH}
    ,
    {.fd = -1,.name = "layer2",.flag = VTYSH_LAYER2,.path = L2_VTYSH_PATH}
    ,
    {.fd = -1,.name = "rstp",.flag = VTYSH_RSTP,.path = RSTP_VTYSH_PATH}
    ,
    {.fd = -1,.name = "igmpsn",.flag = VTYSH_IGMPSN,.path = IGMPSN_VTYSH_PATH}
    ,
    {.fd = -1,.name = "dhcpsn",.flag = VTYSH_DHCPSN,.path = DHCPSN_VTYSH_PATH}
    ,
    {.fd = -1,.name = "lacp",.flag = VTYSH_LACP,.path = LACP_VTYSH_PATH}
    ,
    {.fd = -1,.name = "dot1x",.flag = VTYSH_DOT1X,.path = DOT1X_VTYSH_PATH}
    ,
    {.fd = -1,.name = "hal",.flag = VTYSH_HAL,.path = HAL_VTYSH_PATH}
    ,
    {.fd = -1,.name = "sysctrl",.flag = VTYSH_SYSCTRL,.path = SYSCTRL_VTYSH_PATH}
};

/* Making connection to protocol daemon. */
static int
vtysh_connect (vtysh_client_info * vclient)
{
    int ret;
    int sock, len;
    struct sockaddr_un addr;
    struct stat s_stat;
    /* Stat socket to see if we have permission to access it. */
    ret = stat (vclient->path, &s_stat);
    if (ret < 0 && errno != ENOENT) {
        fprintf(stderr, "vtysh_connect(%s): stat = %s\n", vclient->path, safe_strerror (errno));
        return -1;
        //exit(1);
    }

    if (ret >= 0) {
        if (!S_ISSOCK (s_stat.st_mode)) {
            fprintf(stderr, "vtysh_connect(%s): Not a socket\n", vclient->path);
            return -1;
        }

    }

    sock = socket (AF_UNIX, SOCK_STREAM, 0);
    if (sock < 0) {
//#ifdef DEBUG
        fprintf(stderr, "vtysh_connect(%s): socket = %s\n", vclient->path, safe_strerror (errno));
//#endif /* DEBUG */
        return -1;
    }

    memset (&addr, 0, sizeof (struct sockaddr_un));
    addr.sun_family = AF_UNIX;
    strncpy (addr.sun_path, vclient->path, strlen (vclient->path));
#ifdef HAVE_STRUCT_SOCKADDR_UN_SUN_LEN
    len = addr.sun_len = SUN_LEN (&addr);
#else
    len = sizeof (addr.sun_family) + strlen (addr.sun_path);
#endif /* HAVE_STRUCT_SOCKADDR_UN_SUN_LEN */

    ret = connect (sock, (struct sockaddr *) &addr, len);
    if (ret < 0) {
//#ifdef DEBUG
        fprintf(stderr, "vtysh_connect(%s): connect = %s\n", vclient->path, safe_strerror (errno));
//#endif /* DEBUG */
        close (sock);
        return -1;
    }

    /* close on exec */
    fcntl(sock, F_SETFD, fcntl(sock, F_GETFD) | FD_CLOEXEC);

#if 0
    if (set_nonblocking (sock) < 0) {
        fprintf(stderr, "vtysh_accept: could not set vty socket %d to non-blocking,"
                 " %s, closing", sock, safe_strerror (errno));
        close (sock);
        return -1;
    }
#endif
    vclient->fd = sock;


#if 0
    vty = vty_new ();
    vty->type = VTY_SHELL_CLIENT;
    vty->fd = sock;

    //stephen.liu 20151008
    vty_event (VTYSH_CONNECT, sock, vty);
#endif

    return 0;
}


/*
    stephen.liu 20150930
*/
int
vtysh_connect_all_daemon (void *client, int daemon_num)
{
    u_int i;
    int rc = 0;
    int matches = 0;
    vtysh_client_info *c = (vtysh_client_info *) client;

    if (NULL == client) {
        return -1;
    }

    for (i = 0; i < daemon_num; i++) {
        if (c[i].name && (c[i].flag != VTYSH_TEST)) {
            matches++;
            if (vtysh_connect (&c[i]) == 0)
                rc++;
        }
    }
    if (!matches)
        fprintf(stderr, "Error: no daemons match name %s!\n", c[i].name);
    return rc;
}

static char g_daemon_send_flag = FALSE;
static int g_daemon_send_fd = -1;

int
vtysh_client_daemon_execute (struct vty *vty, vtysh_client_info * vclient, const char *line)
{
    int ret;
    unsigned char buf[1001];
    int nbytes;
    int i;
    int numnulls = 0;
    fd_set g_set, c_set;
    int max = 0, n = 0;
    struct timeval timeout, ti;
    int timeout_num = 0;

    timeout.tv_sec = 10;
    timeout.tv_usec = 0;

    if (vclient->fd < 0)
        return CMD_SUCCESS;
    //printf("%s dst[%s]line:%s\r\n", __func__, vclient->name, line);
    ret = send (vclient->fd, line, strlen (line) + 1, 0);
    if (ret <= 0) {
        fprintf(stderr, "%s %d\r\n", __func__, __LINE__);
        vclient_close (vclient);
        return CMD_SUCCESS;
    }
    //printf("%s write size:%d\r\n", __func__, ret);

    FD_ZERO (&g_set);
    FD_SET (vclient->fd, &g_set);
    max = vclient->fd;

    while (1) {
        ti = timeout;
        c_set = g_set;
        n = select (max + 1, &c_set, NULL, NULL, &ti);
        if (n < 0) {
            if (EINTR == errno) {
                continue;
            }
            VTY_PRINT ("select error:%s\n", strerror (errno));
            continue;
        }
        //printf("%s %d, n=%d\r\n", __func__, __LINE__, n); //stephen.liu
        if(n == 0){
            timeout_num++;
            //stephen.liu 20151030
            //vclient_close (vclient);
            if(timeout_num == 1000){
                vty_out (vty, "   VTY daemons[%s] command excute time out!%s",vclient->name,VTY_NEWLINE);
                return CMD_SUCCESS;
            }
            usleep(100000);
            continue;
        }
        if(FD_ISSET(vclient->fd, &c_set)){
            nbytes = read (vclient->fd, buf, sizeof (buf) - 1);

            if (nbytes <= 0 && errno != EINTR) {
                fprintf(stderr, "%s %d\r\n", __func__, __LINE__);
                vclient_close (vclient);
                return CMD_SUCCESS;
            }

            if (nbytes > 0) {
                //check first three bytes, if 0xff 0x55 0xff , it will wait for input
                //stephen.liu 20151028

#if VTYSH_DEBUG
                if(g_vtysh_init_complete){
                    dump_hex_value(__func__, buf, nbytes);
                }
#endif
                if(buf[0] == 0xff && buf[1] == 0x55 && buf[2] == 0xff){
                    //printf("active recive specify data.\n");
                    //stephen.liu 20151029, for send data to daemons
                    g_active.fd = vty->fd;
                    g_active.flag = TRUE;
                    continue;
                }
                if(buf[nbytes-3] == 0xff && buf[nbytes-2] == 0x55 && buf[nbytes-1] == 0xff){
                    //printf("active recive specify data.end \n");
                    g_active.fd = vty->fd;
                    g_active.flag = TRUE;
                    buf[nbytes-3] = '\0';
                    buf[nbytes-2] = '\0';
                    buf[nbytes-1] = '\0';

                }

                if ((numnulls == 3) && (nbytes == 1)){
                    //printf("command none error\n");
                    return buf[0];
                }
                buf[nbytes] = '\0';
                //fputs (buf, stdout);  //stpehen, 20150929
                //fflush (stdout);
                vty_out (vty, "%s", buf);
                //_vty_flush(vty);  //stephen.liu. 20160815
                //printf("%s %d, nbytes=%d\r\n", __func__, __LINE__, nbytes);
                /* check for trailling \0\0\0<ret code>,
                 * even if split across reads
                 * (see lib/vty.c::vtysh_read)
                 */
                if (nbytes >= 4) {
                    i = nbytes - 4;
                    numnulls = 0;
                }
                else
                    i = 0;

                while (i < nbytes && numnulls < 3) {
                    if (buf[i++] == '\0')
                        numnulls++;
                    else
                        numnulls = 0;
                }

                /* got 3 or more trailing NULs? */
                if ((numnulls >= 3) && (i < nbytes)){
                    //printf("command over????\n");
                    g_active.fd = -1;
                    g_active.flag = FALSE;
                    return (buf[nbytes - 1]);
                }
            }
        }
        //usleep(100000); //stephen.liu 20160817

    }
}

/*
    stephen.liu 20151016
    change parameter
    fixed vtysh parameter error bug.
*/
int
vtysh_execute_func_to_daemon (struct vty *vty, struct cmd_element *cmd, int argc, const char **argv)
{
    int i;
    int cmd_stat;
    char statStr[4] = {0};
    const char **argvnew = NULL;
    vtysh_client_info *c = (vtysh_client_info *) vty->vtysh_info;

    cmd_stat = CMD_SUCCESS;
    for (i = 0; i < vty->daemon_num; i++) {
        if (cmd->daemon & c[i].flag) {
            //printf("%s %d\r\n", __func__, __LINE__); //stephen.liu
            cmd_stat = vtysh_client_daemon_execute (vty, &c[i], vty->buf);
            if (cmd_stat != CMD_SUCCESS && cmd_stat != CMD_SUCCESS_INTERACTION)
                return cmd_stat;
            //printf("%s %d\r\n", __func__, __LINE__); //stephen.liu
        }
    }
    if (cmd_stat != CMD_SUCCESS && cmd_stat != CMD_SUCCESS_INTERACTION)
        return cmd_stat;

    //printf("%s %d\r\n", __func__, __LINE__); //stephen.liu
    if (cmd->func) {
        snprintf(statStr, sizeof(statStr), "%d", cmd_stat);
        argc++;
        argvnew = (const char **)XCALLOC(MTYPE_VTY, sizeof(const char *) * argc);
        if (argvnew == NULL)
            return CMD_ERR_NOTHING_TODO;
        memcpy(argvnew, argv, sizeof(const char *) * (argc - 1));
        argvnew[argc - 1] = statStr;
        cmd_stat = (*cmd->func) (cmd, vty, argc, argvnew);
        XFREE(MTYPE_VTY, argvnew);
        //printf("%s %d\r\n", __func__, __LINE__); //stephen.liu
        return cmd_stat;
    }
    else
        return cmd_stat;
}

void
init_vty_all_daemon (struct vty *vty)
{
    vtysh_client_info *vtysh_info = NULL;
    int i;

    vty->vtysh_info = XMALLOC (MTYPE_VTYSH_VTY, array_size (g_vty_daemon) * sizeof (vtysh_client_info));
    if (vty->vtysh_info) {
        vty->daemon_num = array_size (g_vty_daemon);
        vtysh_info = (vtysh_client_info *) vty->vtysh_info;
        for (i = 0; i < vty->daemon_num; i++) {
            vtysh_info[i].fd = g_vty_daemon[i].fd;
            vtysh_info[i].name = XSTRDUP (MTYPE_VTYSH_VTY, g_vty_daemon[i].name);
            vtysh_info[i].flag = g_vty_daemon[i].flag;
            vtysh_info[i].path = XSTRDUP (MTYPE_VTYSH_VTY, g_vty_daemon[i].path);

        }

        //printf("daemon_num=%d, name:%s\n", vty->daemon_num, g_vty_daemon[0].name);
        //memcpy(vty->vtysh_info, (void *)g_vty_daemon, array_size(g_vty_daemon));
    }
    //printf("debug:%d\n", __LINE__);
    //g_vty_daemon
    if (vtysh_connect_all_daemon (vty->vtysh_info, array_size (g_vty_daemon)) < 0) {
        vty_out (vty, "%sConnect Damon error.Please reboot!!!%s%s", VTY_NEWLINE, VTY_NEWLINE, VTY_NEWLINE);
    }

}

/* Create new vty structure. */
static struct vty *
vty_create (int vty_sock, union sockunion *su)
{
    char buf[SU_ADDRSTRLEN];
    struct vty *vty;
    union sockunion *suLocal;

    /* Allocate new vty structure and set up default values. */
    vty = vty_new ();
    vty->fd = vty_sock;

    sockunion2str (su, buf, SU_ADDRSTRLEN);
    
    vty->type = VTY_TERM;
    strncpy (vty->address, buf, sizeof(vty->address));
    
    /* Add by keith.gong. 20161012. 根据telnet server ip 判断连接类型*/
    suLocal = sockunion_getsockname (vty_sock);
    if(suLocal)
    {
        memset(buf, 0x00, sizeof(buf));
        sockunion2str (suLocal, buf, SU_ADDRSTRLEN);
        if (strcmp(buf, "127.0.0.1") == 0)
        {
            vty->type = VTY_TERM_LOCAL;
            memset(vty->address, 0x00, sizeof(vty->address));
            snprintf(vty->address, sizeof(vty->address), "Console");
        }
        else if (strcmp(buf, "127.0.0.2") == 0)
        {
            vty->type = VTY_SSH;
        }
    }

    //add by stephen.liu 20151013
    vty->port = su->sin.sin_port;

    if (no_password_check) {
        if (restricted_mode)
            vty->node = RESTRICTED_NODE;
        else if (host.advanced)
            vty->node = ENABLE_NODE;
        else
            vty->node = VIEW_NODE;
    }
    else
        vty->node = AUTH_NODE;

    vty->node = USER_NODE;      //stephen.liu add, 20151012

    vty->fail = 0;
    vty->cp = 0;
    vty_clear_buf (vty);
    vty->length = 0;
    memset (vty->hist, 0, sizeof (vty->hist));
    vty->hp = 0;
    vty->hindex = 0;
    vector_set_index (vtyvec, vty_sock, vty);
    vty->status = VTY_NORMAL;
    if (vty->type == VTY_TERM_LOCAL)
        vty->v_timeout = 0;
    else
        vty->v_timeout = 30;/* 30s */
    if (host.lines >= 0)
        vty->lines = host.lines;
    else
        vty->lines = -1;

	vty->cmd_lines = 0;
	vty->terminal_type = TERM_TYPE_NONE;
	vty->tflag = 0;

	vty->iac = 0;
    vty->iac_sb_in_progress = 0;
    vty->sb_len = 0;
    vty->height = 25;  //default value, ANSI terminal will over set
    vty->width = 80;
    vty->new_temp_hist = TRUE;
    if (!no_password_check) {
        /* Vty is not available if password isn't set. */
        if (host.password == NULL && host.password_encrypt == NULL) {
            vty_out (vty, "Vty password is not set.%s", VTY_NEWLINE);
            vty->status = VTY_CLOSE;
            vty_close (vty);
            return NULL;
        }
    }

#if 0                           /* Modified by keith.gong 20151022 */
    /* Say hello to the world. */
    vty_hello (vty);
    if (!no_password_check)
        vty_out (vty, "%sUser Access Verification%s%s", VTY_NEWLINE, VTY_NEWLINE, VTY_NEWLINE);
#else
    vty_out (vty, "\033[2J%s%s", VTY_NEWLINE, VTY_NEWLINE);
#endif
	usleep(3000);  //stephen.liu , not mark please , 20160121

    /* Setting up terminal. */
    vty_will_echo (vty);
    vty_will_suppress_go_ahead (vty);

    //vty_dont_linemode (vty);
    vty_do_window_size (vty);
    /* vty_dont_lflow_ahead (vty); */
	//vty_send_environ(vty);
	vty_do_window_type (vty, DO);

    /* Stephen.Liu 20150930 */
    if (g_vtysh_flag) {
        //need init all daemon
        init_vty_all_daemon (vty);
    }
    else {
        vty->vtysh_info = NULL;
        vty->daemon_num = 0;
    }

    vty_prompt (vty);

    /* Add read/write thread. */
    vty_event (VTY_WRITE, vty_sock, vty);
    vty_event (VTY_READ, vty_sock, vty);

    return vty;
}

/* Accept connection from the network. */
static int
vty_accept (struct thread *thread)
{
    int vty_sock;
    union sockunion su;
    int ret;
    unsigned int on;
    int accept_sock;
    struct prefix *p = NULL;
    struct access_list *acl = NULL;
    char buf[SU_ADDRSTRLEN];
#if DEBUG_VTY
    char task_name[128];
#endif

    accept_sock = THREAD_FD (thread);

    /* We continue hearing vty socket. */
    vty_event (VTY_SERV, accept_sock, NULL);
#if DEBUG_VTY
    printf("%s process:%s\r\n", __func__, (char *)getNameByPid(getpid(), task_name));
#endif
    memset (&su, 0, sizeof (union sockunion));

    /* We can handle IPv4 or IPv6 socket. */
    vty_sock = sockunion_accept (accept_sock, &su);
    if (vty_sock < 0) {
        zlog_warn ("can't accept vty socket : %s", safe_strerror (errno));
        return -1;
    }
    set_nonblocking (vty_sock);

    /* close on exec */
    fcntl(vty_sock, F_SETFD, fcntl(vty_sock, F_GETFD) | FD_CLOEXEC);

    p = sockunion2hostprefix (&su);

    /* VTY's accesslist apply. */
    if (p->family == AF_INET && vty_accesslist_name) {
        if ((acl = access_list_lookup (AFI_IP, vty_accesslist_name)) && (access_list_apply (acl, p) == FILTER_DENY)) {
            zlog (NULL, LOG_INFO, "Vty connection refused from %s", sockunion2str (&su, buf, SU_ADDRSTRLEN));
            close (vty_sock);

            /* continue accepting connections */
            vty_event (VTY_SERV, accept_sock, NULL);

            prefix_free (p);

            return 0;
        }
    }

#ifdef HAVE_IPV6
    /* VTY's ipv6 accesslist apply. */
    if (p->family == AF_INET6 && vty_ipv6_accesslist_name) {
        if ((acl = access_list_lookup (AFI_IP6, vty_ipv6_accesslist_name)) &&
            (access_list_apply (acl, p) == FILTER_DENY)) {
            zlog (NULL, LOG_INFO, "Vty connection refused from %s", sockunion2str (&su, buf, SU_ADDRSTRLEN));
            close (vty_sock);

            /* continue accepting connections */
            vty_event (VTY_SERV, accept_sock, NULL);

            prefix_free (p);

            return 0;
        }
    }
#endif /* HAVE_IPV6 */

    prefix_free (p);

    on = 1;
    ret = setsockopt (vty_sock, IPPROTO_TCP, TCP_NODELAY, (char *) &on, sizeof (on));
    if (ret < 0)
        zlog (NULL, LOG_INFO, "can't set sockopt to vty_sock : %s", safe_strerror (errno));

    zlog (NULL, LOG_INFO, "pid[%d] Vty connection from %s", getpid(), sockunion2str (&su, buf, SU_ADDRSTRLEN));

    vty_create (vty_sock, &su);

    return 0;
}

#ifdef HAVE_IPV6
static void
vty_serv_sock_addrinfo (const char *hostname, unsigned short port)
{
    int ret;
    struct addrinfo req;
    struct addrinfo *ainfo;
    struct addrinfo *ainfo_save;
    int sock;
    char port_str[BUFSIZ];

    memset (&req, 0, sizeof (struct addrinfo));
    req.ai_flags = AI_PASSIVE;
    req.ai_family = AF_UNSPEC;
    req.ai_socktype = SOCK_STREAM;
    sprintf(port_str, "%d", port);
    port_str[sizeof (port_str) - 1] = '\0';

    ret = getaddrinfo (hostname, port_str, &req, &ainfo);

    if (ret != 0) {
        fprintf(stderr, "getaddrinfo failed: %s\n", gai_strerror (ret));
        exit (1);
    }

    ainfo_save = ainfo;

    do {
        if (ainfo->ai_family != AF_INET
#ifdef HAVE_IPV6
            && ainfo->ai_family != AF_INET6
#endif /* HAVE_IPV6 */
            )
            continue;

        sock = socket (ainfo->ai_family, ainfo->ai_socktype, ainfo->ai_protocol);
        if (sock < 0)
            continue;

        sockopt_v6only (ainfo->ai_family, sock);
        sockopt_reuseaddr (sock);
        sockopt_reuseport (sock);

        ret = bind (sock, ainfo->ai_addr, ainfo->ai_addrlen);
        if (ret < 0) {
            close (sock);       /* Avoid sd leak. */
            continue;
        }

        ret = listen (sock, 3);
        if (ret < 0) {
            close (sock);       /* Avoid sd leak. */
            continue;
        }

        /* close on exec */
        fcntl(sock, F_SETFD, fcntl(sock, F_GETFD) | FD_CLOEXEC);

        vty_event (VTY_SERV, sock, NULL);
    }
    while ((ainfo = ainfo->ai_next) != NULL);

    freeaddrinfo (ainfo_save);
}
#else /* HAVE_IPV6 */

/* Make vty server socket. */
static void
vty_serv_sock_family (const char *addr, unsigned short port, int family)
{
    int ret;
    union sockunion su;
    int accept_sock;
    void *naddr = NULL;

    memset (&su, 0, sizeof (union sockunion));
    su.sa.sa_family = family;
    if (addr)
        switch (family) {
        case AF_INET:
            naddr = &su.sin.sin_addr;
            break;
#ifdef HAVE_IPV6
        case AF_INET6:
            naddr = &su.sin6.sin6_addr;
            break;
#endif
        }

    if (naddr)
        switch (inet_pton (family, addr, naddr)) {
        case -1:
            zlog_err ("bad address %s", addr);
            naddr = NULL;
            break;
        case 0:
            zlog_err ("error translating address %s: %s", addr, safe_strerror (errno));
            naddr = NULL;
        }

    /* Make new socket. */
    accept_sock = sockunion_stream_socket (&su);
    if (accept_sock < 0)
        return;

    /* This is server, so reuse address. */
    sockopt_reuseaddr (accept_sock);
    sockopt_reuseport (accept_sock);

    /* Bind socket to universal address and given port. */
    ret = sockunion_bind (accept_sock, &su, port, naddr);
    if (ret < 0) {
        zlog_warn ("can't bind socket");
        close (accept_sock);    /* Avoid sd leak. */
        return;
    }
    //printf("sockunion_bind ok. \n");
    /* Listen socket under queue 3. */
    ret = listen (accept_sock, 3);
    if (ret < 0) {
        zlog (NULL, LOG_WARNING, "can't listen socket");
        close (accept_sock);    /* Avoid sd leak. */
        return;
    }

    /* close on exec */
    fcntl(accept_sock, F_SETFD, fcntl(accept_sock, F_GETFD) | FD_CLOEXEC);

    /* Add vty server event. */
    vty_event (VTY_SERV, accept_sock, NULL);
}
#endif /* HAVE_IPV6 */

#ifdef VTYSH
/* For sockaddr_un. */
#include <sys/un.h>

/* VTY shell tcp domain socket. */
int
vty_serv_un_alarm (const char *path)
{
    int ret;
    int sock, len;
    struct sockaddr_un serv;
    mode_t old_mask;
    //struct zprivs_ids_t ids;

    /* First of all, unlink existing socket */
    unlink (path);

    /* Set umask */
    old_mask = umask (0007);

    /* Make UNIX domain socket. */
    sock = socket (AF_UNIX, SOCK_STREAM, 0);
    if (sock < 0) {
        zlog_err ("Cannot create unix stream socket: %s", safe_strerror (errno));
        return -1;
    }

    /* close on exec */
    fcntl(sock, F_SETFD, fcntl(sock, F_GETFD) | FD_CLOEXEC);

    /* Make server socket. */
    memset (&serv, 0, sizeof (struct sockaddr_un));
    serv.sun_family = AF_UNIX;
    strncpy (serv.sun_path, path, strlen (path));
#ifdef HAVE_STRUCT_SOCKADDR_UN_SUN_LEN
    len = serv.sun_len = SUN_LEN (&serv);
#else
    len = sizeof (serv.sun_family) + strlen (serv.sun_path);
#endif /* HAVE_STRUCT_SOCKADDR_UN_SUN_LEN */

    sockopt_reuseaddr (sock);   //stephen, 20150929

    ret = bind (sock, (struct sockaddr *) &serv, len);
    if (ret < 0) {
        zlog_err ("Cannot bind path %s: %s", path, safe_strerror (errno));
        close (sock);           /* Avoid sd leak. */
        return -1;
    }

    ret = listen (sock, 5);
    if (ret < 0) {
        zlog_err ("listen(fd %d) failed: %s", sock, safe_strerror (errno));
        close (sock);           /* Avoid sd leak. */
        return -1;
    }

    umask (old_mask);

#if 0
    zprivs_get_ids (&ids);

    if (ids.gid_vty > 0) {
        /* set group of socket */
        if (chown (path, -1, ids.gid_vty)) {
            zlog_err ("vty_serv_un_alarm: could chown socket, %s", safe_strerror (errno));
            return -1;
        }
    }
#endif

    return sock;
}

#define MAX_SIZE 1023

bool g_vty_init_alarm = FALSE;

#define CATEGORY_EN
#ifdef CATEGORY_EN
#if 0
static int msg_print(char *msg, int state)
{
    int idx;
    struct vty *vty = NULL;
    struct iovec iov[2];
    int retry = 3;
    #ifdef VTY_FLOWCTRL
    struct timespec tp;
    #endif

    if (NULL == msg) {
        return -1;
    }

    if (!vtyvec)/* vty may not have been initialised */
        return -1;

    if (g_vtysh_flag)
    {
        pthread_mutex_lock(&vty_mutex);
    }

    iov[0].iov_base = msg;
    iov[0].iov_len = strlen(msg);

    for (idx = 0; idx < vector_active (vtyvec); idx++)
    {
        if (((vty = vector_slot (vtyvec, idx)) != NULL))
        {
            //if(vty_user_check_print_log_flag(vty->sign_user.name,LOG_MODULE_DEBUG))
            if (vty->type != VTY_FILE && vty->type != VTY_SHELL_SERV)
            {
                if (state == MSG_STA_DEBUG && !vty->monitor)
                    continue;

                #ifdef VTY_FLOWCTRL
                clock_gettime (CLOCK_MONOTONIC, &tp);
                if (tp.tv_sec > vty->time_limit.tv_sec)
                {
                    vty->flowctrl = 0;
                    vty->time_limit = tp;
                }

                if ((vty->flowctrl + strlen(msg) + strlen(VTY_NEWLINE)) > 900)
                {
                    clock_gettime (CLOCK_MONOTONIC, &vty->time_limit);
                    continue;
                }
                #endif

                iov[1].iov_base = VTY_NEWLINE;
                iov[1].iov_len  = strlen(VTY_NEWLINE);

                retry = 3;
                while(writev(vty->fd, iov, 2) < 0 && ERRNO_IO_RETRY (errno) && retry > 0)
                {
                    fprintf(stderr, "%s call writev error: %s\r\n", __func__, safe_strerror(errno));
                    usleep(10);
                    retry--;
                }

                #ifdef VTY_FLOWCTRL
                vty->flowctrl += strlen(msg) + strlen(VTY_NEWLINE);
                #endif
                /*vty_out_line (vty, "%s", msg);*/
            }
        }
    }

    if (g_vtysh_flag)
    {
        pthread_mutex_unlock(&vty_mutex);
    }

    return 0;
}
#endif

static int msg_print(char *msg, int state)
{
    int idx;
    struct vty *vty = NULL;

    if (NULL == msg) {
        return -1;
    }

    if (!vtyvec)/* vty may not have been initialised */
        return -1;

    if (g_vtysh_flag)
    {
        pthread_mutex_lock(&vty_mutex);
    }

    for (idx = 0; idx < vector_active (vtyvec); idx++)
    {
        if (((vty = vector_slot (vtyvec, idx)) != NULL))
        {
            //if(vty_user_check_print_log_flag(vty->sign_user.name,LOG_MODULE_DEBUG))
            if (vty->type != VTY_FILE && vty->type != VTY_SHELL_SERV)
            {
                if (state == MSG_STA_DEBUG && !vty->debug)
                    continue;

                if ((buffer_data_size(vty->dbuf) + strlen(msg)) > (1024 * 1024))
                    continue;

                buffer_put(vty->dbuf, msg, strlen(msg));
                buffer_put(vty->dbuf, VTY_NEWLINE, strlen(VTY_NEWLINE));
                /*vty_out_line (vty, "%s", msg);*/
            }
        }
    }

    if (g_vtysh_flag)
    {
        pthread_mutex_unlock(&vty_mutex);
    }

    return 0;
}

static void msg_flush(void)
{
    int idx;
    struct vty *vty;
    int width = 0;
    int height = 0;
    struct timespec tp;
    size_t size = 0;

    if (!vtyvec)/* vty may not have been initialised */
        return;

    if (g_vtysh_flag)
    {
        pthread_mutex_lock(&vty_mutex);
    }

    for (idx = 0; idx < vector_active (vtyvec); idx++)
    {
        if (((vty = vector_slot (vtyvec, idx)) != NULL))
        {
            if (vty->type == VTY_TERM || vty->type == VTY_TERM_LOCAL || vty->type == VTY_SSH)
            {
                if (vty->width == 0)
                    width = 80;
                else
                    width = vty->width;

                height = vty->height;

                if (vty->type == VTY_TERM_LOCAL)
                {
                    clock_gettime (CLOCK_MONOTONIC, &tp);
                    if (tp.tv_sec > vty->time_limit.tv_sec)
                    {
                        vty->flowctrl = 0;
                        vty->time_limit = tp;
                    }

                    height = (960 - vty->flowctrl) / width;
                    if (height <= 0)
                    {
                        clock_gettime (CLOCK_MONOTONIC, &vty->time_limit);
                        continue;
                    }

                    size = buffer_data_size(vty->dbuf);
                }

                buffer_flush_window (vty->dbuf, vty->fd, width, height, 0, 1);

                if (vty->type == VTY_TERM_LOCAL)
                {
                    vty->flowctrl += size - buffer_data_size(vty->dbuf);
                }
            }
        }
    }

    if (g_vtysh_flag)
    {
        pthread_mutex_unlock(&vty_mutex);
    }

    return;
}

static int msg_category(char *buf, int *state)
{
    int offset = 1;

    if (strncmp(buf, "!</debug>", strlen("!</debug>")) == 0)
    {
        *state = MSG_STA_DEBUG;
        offset = 9;
    }

    else if (strncmp(buf, "!</alarm>", strlen("!</alarm>")) == 0)
    {
        *state = MSG_STA_ALARM;
        offset = 9;
    }
    else if (strncmp(buf, "!</>", strlen("!</>")) == 0)
    {
        *state = MSG_STA_SEC_END;
        offset = 4;
    }

    return offset;
}

#ifdef LOG_BUF_USED
static int msg_buf_print(char *buf, int len)
{
    int offset;
    int  msgsta = MSG_STA_NONE;
    int  prevMsgsta = MSG_STA_NONE;
    char msgbuf[MAX_SIZE + 1]; /* Make sure that there is no overflow */
    int  skips = 0;
    int  idx = 0;

    if (!buf)
        return -1;

    for (offset = 0; offset < len && buf[offset] != '\0';)
    {
        switch ((unsigned char)buf[offset])
        {
            case '!':
                prevMsgsta = msgsta;
                skips = msg_category(&buf[offset], &msgsta);
                if (skips == 1)
                {
                    msgbuf[idx++] = buf[offset];
                }
                else
                {
                    if (idx != 0)
                    {
                        msgbuf[idx] = '\0';
                        msg_print(msgbuf, prevMsgsta);
                        memset(msgbuf, 0, sizeof(msgbuf));
                        idx = 0;
                    }
                }
                offset += skips;
                break;
            case '\r':/* skip  */
                offset++;
                break;
            case '\n':
                msgbuf[idx++] = '\0';
                msg_print(msgbuf, msgsta);
                msgsta = MSG_STA_NONE;
                memset(msgbuf, 0, sizeof(msgbuf));
                idx = 0;
                offset++;
                break;
             default:
                msgbuf[idx++] = buf[offset++];
                break;
        }
    }

    return 0;
}

static void emit_log_char(char c)
{
    if (c == '\0')
    {
        return;
    }

	LOG_BUF(log_end) = c;
	log_end++;
	if (log_end - log_start > log_buf_len)
		log_start = log_end - log_buf_len;
}

static void emit_log(char *msg, int len)
{
	int idx;

    pthread_mutex_lock(&log_buf_mutex);

    for (idx = 0; idx < len; idx++)
    {
        emit_log_char(msg[idx]);
    }

    pthread_mutex_unlock(&log_buf_mutex);
}

#if 0
static void log_print_enable(char enable)
{
    log_print_en = enable ? TRUE : FALSE;
}
#endif

static int alarm_log_print(void)
{
    char c;
    char msg[1024] = {0};
    int  len_dst = 0;
    int  len_src = 0;
    int  ret = 0;
    char *msgend = NULL;
    char *msgStart = NULL;
    char  truncate = FALSE;
    char  crlf = '\n';

    if (!log_print_en)
    {
        return ret;
    }

    pthread_mutex_lock(&log_buf_mutex);
    if (log_start == log_end)
    {
        pthread_mutex_unlock(&log_buf_mutex);
        return ret;
    }

    if (log_start_prev != log_start && (log_end - log_start) == log_buf_len)
    {
        truncate = TRUE;
    }

    /*fprintf(stderr, "log_start=%08x  log_end=%08x\n", log_start, log_end);*/
    if ((log_start & (~LOG_BUF_MASK)) != (log_end & (~LOG_BUF_MASK)))
    {
        c = LOG_BUF(__LOG_BUF_LEN - 1);
        LOG_BUF(__LOG_BUF_LEN - 1) = '\0';
        snprintf(msg, sizeof(msg), "%s%c", &LOG_BUF(log_start), c);
        LOG_BUF(__LOG_BUF_LEN - 1) = c;
        log_start += strlen(msg);
    }

    len_dst = strlen(msg);
    if ((log_start & (~LOG_BUF_MASK)) == (log_end & (~LOG_BUF_MASK)))
    {
        LOG_BUF(log_end) = '\0';
        len_src = sizeof(msg) - len_dst - 1;
        if (len_src != 0)
        {
            strncat(msg, &LOG_BUF(log_start), len_src);
        }
    }


    msgend = strrchr(msg, crlf);
    if (msgend != NULL)
    {
        *(msgend+1) = '\0';
    }

    log_start += strlen(msg) - len_dst;
    log_start_prev = log_start;
    pthread_mutex_unlock(&log_buf_mutex);

    if (truncate)
    {
        msgStart = strchr(msg, crlf);
        if(msgStart != NULL)
        {
            msgStart++;
        }
    }

    if (msgStart == NULL)
    {
        msgStart = msg;
    }

    msg_buf_print(msgStart, strlen(msgStart));

    pthread_mutex_lock(&log_buf_mutex);
    ret = (log_start != log_end);
    pthread_mutex_unlock(&log_buf_mutex);

    return ret;
}

void cli_alarm_log_print(void *arg)
{
    int isempty = 0;

    while(1)
    {
        if (alarm_log_print())
            isempty = 0;
        else
            isempty = 1;

        msg_flush();

        if (isempty)
        {
            /* 让出CPU，响应输入 */
            usleep(10000);
        }
    }
}

#endif

#endif

extern int errno;
void
thread_process_alarm_recv (void)
{
    int serv_sock = 0;
    int accept_sock = 0, max, i;
    fd_set g_set, c_set;
    int n = 0;
    int nbytes;
    vector bufvec = NULL;
    char *buf = NULL;
    int  offset = 0;
    #ifdef CATEGORY_EN
    int  msgsta = MSG_STA_NONE;
    char msgbuf[MAX_SIZE + 1]; /* Make sure that there is no overflow */
    char *msgstart = NULL;
    #ifndef LOG_BUF_USED
    int  prevMsgsta = MSG_STA_NONE;
    int  skips = 0;
    #endif
    int  idx = 0;
    #endif

    serv_sock = vty_serv_un_alarm (ALARM_VTYSH_PATH);
    if (serv_sock < 0) {
        printf("start alarm thread error.\n");
        //system ("reboot");
        return;
    }
    //printf("start vty alarm server.\n");
    g_vty_init_alarm = TRUE;
    FD_ZERO (&g_set);
    FD_SET (serv_sock, &g_set);
    max = serv_sock;

    bufvec = vector_init(0);
    if (bufvec == NULL)
    {
        fprintf(stderr, "Message buffer allocation failure\n");
        return;
    }

    while (1) {
        c_set = g_set;
        n = select (max + 1, &c_set, NULL, NULL, 0);
        if (n < 0) {
            if (EINTR == errno) {
                continue;
            }
            printf("select error:%s\n", strerror (errno));
            continue;
        }
        for (i = 3; i < max + 1; i++) {
            if (FD_ISSET (i, &c_set)) {
                if (i == serv_sock) {
                    //printf("fd set.\n");
                    //accept
                    accept_sock = accept (serv_sock, NULL, NULL);
                    if (accept_sock > 0) {
                        FD_SET (accept_sock, &g_set);
                        max = (max > accept_sock) ? max : accept_sock;
                        buf = XCALLOC (MTYPE_TMP, MAX_SIZE + 1);
                        if (buf != NULL)
                        {
                            vector_ensure(bufvec, accept_sock);
                            vector_set_index(bufvec, accept_sock, buf);
                        }
                        else
                        {
                            close(accept_sock);
                        }

                        /* close on exec */
                        fcntl(accept_sock, F_SETFD, fcntl(accept_sock, F_GETFD) | FD_CLOEXEC);

                        //printf("new accept sock:%d,max=%d\n"
                        //    , accept_sock, max);
                        usleep (20000);
                        break;
                    }
                }
                else {

                    /* get buffer */
                    buf = vector_lookup(bufvec, i);
                    if (buf == NULL)
                    {
                        continue;
                    }

                    offset = strlen(buf);

                    /* receive data */
                    nbytes = read (i, buf + offset, MAX_SIZE - offset);
                    buf[MAX_SIZE] = '\0';/* Make sure that there is no overflow */
                    //fprintf(stderr, "start read data.====nbytes=%d offset=%d\n", nbytes, offset);

                    /* end of the file */
                    if (nbytes == 0) {
                        XFREE(MTYPE_TMP, buf);
                        vector_unset(bufvec, i);
                        close (i);
                        FD_CLR (i, &g_set);
                        continue;
                    }

                    /* error */
                    if (nbytes < 0) {
                        if (!ERRNO_IO_RETRY (errno))
                        {
                            XFREE(MTYPE_TMP, buf);
                            vector_unset(bufvec, i);
                            close (i);
                            FD_CLR (i, &g_set);
                        }
                        continue;
                    }

                    #ifdef CATEGORY_EN
                    /* category */
                    idx = 0;
                    nbytes += offset;
                    msgsta = MSG_STA_NONE;
                    for(msgstart = buf, offset = 0; offset < nbytes;)
                    {
                        switch ((unsigned char)buf[offset])
                        {
                            #ifndef LOG_BUF_USED /* handle it before send */
                            case '!':
                                prevMsgsta = msgsta;
                                skips = msg_category(&buf[offset], &msgsta);
                                if (skips == 1)
                                {
                                    msgbuf[idx++] = buf[offset];
                                }
                                else
                                {
                                    if (idx != 0)
                                    {
                                        msgbuf[idx] = '\0';
                                        msg_print(msgbuf, prevMsgsta);
                                        memset(msgbuf, 0, sizeof(msgbuf));
                                        idx = 0;
                                    }
                                }
                                offset += skips;
                                break;
                            #endif
                            case '\r':/* skip  */
                                offset++;
                                break;
                            case '\n':

                                #ifdef LOG_BUF_USED
                                msgbuf[idx++] = buf[offset];
                                msgbuf[idx++] = '\0';
                                emit_log(msgbuf, idx);
                                pthread_mutex_unlock(&log_mutex);
                                #else
                                msgbuf[idx++] = '\0';
                                msg_print(msgbuf, msgsta);
                                msgsta = MSG_STA_NONE;
                                #endif
                                memset(msgbuf, 0, sizeof(msgbuf));
                                idx = 0;
                                offset++;
                                msgstart = buf + offset;
                                break;
                            case 0xFF:
                                /*fprintf(stderr, "offset=%d nbytes=%d\r\n", offset, nbytes);*/
                                if ((offset + 2) <= nbytes && (unsigned char)buf[offset+1] == 0x55 && (unsigned char)buf[offset+2] == 0xFF)
                                {
                                    /*fprintf(stderr, "%s:%d\r\n", __FUNCTION__, __LINE__);*/
                                    g_daemon_send_fd = i;
                                    g_daemon_send_flag = TRUE;
                                    offset += 3;
                                }
                                else
                                {
                                    msgbuf[idx++] = buf[offset++];
                                }
                                break;
                             default:
                                msgbuf[idx++] = buf[offset++];
                                break;
                        }
                    }

                    if (msgstart != buf + offset)
                    {
                        snprintf(buf, MAX_SIZE, "%s", msgstart);
                    }
                    else
                    {
                        memset(buf, 0, MAX_SIZE);
                    }

                    if (1 == 0)
                        msg_flush();
                    #endif

                    //to all vty
                    //printf("recv:%x %x %x \n", buf[0], buf[1], buf[2]);
                    //vty_out_to_all ((char*)buf);
                }
                usleep (20000);
            }
        }
    }
}

void create_thread_no_arg (void *func)
{
    pthread_t thread_id;

    pthread_create (&thread_id, NULL, func, NULL);
}


/* VTY shell UNIX domain socket. */
static void
vty_serv_un (const char *path)
{
    int ret;
    int sock, len;
    struct sockaddr_un serv;
    mode_t old_mask;
    //struct zprivs_ids_t ids;

    // TODO: stephen.liu 20151010

    /* First of all, unlink existing socket */
    unlink (path);

    /* Set umask */
    old_mask = umask (0007);

    /* Make UNIX domain socket. */
    sock = socket (AF_UNIX, SOCK_STREAM, 0);
    if (sock < 0) {
        zlog_err ("Cannot create unix stream socket: %s", safe_strerror (errno));
        return;
    }

    /* Make server socket. */
    memset (&serv, 0, sizeof (struct sockaddr_un));
    serv.sun_family = AF_UNIX;
    strncpy (serv.sun_path, path, strlen (path));
#ifdef HAVE_STRUCT_SOCKADDR_UN_SUN_LEN
    len = serv.sun_len = SUN_LEN (&serv);
#else
    len = sizeof (serv.sun_family) + strlen (serv.sun_path);
#endif /* HAVE_STRUCT_SOCKADDR_UN_SUN_LEN */

    sockopt_reuseaddr (sock);   //stephen, 20150929

    ret = bind (sock, (struct sockaddr *) &serv, len);
    if (ret < 0) {
        zlog_err ("Cannot bind path %s: %s", path, safe_strerror (errno));
        close (sock);           /* Avoid sd leak. */
        return;
    }

    ret = listen (sock, 5);
    if (ret < 0) {
        zlog_err ("listen(fd %d) failed: %s", sock, safe_strerror (errno));
        close (sock);           /* Avoid sd leak. */
        return;
    }

    umask (old_mask);

#if 0
    zprivs_get_ids (&ids);

    if (ids.gid_vty > 0) {
        /* set group of socket */
        if (chown (path, -1, ids.gid_vty)) {
            zlog_err ("vty_serv_un: could chown socket, %s", safe_strerror (errno));
        }
    }
#endif

    /* close on exec */
    fcntl(sock, F_SETFD, fcntl(sock, F_GETFD) | FD_CLOEXEC);
    VTY_PRINT("%s %d path:%s\r\n", __func__, __LINE__, path); //stephen.liu
    vty_event (VTYSH_SERV, sock, NULL);
}


static int
vtysh_accept (struct thread *thread)
{
    int accept_sock;
    int sock;
    int client_len;
    /*vtysh_client_info *vtysh_info = NULL;*/
    //int i = 0;
    //struct sockaddr_un client;
    struct sockaddr client;     //stephen, 20150929
    struct vty *vty;
#if DEBUG_VTY
    char task_name[128];

    VTY_PRINT("%s process:%s\r\n", __func__, (char *)getNameByPid(getpid(), task_name));
#endif

    accept_sock = THREAD_FD (thread);

    vty_event (VTYSH_SERV, accept_sock, NULL);

    //memset (&client, 0, sizeof (struct sockaddr_un));
    //client_len = sizeof (struct sockaddr_un);
    memset (&client, 0, sizeof (struct sockaddr));
    client_len = sizeof (struct sockaddr);

    sock = accept (accept_sock, (struct sockaddr *) &client, (socklen_t *) & client_len);

    if (sock < 0) {
        zlog_warn ("can't accept vty socket : %s", safe_strerror (errno));
        return -1;
    }
    //printf("accept new socket.%d\n", sock);

    //stephen, 20150929
    //unlink (client.sun_path);

    if (set_nonblocking (sock) < 0) {
        zlog_warn ("vtysh_accept: could not set vty socket %d to non-blocking,"
                   " %s, closing", sock, safe_strerror (errno));
        close (sock);
        return -1;
    }

    /* close on exec */
    fcntl(sock, F_SETFD, fcntl(sock, F_GETFD) | FD_CLOEXEC);

#ifdef VTYSH_DEBUG
#ifdef DEBUG_VTY
    VTY_PRINT ("[%s]VTY shell accept\n", task_name);
#endif
#endif /* VTYSH_DEBUG */

    vty = vty_new ();
    vty->fd = sock;
    vty->type = VTY_SHELL_SERV;
    vty->node = VIEW_NODE;      //stephen.liu 20151013, change view node to enable
    //stephen.liu add, 20160925
    if(g_vtysh_flag){
        vty->sign_user.level = CLI_ACCESS_ROOT;   //stephen.liu 20160817
        snprintf(vty->sign_user.name, sizeof(vty->sign_user.name), "simVty");
        vty->v_timeout = 0;
    }

    //stephen.liu 20151008, for alarm to vtysh main
    vector_set_index (vtyvec, sock, vty);

    /* stephen.liu , 20151008 */
    /* connect to alarm server */
    if ((alarm_info == NULL)&&(!g_vtysh_flag))
    {
        alarm_info = XMALLOC (MTYPE_VTYSH_VTY, sizeof (vtysh_client_info));
        if (alarm_info)
        {
            alarm_info->fd = -1;
            alarm_info->name = XSTRDUP (MTYPE_VTYSH_VTY, "alarm");
            alarm_info->flag = 0x400;   //VTYSH_ALARM;
            alarm_info->path = XSTRDUP (MTYPE_VTYSH_VTY, ALARM_VTYSH_PATH);

            if (vtysh_connect (alarm_info) < 0)
            {
                VTY_PRINT ("vtysh connect alarm server error.\n");
                if (alarm_info->fd != -1)
                    close(alarm_info->fd);
                XFREE (MTYPE_VTYSH_VTY, alarm_info->name);
                XFREE (MTYPE_VTYSH_VTY, alarm_info->path);
                XFREE (MTYPE_VTYSH_VTY, alarm_info);
            }
        }
    }
    vty->vtysh_info = alarm_info;
    if (vty->vtysh_info != NULL)
    {
        vty->daemon_num = 1;
        alarm_info_refs++;
        /*fprintf(stderr, "pid[%d] alarm fd=%d refs=%d\n", getpid(), alarm_info->fd, alarm_info_refs);*/
    }
    //stephen.liu add, for unix socket access. used for simulation vty
    if ((g_vtysh_flag) && (!alarm_info)){
        //printf("%s %d, pid[%d], sock=%d\r\n", __func__, __LINE__, getpid(), sock);
        //need init all daemon
        init_vty_all_daemon (vty);
    }

    //fprintf(stderr, "[%d]%d sock=%d\r\n", getpid(), __LINE__, sock);//stephen.liu 20160815
    vty_event (VTYSH_READ, sock, vty);

    return 0;
}

int
vtysh_flush (struct vty *vty)
{
    switch (buffer_flush_available (vty->obuf, vty->fd)) {
    case BUFFER_PENDING:
        vty_event (VTYSH_WRITE, vty->fd, vty);
        break;
    case BUFFER_ERROR:
        vty->monitor = 0;       /* disable monitoring to avoid infinite recursion */
        zlog_warn ("%s: write error to fd %d, closing", __func__, vty->fd);
        buffer_reset (vty->obuf);
        vty_close (vty);
        return -1;
        break;
    case BUFFER_EMPTY:
        break;
    }

    return 0;
}

static int
vtysh_read (struct thread *thread)
{
    int ret;
    int sock;
    int nbytes;
    struct vty *vty;
    unsigned char buf[VTY_READ_BUFSIZ];
    unsigned char *p;
    u_char header[4] = { 0, 0, 0, 0 };
#if DEBUG_VTY
        char task_name[128];
    VTY_PRINT("%s process:%s\r\n", __func__, (char *)getNameByPid(getpid(), task_name));
#endif
    sock = THREAD_FD (thread);
    vty = THREAD_ARG (thread);
    vty->t_read = NULL;

    if ((nbytes = read (sock, buf, VTY_READ_BUFSIZ)) <= 0) {
        if (nbytes < 0) {
            if (ERRNO_IO_RETRY (errno)) {
                vty_event (VTYSH_READ, sock, vty);
                return 0;
            }
            vty->monitor = 0;   /* disable monitoring to avoid infinite recursion */
            zlog_warn ("%s: read failed on vtysh client fd %d, closing: %s", __func__, sock, safe_strerror (errno));
        }
        buffer_reset (vty->obuf);
        vty_close (vty);
#ifdef VTYSH_DEBUG
        VTY_PRINT ("close vtysh\r\n");
#endif /* VTYSH_DEBUG */
        return 0;
    }

#ifdef VTYSH_DEBUG
#ifdef DEBUG_VTY
    VTY_PRINT ("[%s]vtysh :line: %.*s\r\n", task_name, nbytes, buf);
#endif
#endif /* VTYSH_DEBUG */

    if (nbytes >= sizeof(VTYSH_CMD_USER_ENV_GET) && memcmp(buf, VTYSH_CMD_USER_ENV_GET, sizeof(VTYSH_CMD_USER_ENV_GET)) == 0)
    {
        int len = sizeof(buf) - sizeof(VTYSH_CMD_USER_ENV_GET);

        len = sizeof(vty->user.env) > len ? len : sizeof(vty->user.env);
        /*fprintf(stderr, "pid[%d] command enter\r\n", getpid());*/
        memcpy(&buf[sizeof(VTYSH_CMD_USER_ENV_GET)], &vty->user.env, len);
        if (vty->t_write) {
            thread_cancel (vty->t_write);
            buffer_flush_all(vty->obuf, vty->fd);
        }
        buffer_put (vty->obuf, buf, len + sizeof(VTYSH_CMD_USER_ENV_GET));
        vtysh_flush(vty);
        /*fprintf(stderr, "pid[%d] command complete\r\n", getpid());*/
    }
    else
    {
        for (p = buf; p < buf + nbytes; p++) {
            vty_ensure (vty, vty->length + 1);
            vty->buf[vty->length++] = *p;
            if (*p == '\0') {
                /* Pass this line to parser. */
                ret = vty_execute (vty);
                /* Note that vty_execute clears the command buffer and resets
                   vty->length to 0. */

                /* Return result. */
#ifdef VTYSH_DEBUG
#ifdef DEBUG_VTY
                VTY_PRINT ("[%s]result: %d\r\n", task_name, ret);
                VTY_PRINT ("[%s]vtysh node: %d\r\n", task_name, vty->node);
#endif
#endif /* VTYSH_DEBUG */
                header[3] = ret;
                buffer_put (vty->obuf, header, 4);

                if (!vty->t_write && (vtysh_flush (vty) < 0)) {
                    /* Try to flush results; exit if a write error occurs. */
                    return 0;
                }
            }
        }
    }

    vty_event (VTYSH_READ, sock, vty);

    return 0;
}

static int
vtysh_client_read (struct thread *thread)
{
    //int ret;
    int sock;
    int nbytes;
    struct vty *vty;
    char buf[VTY_READ_BUFSIZ];
    //unsigned char *p;
    //u_char header[4] = {0, 0, 0, 0};
    int i;
    int numnulls = 0;

    sock = THREAD_FD (thread);
    vty = THREAD_ARG (thread);
    vty->t_read = NULL;
    VTY_PRINT ("sock:%d, vty->fd=%d, vty->type=%d\n", sock, vty->fd, vty->type);
    nbytes = read (sock, buf, VTY_READ_BUFSIZ);
    if (nbytes <= 0 && errno != EINTR) {

        vty_event (VTYSH_CONNECT, sock, vty);
        //vclient_close (vclient);
        return CMD_SUCCESS;
    }

    if (nbytes > 0) {
        if ((numnulls == 3) && (nbytes == 1))
            return buf[0];
        //dump_hex_value (__func__, buf, nbytes);
        //buf[nbytes] = '\0';
        fputs (buf, stdout);    //stpehen, 20150929
        fflush (stdout);
        //vty_out(vty, "%s", buf);

        /* check for trailling \0\0\0<ret code>,
         * even if split across reads
         * (see lib/vty.c::vtysh_read)
         */
        if (nbytes >= 4) {
            i = nbytes - 4;
            numnulls = 0;
        }
        else
            i = 0;

        while (i < nbytes && numnulls < 3) {
            if (buf[i++] == '\0')
                numnulls++;
            else
                numnulls = 0;
        }

        /* got 3 or more trailing NULs? */
        if ((numnulls >= 3) && (i < nbytes))
            return (buf[nbytes - 1]);
    }
#if 0
    {
        if (nbytes < 0) {
            if (ERRNO_IO_RETRY (errno)) {
                vty_event (VTYSH_READ, sock, vty);
                return 0;
            }
            vty->monitor = 0;   /* disable monitoring to avoid infinite recursion */
            zlog_warn ("%s: read failed on vtysh client fd %d, closing: %s", __func__, sock, safe_strerror (errno));
        }
        buffer_reset (vty->obuf);
        vty_close (vty);
#ifdef VTYSH_DEBUG
        printf("close vtysh\n");
#endif /* VTYSH_DEBUG */
        return 0;
    }

#ifdef VTYSH_DEBUG
    printf("line: %.*s\n", nbytes, buf);
#endif /* VTYSH_DEBUG */

    for (p = buf; p < buf + nbytes; p++) {
        vty_ensure (vty, vty->length + 1);
        vty->buf[vty->length++] = *p;
        if (*p == '\0') {
            /* Pass this line to parser. */
            ret = vty_execute (vty);
            /* Note that vty_execute clears the command buffer and resets
               vty->length to 0. */

            /* Return result. */
#ifdef VTYSH_DEBUG
            printf("result: %d\n", ret);
            printf("vtysh node: %d\n", vty->node);
#endif /* VTYSH_DEBUG */

            header[3] = ret;
            buffer_put (vty->obuf, header, 4);

            if (!vty->t_write && (vtysh_flush (vty) < 0))
                /* Try to flush results; exit if a write error occurs. */
                return 0;
        }
    }
#endif

    vty_event (VTYSH_CONNECT, sock, vty);

    return 0;
}


static int
vtysh_write (struct thread *thread)
{
    struct vty *vty = THREAD_ARG (thread);

    vty->t_write = NULL;
    vtysh_flush (vty);
    return 0;
}

//if VTY_SHELL_SERV, WE USE THIS interface
//stephen.liu 20151029
int vtysh_command_get_data(struct vty *vty, unsigned char *data)
{
    int timeout = 0;
    unsigned char buf[3] = {0xff, 0x55, 0xff};
    vtysh_client_info *vtysh_info = NULL;
    if(NULL == data)
        return -1;

    //send to send to command channel
    write(vty->fd, buf, 3);
    //send to alarm channel, we will read data from alarm channel
    vtysh_info = (vtysh_client_info *) vty->vtysh_info;
    write(vtysh_info->fd, buf, 3);
    //printf("read fd:%d\n", vtysh_info->fd);
    g_active.fd = vtysh_info->fd;
    g_active.flag = TRUE;

    while(g_active.flag && timeout < GET_DATA_TIME_OUT){
        timeout++;
        sleep(1);
        /*printf("start read.timeout=%d\r\n", timeout);*/
    }

    if(g_active.flag&&timeout==GET_DATA_TIME_OUT){
        VTY_PRINT("time out\n");
        g_active.flag = FALSE;
        g_active.fd = -1;
        g_active.size = 0;
        g_daemon_send_fd = -1;
        g_daemon_send_flag = FALSE;
        return TIME_OUT_ERR;
    }

    memcpy(data, g_active.buffer, g_active.size);
    g_active.flag = FALSE;
    g_active.size = 0;
    g_active.fd = -1;
    return RT_OK;
}

int vtysh_user_env_get(struct vty *vty, vtysh_client_info * vclient, char *buf, int len)
{
    int ret;
    unsigned char cmd[] = VTYSH_CMD_USER_ENV_GET;
    int nbytes;
    fd_set g_set, c_set;
    int max = 0, n = 0;
    struct timeval timeout, ti;
    int timeout_num = 0;
    char *cmdRsp = NULL;
    int   cmdRspLen = sizeof(cmd)+len;
    int wait_num = 0;

    timeout.tv_sec = 10;
    timeout.tv_usec = 0;

    if (vclient->fd < 0)
        return CMD_ERR_INCOMPLETE;

    ret = send (vclient->fd, cmd,sizeof(cmd), 0);
    if (ret <= 0)
    {
        fprintf(stderr, "%s %d\r\n", __func__, __LINE__);
        vclient_close (vclient);
        return CMD_ERR_INCOMPLETE;
    }
    //printf("write size:%d\n", ret);

    FD_ZERO (&g_set);
    FD_SET (vclient->fd, &g_set);
    max = vclient->fd;

    cmdRsp = XMALLOC(MTYPE_TMP, cmdRspLen + 1);
    if (cmdRsp == NULL)
    {
        return CMD_ERR_INCOMPLETE;
    }

    while (1)
    {
        ti = timeout;
        c_set = g_set;
        n = select (max + 1, &c_set, NULL, NULL, &ti);
        if (n < 0)
        {
            if (EINTR == errno)
            {
                continue;
            }
            vty_out (vty, "select error:%s\n", strerror (errno));
            XFREE(MTYPE_TMP, cmdRsp);
            return CMD_ERR_INCOMPLETE;
        }
        if(n == 0)
        {
            timeout_num++;
            if(timeout_num == 3)
            {
                vty_out (vty, "   VTY daemons[%s] command excute time out!%s",vclient->name,VTY_NEWLINE);
                XFREE(MTYPE_TMP, cmdRsp);
                return CMD_ERR_INCOMPLETE;
            }
            continue;
        }
        if(FD_ISSET(vclient->fd, &c_set))
        {
            nbytes = read (vclient->fd, cmdRsp, cmdRspLen);

            if (nbytes < 0 && !ERRNO_IO_RETRY(errno))
            {
                fprintf(stderr, "%s %d\r\n", __func__, __LINE__);
                vclient_close (vclient);
                XFREE(MTYPE_TMP, cmdRsp);
                return CMD_ERR_INCOMPLETE;
            }

            if (nbytes == 0)/* end of file */
            {
                XFREE(MTYPE_TMP, cmdRsp);
                return CMD_ERR_INCOMPLETE;
            }

            if (nbytes > 0)
            {
                if (wait_num > 3)
                {
                    XFREE(MTYPE_TMP, cmdRsp);
                    return CMD_ERR_INCOMPLETE;
                }

                if (nbytes < sizeof(cmd) || memcmp(cmdRsp, cmd, sizeof(cmd)) != 0)
                {
                    /*fprintf(stderr, "pid[%d] error restfse\r\n", getpid());*/
                    wait_num++;
                    continue;
                }

                memcpy(buf, &cmdRsp[sizeof(cmd)], nbytes - sizeof(cmd));

                XFREE(MTYPE_TMP, cmdRsp);
                return CMD_SUCCESS;
            }
        }
    }

}

int vtysh_user_env_get_from_serv(struct vty * vty,struct cmd_element *cmd)
{
    int i;
    int cmd_stat;
    vtysh_client_info *c = (vtysh_client_info *) vty->vtysh_info;

    cmd_stat = CMD_SUCCESS;
    for (i = 0; i < vty->daemon_num; i++) {
        if (cmd->daemon & c[i].flag) {
            cmd_stat = vtysh_user_env_get (vty, &c[i], (char*)&vty->user.env, sizeof(vty->user.env));
            if (cmd_stat == CMD_SUCCESS)
                return cmd_stat;
        }
    }

    return CMD_ERR_INCOMPLETE;
}

int vtysh_gprofile_node_enter_check(struct vty * vty)
{
    int idx;
    struct vty *pTmpVty;

    if (vtyvec == NULL)
        return -1;

    for (idx = 0; idx < vector_active(vtyvec); idx++)
    {
        pTmpVty = vector_slot (vtyvec, idx);
        if (pTmpVty != NULL) {
            if(pTmpVty == vty)
                continue;

            if (pTmpVty->node == vty->node &&
                memcmp(&pTmpVty->user.env, &vty->user.env, sizeof(pTmpVty->user.env)) == 0)
            {
                vty_out_line(vty, "  Error: Another user is operating the current profile, please try later");
                vty->node = CONFIG_NODE;
            }
        }
    }

    return 0;
}

#endif /* VTYSH */

/* Determine address family to bind. */
void
vty_serv_sock (const char *addr, unsigned short port, const char *path)
{
    /* If port is set to 0, do not listen on TCP/IP at all! */
    if (port) {
#ifdef HAVE_IPV6
        vty_serv_sock_addrinfo (addr, port);
#else /* ! HAVE_IPV6 */
        //printf("vty_serv_sock_family,g_vtysh_flag=%d, port=%d\n"
        //, g_vtysh_flag, port);
        vty_serv_sock_family (addr, port, AF_INET);
#endif /* HAVE_IPV6 */
    }

//#ifdef VTYSH
    //stephen.liu 20160816
    if ((!g_vtysh_flag)||(!strcmp(path, VTYSH_VTYSH_PATH)))
        vty_serv_un (path);
//#endif /* VTYSH */
}

/* Close vty interface.  Warning: call this only from functions that
   will be careful not to access the vty afterwards (since it has
   now been freed).  This is safest from top-level functions (called
   directly by the thread dispatcher). */
void
vty_close (struct vty *vty)
{
    vtysh_client_info *vtysh_info = NULL;
    int i;
#if DEBUG_VTY
        char task_name[128];
#endif
    if (g_vtysh_flag)
    {
        pthread_mutex_lock(&vty_mutex);
    }
    /* Cancel threads. */
    if (vty->t_read)
        thread_cancel (vty->t_read);
    if (vty->t_write)
        thread_cancel (vty->t_write);
    if (vty->t_timeout)
        thread_cancel (vty->t_timeout);

    /* logout or timeout record */
    if ((vty->type == VTY_TERM || vty->type == VTY_TERM_LOCAL || vty->type == VTY_SSH)
        && ((vty->sign_user.level & ENUM_ACCESS_OP_MANUFACTURE) == 0))
    {
        tflog_oper("[%s@%s:%d] logoff", vty->sign_user.name, vty->address[0] ? vty->address : "unknown", vty->fd);
    }

    /* Flush buffer. */
    buffer_flush_all (vty->obuf, vty->fd);

    /* Free input buffer. */
    buffer_free (vty->obuf);

    /***Begin: Add by steven.tian on 2015/12/29*************/
    /* Reset alarm and debug buffer */
    buffer_reset(vty->dbuf);
    /* Free alarm and debug buffer */
    buffer_free(vty->dbuf);
    /***End: Add by steven.tian on 2015/12/29*************/

    /* Free command history. */
    for (i = 0; i < VTY_MAXHIST; i++)
        if (vty->hist[i])
            XFREE (MTYPE_VTY_HIST, vty->hist[i]);

    /* Unset vector. */
    vector_unset (vtyvec, vty->fd);

    /* Close socket. */
    if (vty->fd > 0)
        close (vty->fd);

    if (vty->buf)
        XFREE (MTYPE_VTY, vty->buf);
    //stephen.liu, 20161214
    if(vty->temp_buf)
        XFREE (MTYPE_VTY, vty->temp_buf);

    if (vty->sign_user.name[0] != '\0')
        vty_user_update_hist_state (vty->port, VTY_LOGOUT);

    /*stephen.liu, 20150930, free vtysh client info */
    if (vty->vtysh_info) {
        vtysh_info = (vtysh_client_info *) vty->vtysh_info;
        //modified by stephen.liu 20160817, for add local unix socket access vtysh , from other process
        //if (((vty->type != VTY_SHELL_SERV)&&(!g_vtysh_flag))
         //   ||((vty->type == VTY_SHELL_SERV)&&g_vtysh_flag))
         if(vty->type != VTY_SHELL_SERV)
        {
            for (i = 0; i < vty->daemon_num; i++) {
                close (vtysh_info[i].fd);
                vtysh_info[i].fd = -1;
                if (vtysh_info[i].name)
                    XFREE (MTYPE_VTYSH_VTY, vtysh_info[i].name);
                vtysh_info[i].flag = 0;
                if (vtysh_info[i].path)
                    XFREE (MTYPE_VTYSH_VTY, vtysh_info[i].path);
            }
            XFREE (MTYPE_VTYSH_VTY, vty->vtysh_info);
        }
        else
        {
            if (vtysh_info == alarm_info)
            {
                alarm_info_refs--;
                /*fprintf(stderr, "pid[%d] alarm fd=%d refs=%d\n", getpid(), alarm_info->fd, alarm_info_refs);*/
                if (alarm_info_refs == 0)
                {
                    close (alarm_info->fd);
                    alarm_info->fd = -1;
                    if (alarm_info->name)
                        XFREE (MTYPE_VTYSH_VTY, alarm_info->name);
                    alarm_info->flag = 0;
                    if (alarm_info->path)
                        XFREE (MTYPE_VTYSH_VTY, alarm_info->path);
                    XFREE (MTYPE_VTYSH_VTY, alarm_info);
                }
            }
        }


    }

    /* Check configure. */
    vty_config_unlock (vty);

    /* OK free vty. */
    XFREE (MTYPE_VTY, vty);
    //printf("[%s]Vty close<type:%d>.\n",(char *)getNameByPid(getpid(), task_name), vty->type); //stephen.liu

    if (g_vtysh_flag)
    {
        pthread_mutex_unlock(&vty_mutex);
    }
}

void
vty_clear_user_data (struct vty *vty)
{
    memset (&vty->sign_user, 0x0, sizeof (struct vty_user));
}

/* When time out occur output message then close connection. */
static int
vty_timeout (struct thread *thread)
{
    struct vty *vty;

    vty = THREAD_ARG (thread);
    vty->t_timeout = NULL;
    vty->v_timeout = 0;

    /* Clear buffer */
    buffer_reset (vty->obuf);
    vty_out (vty, "%sconnection is timed out.%s", VTY_NEWLINE, VTY_NEWLINE);
    fprintf(stderr, "connection is timed out.\r\n");

    /* Close connection. */
    vty->status = VTY_CLOSE;
    vty_close (vty);

    return 0;
}

/* Read up configuration file from file_name. */
static void
vty_read_file (FILE * confp)
{
    int ret;
    struct vty *vty;
    unsigned int line_num = 0;

    //stephen.liu 20151019
    if (NULL == confp) {
        return;
    }

    vty = vty_new ();
    vty->fd = dup (STDERR_FILENO);  /* vty_close() will close this */
    if (vty->fd < 0) {
        /* Fine, we couldn't make a new fd. vty_close doesn't close stdout. */
        vty->fd = STDOUT_FILENO;
    }
    vty->type = VTY_FILE;
    vty->node = CONFIG_NODE;

    /* Execute configuration file */
    ret = config_from_file (vty, confp, &line_num);

    /* Flush any previous errors before printing messages below */
    buffer_flush_all (vty->obuf, vty->fd);

    if (!((ret == CMD_SUCCESS) || (ret == CMD_ERR_NOTHING_TODO))) {
        switch (ret) {
        case CMD_ERR_AMBIGUOUS:
            vty_out_line(vty, "*** Error reading config: Ambiguous command.");
            break;
        case CMD_ERR_NO_MATCH:
            vty_out_line(vty, "*** Error reading config: There is no such command.");
            break;
        }
        vty_out_line(vty, "*** Error occured processing line %u, below:", line_num);
        vty_out_line(vty, "%s", vty->buf);
        vty_close (vty);
        exit (1);
    }

    vty_close (vty);
}

static FILE *
vty_use_backup_config (char *fullpath)
{
    char *fullpath_sav, *fullpath_tmp;
    FILE *ret = NULL;
    struct stat buf;
    int tmp, sav;
    int c;
    char buffer[512];

    fullpath_sav = malloc (strlen (fullpath) + strlen (CONF_BACKUP_EXT) + 1);
    strcpy (fullpath_sav, fullpath);
    strcat (fullpath_sav, CONF_BACKUP_EXT);
    if (stat (fullpath_sav, &buf) == -1) {
        free (fullpath_sav);
        return NULL;
    }

    fullpath_tmp = malloc (strlen (fullpath) + 8);
    sprintf(fullpath_tmp, "%s.XXXXXX", fullpath);

    /* Open file to configuration write. */
    tmp = mkstemp (fullpath_tmp);
    if (tmp < 0) {
        free (fullpath_sav);
        free (fullpath_tmp);
        return NULL;
    }

    sav = open (fullpath_sav, O_RDONLY);
    if (sav < 0) {
        unlink (fullpath_tmp);
        free (fullpath_sav);
        free (fullpath_tmp);
        return NULL;
    }

    while ((c = read (sav, buffer, 512)) > 0)
        write (tmp, buffer, c);

    close (sav);
    close (tmp);

    if (chmod (fullpath_tmp, CONFIGFILE_MASK) != 0) {
        unlink (fullpath_tmp);
        free (fullpath_sav);
        free (fullpath_tmp);
        return NULL;
    }

    if (link (fullpath_tmp, fullpath) == 0)
        ret = fopen (fullpath, "r");

    unlink (fullpath_tmp);

    free (fullpath_sav);
    free (fullpath_tmp);
    return ret;
}

/* Read up configuration file from file_name. */
void
vty_read_config (char *config_file, char *config_default_dir)
{
    char cwd[MAXPATHLEN];
    FILE *confp = NULL;
    char *fullpath = NULL;
    char *tmp = NULL;

    /* If -f flag specified. */
    if (config_file != NULL) {
        if (!IS_DIRECTORY_SEP (config_file[0])) {
            getcwd (cwd, MAXPATHLEN);
            tmp = XMALLOC (MTYPE_TMP, strlen (cwd) + strlen (config_file) + 2);
            sprintf(tmp, "%s/%s", cwd, config_file);
            fullpath = tmp;
        }
        else
            fullpath = config_file;

        confp = fopen (fullpath, "r");

        if (confp == NULL) {
            //fprintf(stderr, "%s: failed to open configuration file %s: %s\n",
            //         __func__, fullpath, safe_strerror (errno));

            confp = vty_use_backup_config (fullpath);
            if (confp);         //fprintf(stderr, "WARNING: using backup configuration file!\n");
            else {
                //fprintf(stderr, "can't open configuration file [%s]\n",
                //     config_file);
                //exit(1);
            }
        }
    }
    else {
#ifdef VTYSH
        int ret;
        struct stat conf_stat;

        /* !!!!PLEASE LEAVE!!!!
         * This is NEEDED for use with vtysh -b, or else you can get
         * a real configuration food fight with a lot garbage in the
         * merged configuration file it creates coming from the per
         * daemon configuration files.  This also allows the daemons
         * to start if there default configuration file is not
         * present or ignore them, as needed when using vtysh -b to
         * configure the daemons at boot - MAG
         */

        /* Stat for vtysh Zebra.conf, if found startup and wait for
         * boot configuration
         */

        if (strstr (config_default_dir, "vtysh") == NULL) {
            ret = stat (integrate_default, &conf_stat);
            if (ret >= 0)
                return;
        }
#endif /* VTYSH */

        confp = fopen (config_default_dir, "r");
        if (confp == NULL) {
            fprintf(stderr, "%s: failed to open configuration file %s: %s\n",
                     __func__, config_default_dir, safe_strerror (errno));
            return;
            confp = vty_use_backup_config (config_default_dir);
            if(!confp){
                fprintf(stderr, "WARNING: using backup configuration file!\n");
                return;
            }
        }

        fullpath = config_default_dir;
    }

    vty_read_file (confp);

    //stephen.liu 20151019
    if (confp){
        fclose (confp);
    }

    host_config_set (fullpath);

    if (tmp)
        XFREE (MTYPE_TMP, fullpath);
}

/* Small utility function which output log to the VTY. */
void
vty_log (const char *level, const char *proto_str, const char *format, struct timestamp_control *ctl, va_list va)
{
    unsigned int i;
    struct vty *vty;

    if (!vtyvec)
        return;

    for (i = 0; i < vector_active (vtyvec); i++)
        if ((vty = vector_slot (vtyvec, i)) != NULL)
            if (vty->monitor) {
                va_list ac;
                va_copy (ac, va);
                vty_log_out (vty, level, proto_str, format, ctl, ac);
                va_end (ac);
            }
}

/* Async-signal-safe version of vty_log for fixed strings. */
void
vty_log_fixed (char *buf, size_t len)
{
    unsigned int i;
    struct iovec iov[2];

    /* vty may not have been initialised */
    if (!vtyvec)
        return;

    iov[0].iov_base = buf;
    iov[0].iov_len = len;
    iov[1].iov_base = (void *) "\r\n";
    iov[1].iov_len = 2;

    for (i = 0; i < vector_active (vtyvec); i++) {
        struct vty *vty;
        if (((vty = vector_slot (vtyvec, i)) != NULL) && vty->monitor)
            /* N.B. We don't care about the return code, since process is
               most likely just about to die anyway. */
            writev (vty->fd, iov, 2);
    }
}

int
vty_config_lock (struct vty *vty)
{
    if (vty_config == 0) {
        vty->config = 1;
        vty_config = 1;
    }
    return vty->config;
}

int
vty_config_unlock (struct vty *vty)
{
    if (vty_config == 1 && vty->config == 1) {
        vty->config = 0;
        vty_config = 0;
    }
    return vty->config;
}

/* Master of the threads. */
static struct thread_master *master;

static void
vty_event (enum event event, int sock, struct vty *vty)
{
    struct thread *vty_serv_thread;

    switch (event) {
    case VTY_SERV:
        vty_serv_thread = thread_add_read (master, vty_accept, vty, sock);
        vector_set_index (Vvty_serv_thread, sock, vty_serv_thread);
        break;
#ifdef VTYSH
    case VTYSH_SERV:
        vty_serv_thread = thread_add_read (master, vtysh_accept, vty, sock);
        vector_set_index (Vvty_serv_thread, sock, vty_serv_thread);
        break;
    case VTYSH_READ:
        vty->t_read = thread_add_read (master, vtysh_read, vty, sock);
        break;
    case VTYSH_WRITE:
        vty->t_write = thread_add_write (master, vtysh_write, vty, sock);
        break;
        /* stephen.liu 20151008 */
    case VTYSH_CONNECT:
        vty->t_read = thread_add_read (master, vtysh_client_read, vty, sock);
        break;
#endif /* VTYSH */
    case VTY_READ:
        vty->t_read = thread_add_read (master, vty_read, vty, sock);

        /* Time out treatment. */
        if (vty->v_timeout) {
            if (vty->t_timeout)
                thread_cancel (vty->t_timeout);
            vty->t_timeout = thread_add_timer (master, vty_timeout, vty, vty->v_timeout);
        }
        break;
    case VTY_WRITE:
        if (!vty->t_write)
            vty->t_write = thread_add_write (master, vty_flush, vty, sock);
        break;
    case VTY_TIMEOUT_RESET:
        if (vty->t_timeout) {
            thread_cancel (vty->t_timeout);
            vty->t_timeout = NULL;
        }
        if (vty->v_timeout) {
            vty->t_timeout = thread_add_timer (master, vty_timeout, vty, vty->v_timeout);
        }
        break;
    }
}



void vty_iactive_init()
{
    g_active.fd = -1;
    g_active.flag = FALSE;
    g_active.vtysh_flag = 0;
    memset(g_active.buffer, 0x00, DEFALUT_BUFFER_SIZE);
}
//if VTY_TERM , we use this interface
//stephen.liu 20151029
int vty_command_get_data(int fd, unsigned char *data)
{
    int timeout = 0;
    //unsigned char buf[3] = {0xff, 0x55, 0xff};
    if(NULL == data)
        return -1;

    if(g_active.fd != -1){
        return PARAMETER_ERROR;
    }

    g_active.fd = fd;
    g_active.flag = TRUE;

    while(g_active.flag && timeout < GET_DATA_TIME_OUT){
        timeout++;
        sleep(1);
        /*printf("start read.timeout=%d\r\n", timeout);*/
    }

    if(g_active.flag&&timeout==GET_DATA_TIME_OUT){
        VTY_PRINT("time out\n");
        g_active.flag = FALSE;
        g_active.size = 0;
        g_active.fd = -1;
        return TIME_OUT_ERR;
    }
    memcpy(data, g_active.buffer, g_active.size);
    g_active.flag = FALSE;
    g_active.size = 0;
    g_active.fd = -1;
    return RT_OK;
}

int vty_get_data(struct vty *vty, unsigned char *data)
{
    if(vty->type == VTY_SHELL_SERV)
        return vtysh_command_get_data(vty, data);
    return vty_command_get_data(vty->fd, data);
}

void vty_command_read_data_thread(void)
{
    int rBytes = 0, r;
    int cnt = 0;
    fd_set readset;
    struct timeval timeout;
    struct timeval wait;
    int ret;

    while(TRUE){
        if(g_active.flag && g_active.fd != -1 && wait.tv_sec < 10){/* max:10s */
            if (cnt == 0)
            {
                memset(g_active.buffer, 0x00, DEFALUT_BUFFER_SIZE);
                g_active.size = 0;
            }
            cnt++;

            FD_ZERO(&readset);
            FD_SET(g_active.fd, &readset);
            timeout.tv_sec = 0;
            timeout.tv_usec = 100000;/* 100ms */

            /*fprintf(stderr, "PID[%d] %s:%d\r\n", getpid(), __FUNCTION__, __LINE__);*/

            ret = select(g_active.fd + 1, &readset, NULL, NULL, &timeout);
            if (ret <= 0)
            {
                wait.tv_usec = (wait.tv_usec + 100000) % 1000000;
                wait.tv_sec +=  (wait.tv_usec + 100000) / 1000000;
                continue;
            }

            wait.tv_usec = (wait.tv_usec + 100000 - timeout.tv_usec) % 1000000;
            wait.tv_sec +=  (wait.tv_usec + 100000 - timeout.tv_usec) / 1000000;

            if (!FD_ISSET(g_active.fd, &readset))
            {
                continue;
            }

            #if 1
            rBytes = read(g_active.fd , &g_active.buffer[g_active.size], DEFALUT_BUFFER_SIZE - g_active.size);
            #else
            rBytes = read(g_active.fd , g_active.buffer, DEFALUT_BUFFER_SIZE);
            #endif
            if(rBytes <= 0){
                /*usleep(1000);*/
                //printf("ddddddrBytes=%d\n", rBytes);
                continue;
            }
            #if 1
            g_active.size += rBytes;
            #else
            g_active.size = rBytes;
            #endif
            g_active.buffer[g_active.size%DEFALUT_BUFFER_SIZE] = '\0';
            /*fprintf(stderr, "pid[%d] recive get  input data.fd=%d,buf[0]=%s,rbytes=%d\r\n"
                , getpid(), g_active.fd, g_active.buffer, rBytes);*/
            //must check if vtysh process
            #if 1
            if (strchr((char*)g_active.buffer, '\n') == NULL
                && strchr((char*)g_active.buffer, '\r') == NULL)
            {
                continue;
            }
            g_active.flag = FALSE;
            #endif

            if(g_vtysh_flag){
                if(g_daemon_send_flag){
                    //printf("write fd=%d, buffer[0]=%c\n"
                    //    , g_daemon_send_fd, g_active.buffer[0]);
                    r = send(g_daemon_send_fd, g_active.buffer, rBytes, 0);
                    g_daemon_send_flag = FALSE;
                    g_active.size = 0;
                    memset(g_active.buffer, 0x00, DEFALUT_BUFFER_SIZE);
                    g_active.fd = -1;
                    //printf("r = %d\n", r);
                }
            }else{

            }
        }else{
            //collect 1s intervals, we check read flag
            //stephen.liu, 20151028
            usleep(10000);
        }
        cnt = 0;
        wait.tv_sec = 0;
        wait.tv_usec = 0;
    }
}


DEFUN (config_who,
    config_who_cmd,
    "show client",
    "Show information\n"
    "Users information\n")
{
    vty_login_show (vty, 1);
    return CMD_SUCCESS;
}

DEFUN (config_who_info,
    config_who_info_cmd,
    "show client info",
    "Show information\n"
    "Users information\n"
    "Information\n")
{
    unsigned int idx;
    struct vty *v;
    char *typeStr[] = { "Telnet", "File", "Shell", "Shell Server", "Shell Client", "Console" };
    struct timeval time_end;
    int sec;
    char printed = 0;
    char loginFlag;

    gettimeofday (&time_end, NULL);

    for (idx = 0; idx < vector_active (vtyvec); idx++) {
        if ((v = vector_slot (vtyvec, idx)) != NULL) {
            vty_out(vty, "v->node=%d, port=%d, type=%d%s", v->node, v->port, v->type,VTY_NEWLINE);

            sec = time_end.tv_sec - v->time_start.tv_sec;
            vty_out (vty, " %c%-12s   %-16s   %-17s   %02d:%02d:%02d%s",
                    v == vty ? '>' : ' ',
                    typeStr[v->type],
                    v->sign_user.name,
                    v->type == VTY_TERM_LOCAL ? "--" :v->address,
                    sec / 3600, sec % 3600 / 60, sec % 60, VTY_NEWLINE);
            vty_out(vty, " terminal type: %s%s", &v->sb_buf[2], VTY_NEWLINE);
            vty_out(vty, " terminal window size:%s", VTY_NEWLINE);
            vty_out(vty, "         height:  %d%s", v->height, VTY_NEWLINE);
            vty_out(vty, "         width :  %d%s", v->width, VTY_NEWLINE);
        }
    }
    return CMD_SUCCESS;
}


DEFUN (config_kickoff_client,
    config_kickoff_client_cmd,
    "client kick-off <1-4294967295>",
    "Users information\n"
    "Kick off login client\n"
    "Client ID. <U><1-4294967295>\n")
{
    unsigned int idx = atoi(argv[0]);
    struct vty *v;

    if(idx >= vector_active (vtyvec) || NULL == (v = vector_slot (vtyvec, idx)))
    {
        vty_out_line(vty, "  Error: The client do not exist");
        return CMD_ERR_NOTHING_TODO;
    }

    if(v == vty)
    {
        vty_out_line(vty, "  Error: Can't kick oneself off");
        return CMD_ERR_NOTHING_TODO;
    }

    if(cmd_element_access_allowed(v->sign_user.level, vty->sign_user.level) != TRUE)
    {
        vty_out_line(vty, "  Error: Insufficient privilege to perform requested command.");
        return CMD_ERR_NOTHING_TODO;
    }

    buffer_reset (v->obuf);
    v->status = VTY_CLOSE;
    vty_close (v);

    vty_out_line(vty, "The user has been kicked off successfully");

    return CMD_SUCCESS;
}


/* Set time out value. */
static int
exec_timeout (struct vty *vty, const char *min_str, const char *sec_str)
{
    unsigned long timeout = 0;

    /* min_str and sec_str are already checked by parser.  So it must be
       all digit string. */
    if (min_str) {
        timeout = strtol (min_str, NULL, 10);
        timeout *= 60;
    }
    if (sec_str)
        timeout += strtol (sec_str, NULL, 10);

    vty->v_timeout = timeout;
    vty_event (VTY_TIMEOUT_RESET, 0, vty);

    return CMD_SUCCESS;
}

DEFUN (exec_timeout_sec,
       exec_timeout_sec_cmd,
       "exec-timeout <1-36000>",
       "Set timeout value of the terminal line\n"
       "Timeout in seconds\n")
{
    return exec_timeout (vty, NULL, argv[0]);
}

DEFUN (exec_timeout_show,
       exec_timeout_show_cmd,
       "show exec-timeout",
       "Show information\n"
       "Timeout in seconds\n")
{
    vty_out_line(vty, " Timeout: %lus", vty->v_timeout);
    return CMD_SUCCESS;
}


DEFUN (no_exec_timeout,
       no_exec_timeout_cmd,
       "no exec-timeout",
       NO_STR
       "Set the EXEC timeout\n")
{
    return exec_timeout (vty, NULL, NULL);
}

/* Set vty access class. */
DEFUN (vty_access_class,
       vty_access_class_cmd,
       "access-class WORD",
       "Filter connections based on an IP access list\n"
       "IP access list\n")
{
    if (vty_accesslist_name)
        XFREE (MTYPE_VTY, vty_accesslist_name);

    vty_accesslist_name = XSTRDUP (MTYPE_VTY, argv[0]);

    return CMD_SUCCESS;
}

/* Clear vty access class. */
DEFUN (no_vty_access_class,
       no_vty_access_class_cmd,
       "no access-class [WORD]",
       NO_STR
       "Filter connections based on an IP access list\n"
       "IP access list\n")
{
    if (!vty_accesslist_name || (argc && strcmp (vty_accesslist_name, argv[0]))) {
        vty_out (vty, "Access-class is not currently applied to vty%s", VTY_NEWLINE);
        return CMD_WARNING;
    }

    XFREE (MTYPE_VTY, vty_accesslist_name);

    vty_accesslist_name = NULL;

    return CMD_SUCCESS;
}

#ifdef HAVE_IPV6
/* Set vty access class. */
DEFUN (vty_ipv6_access_class,
       vty_ipv6_access_class_cmd,
       "ipv6 access-class WORD",
       IPV6_STR
       "Filter connections based on an IP access list\n"
       "IPv6 access list\n")
{
    if (vty_ipv6_accesslist_name)
        XFREE (MTYPE_VTY, vty_ipv6_accesslist_name);

    vty_ipv6_accesslist_name = XSTRDUP (MTYPE_VTY, argv[0]);

    return CMD_SUCCESS;
}

/* Clear vty access class. */
DEFUN (no_vty_ipv6_access_class,
       no_vty_ipv6_access_class_cmd,
       "no ipv6 access-class [WORD]",
       NO_STR
       IPV6_STR
       "Filter connections based on an IP access list\n"
       "IPv6 access list\n")
{
    if (!vty_ipv6_accesslist_name || (argc && strcmp (vty_ipv6_accesslist_name, argv[0]))) {
        vty_out (vty, "IPv6 access-class is not currently applied to vty%s", VTY_NEWLINE);
        return CMD_WARNING;
    }

    XFREE (MTYPE_VTY, vty_ipv6_accesslist_name);

    vty_ipv6_accesslist_name = NULL;

    return CMD_SUCCESS;
}
#endif /* HAVE_IPV6 */

/* vty login. */
DEFUN (vty_login,
    vty_login_cmd,
    "login",
    "Enable password checking\n")
{
    no_password_check = 0;
    return CMD_SUCCESS;
}

DEFUN (no_vty_login,
       no_vty_login_cmd,
       "no login",
       NO_STR
       "Enable password checking\n")
{
    no_password_check = 1;
    return CMD_SUCCESS;
}

/* initial mode. */
DEFUN (vty_restricted_mode,
       vty_restricted_mode_cmd,
       "anonymous restricted",
       "Restrict view commands available in anonymous, unauthenticated vty\n")
{
    restricted_mode = 1;
    return CMD_SUCCESS;
}

DEFUN (vty_no_restricted_mode,
       vty_no_restricted_mode_cmd,
       "no anonymous restricted",
       NO_STR
       "Enable password checking\n")
{
    restricted_mode = 0;
    return CMD_SUCCESS;
}

DEFUN (service_advanced_vty,
       service_advanced_vty_cmd,
       "service advanced-vty",
       "Set up miscellaneous service\n"
       "Enable advanced mode vty interface\n")
{
    host.advanced = 1;
    return CMD_SUCCESS;
}

DEFUN (no_service_advanced_vty,
       no_service_advanced_vty_cmd,
       "no service advanced-vty",
       NO_STR
       "Set up miscellaneous service\n"
       "Enable advanced mode vty interface\n")
{
    host.advanced = 0;
    return CMD_SUCCESS;
}

DEFUN (terminal_monitor,
       terminal_monitor_cmd,
       "terminal monitor",
       "Set terminal line parameters\n"
       "Copy debug output to the current terminal line\n")
{
    vty->monitor = 1;
    return CMD_SUCCESS;
}

DEFUN (terminal_no_monitor,
       terminal_no_monitor_cmd,
       "terminal no monitor",
       "Set terminal line parameters\n"
       NO_STR
       "Copy debug output to the current terminal line\n")
{
    vty->monitor = 0;
    return CMD_SUCCESS;
}

char *getNameByPid(pid_t pid, char *task_name)
{
    char proc_pid_path[128];
    char buf[128];
    sprintf(proc_pid_path, "/proc/%d/status", pid);
    FILE* fp = fopen(proc_pid_path, "r");
    if(NULL != fp){
        if( fgets(buf, 127, fp)== NULL ){
            fclose(fp);
        }
        fclose(fp);
        sscanf(buf, "%*s %s", task_name);
        return task_name;
    }
    return NULL;
}





ALIAS (terminal_no_monitor,
       no_terminal_monitor_cmd,
       "no terminal monitor",
       NO_STR
       "Set terminal line parameters\n"
       "Copy debug output to the current terminal line\n")

DEFUN (terminal_debugging,
       terminal_debugging_cmd,
       "terminal debugging",
       "Set terminal line parameters\n"
       "Enble debug output to the current terminal line\n")
{
    vty->debug = 1;
    return CMD_SUCCESS;
}

DEFUN (no_terminal_debugging,
       no_terminal_debugging_cmd,
       "no terminal debugging",
       NO_STR
       "Set terminal line parameters\n"
       "Disable debug output to the current terminal line\n")
{
    vty->debug = 0;
    return CMD_SUCCESS;
}

DEFUN (show_terminal_debugging,
       show_terminal_debugging_cmd,
       "show terminal debugging",
       "Show information\n"
       "Show parameters of the current terminal line\n"
       "Show debugging status of the current terminal\n")
{
    vty_print_string_line (vty, "", __PROMPT_SEPARATOR_LENGTH__, __PROMPT_SEPARATOR_CHAR__);
    vty_out_line(vty, "  Current terminal debugging is %s.", vty->debug ? "ON" : "OFF");
    vty_print_string_line (vty, "", __PROMPT_SEPARATOR_LENGTH__, __PROMPT_SEPARATOR_CHAR__);
    return CMD_SUCCESS;
}

DEFUN (show_history,
       show_history_cmd,
       "show history",
       SHOW_STR
       "Display the session command history\n")
{
    int index;

    for (index = vty->hindex + 1; index != vty->hindex;) {
        if (index == VTY_MAXHIST) {
            index = 0;
            continue;
        }

        if (vty->hist[index] != NULL)
            vty_out (vty, "  %s%s", vty->hist[index], VTY_NEWLINE);

        index++;
    }

    return CMD_SUCCESS;
}

#if 000
/* Display current configuration. */
static int
vty_config_write (struct vty *vty)
{
    vty_out (vty, "line vty%s", VTY_NEWLINE);

    if (vty_accesslist_name)
        vty_out (vty, " access-class %s%s", vty_accesslist_name, VTY_NEWLINE);

    if (vty_ipv6_accesslist_name)
        vty_out (vty, " ipv6 access-class %s%s", vty_ipv6_accesslist_name, VTY_NEWLINE);

    /* exec-timeout */
    if (vty_timeout_val != VTY_TIMEOUT_DEFAULT)
        vty_out (vty, " exec-timeout %ld %ld%s", vty_timeout_val / 60, vty_timeout_val % 60, VTY_NEWLINE);

    /* login */
    if (no_password_check)
        vty_out (vty, " no login%s", VTY_NEWLINE);

    if (restricted_mode != restricted_mode_default) {
        if (restricted_mode_default)
            vty_out (vty, " no anonymous restricted%s", VTY_NEWLINE);
        else
            vty_out (vty, " anonymous restricted%s", VTY_NEWLINE);
    }

    vty_out (vty, "!%s", VTY_NEWLINE);

    return CMD_SUCCESS;
}
#endif

struct cmd_node vty_node = {
    VTY_NODE,
    "%s(config-line)# ",
    1,
};

/* Reset all VTY status. */
void
vty_reset ()
{
    unsigned int i;
    struct vty *vty;
    struct thread *vty_serv_thread;

    for (i = 0; i < vector_active (vtyvec); i++)
        if ((vty = vector_slot (vtyvec, i)) != NULL) {
            buffer_reset (vty->obuf);
            vty->status = VTY_CLOSE;
            vty_close (vty);
        }

    for (i = 0; i < vector_active (Vvty_serv_thread); i++)
        if ((vty_serv_thread = vector_slot (Vvty_serv_thread, i)) != NULL) {
            thread_cancel (vty_serv_thread);
            vector_slot (Vvty_serv_thread, i) = NULL;
            close (i);
        }

    vty_timeout_val = VTY_TIMEOUT_DEFAULT;

    if (vty_accesslist_name) {
        XFREE (MTYPE_VTY, vty_accesslist_name);
        vty_accesslist_name = NULL;
    }

    if (vty_ipv6_accesslist_name) {
        XFREE (MTYPE_VTY, vty_ipv6_accesslist_name);
        vty_ipv6_accesslist_name = NULL;
    }
}

static void
vty_save_cwd (void)
{
    char cwd[MAXPATHLEN];
    char *c;

    c = getcwd (cwd, MAXPATHLEN);

    if (!c) {
        chdir (SYSCONFDIR);
        getcwd (cwd, MAXPATHLEN);
    }

    vty_cwd = XMALLOC (MTYPE_TMP, strlen (cwd) + 1);
    strcpy (vty_cwd, cwd);
}

char *
vty_get_cwd ()
{
    return vty_cwd;
}

int
vty_shell (struct vty *vty)
{
    return vty->type == VTY_SHELL ? 1 : 0;
}

int
vty_shell_serv (struct vty *vty)
{
    return vty->type == VTY_SHELL_SERV ? 1 : 0;
}

void
vty_init_vtysh ()
{
    vtyvec = vector_init (VECTOR_MIN_SIZE);
}

/* Install vty's own commands like `who' command. */
void
vty_init (struct thread_master *master_thread)
{
    /* For further configuration read, preserve current directory. */
    vty_save_cwd ();

    vtyvec = vector_init (VECTOR_MIN_SIZE);

    master = master_thread;

    /* Initilize server thread vector. */
    Vvty_serv_thread = vector_init (VECTOR_MIN_SIZE);

    /*for local command, vtysh */
    g_exec_daemon = vtysh_execute_func_to_daemon;

    vty_iactive_init();
    create_thread_no_arg(vty_command_read_data_thread);

    //stephen.liu 20160822
    install_element (DEBUG_NODE, &config_who_info_cmd);
    //install_element (RESTRICTED_NODE, &show_history_cmd);
    install_element (VIEW_NODE, &config_who_cmd);
    install_element (VIEW_NODE, &show_history_cmd);
    install_element (ENABLE_NODE, &config_who_cmd);
    install_element (ENABLE_NODE, &config_kickoff_client_cmd);
    install_element (CONFIG_NODE, &config_who_cmd);
    install_element (CONFIG_NODE, &config_kickoff_client_cmd);
    //install_element (CONFIG_NODE, &line_vty_cmd);
    //install_element (CONFIG_NODE, &service_advanced_vty_cmd);
    //install_element (CONFIG_NODE, &no_service_advanced_vty_cmd);
    install_element (DEBUG_NODE, &terminal_debugging_cmd);
    install_element (DEBUG_NODE, &no_terminal_debugging_cmd);
    install_element (DEBUG_NODE, &show_terminal_debugging_cmd);
    install_element (CONFIG_NODE, &show_history_cmd);
    //install_element (ENABLE_NODE, &terminal_monitor_cmd);
    //install_element (ENABLE_NODE, &terminal_no_monitor_cmd);
    //install_element (ENABLE_NODE, &no_terminal_monitor_cmd);
    install_element (ENABLE_NODE, &show_history_cmd);

    //stephen.liu add, 20151009
    install_element (ENABLE_NODE, &no_exec_timeout_cmd);
    install_element (ENABLE_NODE, &exec_timeout_sec_cmd);
    install_element (ENABLE_NODE, &exec_timeout_show_cmd);
    install_element (CONFIG_NODE, &no_exec_timeout_cmd);
    install_element (CONFIG_NODE, &exec_timeout_sec_cmd);
    install_element (CONFIG_NODE, &exec_timeout_show_cmd);
}

void
vty_terminate (void)
{
    if (vty_cwd)
        XFREE (MTYPE_TMP, vty_cwd);

    if (vtyvec && Vvty_serv_thread) {
        vty_reset ();
        vector_free (vtyvec);
        vector_free (Vvty_serv_thread);
    }
}

/*************************************************************
 * cfg_header_check - 检查配置文件头
 * 
 * fp[in]: 配置文件
 * hdr[out]: 配置文件包含的头信息;NULL 不需要输出头信息
 *
 * Returns: 0 成功; -1 失败.
 *
 ************************************************************/
int cfg_header_check(FILE *fp, cfg_hdr_t *hdr)
{
    char buf[512] = {0};
    char * cfg_header_str[]= 
    {
        CFG_HEADER_STR(0),
        CFG_HEADER_STR(1),
        CFG_HEADER_STR(2)
    };
    int   cfg_header_str_num = sizeof(cfg_header_str) / sizeof(cfg_header_str[0]);
    int   match = 0;
    char  val[512] = {0};
    int   ret = 0;
    char  ver[VTY_VER_STR_MAX_LEN + 1] = {0};

    if (fp == NULL)
    {
        return -1;
    }

    if (hdr)
    {
        memset((char*)hdr, 0, sizeof(*hdr));
    }
    
    while((fgets(buf, sizeof(buf), fp) != NULL) && (match < cfg_header_str_num))
    {
        memset(val, 0, sizeof(val));
        /* 必须以#开头 */
        if ('#' != buf[0])
        {
            break;
        }
        switch(match)
        {
            case 0:
                ret = sscanf(buf, CFG_HEADER_STR(0) " %s\r\n", val);
                if (hdr && (1 == ret))
                    snprintf(hdr->user, sizeof(hdr->user), "%s", val);

                /* snmp保存的配置文件没有用户名 */
                if ((1 != ret) && !strncmp(buf, CFG_HEADER_STR(0), strlen(CFG_HEADER_STR(0))))
                {
                    ret = 1;
                }
                break;
            case 1:
                ret = sscanf(buf, CFG_HEADER_STR(1) " %s\r\n", val);
                break;
            case 2:
                snprintf(val, sizeof(val), "%s %%%ds\r\n", CFG_HEADER_STR(2), VTY_VER_STR_MAX_LEN);
                printf("%s: fmt---%s--\r\n", __func__, val);
                ret = sscanf(buf, val, ver);
                if (hdr && (1 == ret))
                    snprintf(hdr->ver, sizeof(hdr->ver), "%s", ver);
                break;
        }

        if (1 != ret)
        {
            break;
        }
        match++;
    }

    /* 回到文件开始位置 */
    fseek(fp, 0, SEEK_SET);

    printf("%s: match=%d ver=%s\r\n", __func__, match, ver);

    /* 兼容不带版本信息的老版本 */
    if (('V' != ver[0]) && (2 != match))
    {
        ret = -1;
    }
    else
    {
        ret = 0;
    }
    
    return ret;
}

