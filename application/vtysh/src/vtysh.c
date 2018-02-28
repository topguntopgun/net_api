/* Virtual terminal interface shell.
 * Copyright (C) 2000 Kunihiro Ishiguro
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
#include <dirent.h>
#include <readline/readline.h>
#include <readline/history.h>

#include "command.h"
#include "memory.h"
#include "vtysh.h"
#include "log.h"
#include "vty.h"
//#include "bgpd/bgp_vty.h"
#include "sigevent.h"
#include <global.h>
# include <termios.h>
#include "vtyCommon.h"
#include "../../switch/include/swAcl.h"
#include "tfNvramParam.h"

#include "log.h"
#include "monitor_pub.h"
/*Add by jq.deng at 2016.09.03(start)*/
#include "alarm.h"
#include "port_vif.h"
#include "ipc_if.h"
#include "ipc_public.h"
/*Add by jq.deng at 2016.09.03(end)*/

#include "tfSysCtrlPub.h"

//stephen.liu 20160811
extern vector cmdvec;


/* Struct VTY. */
struct vty *vtysh;

/* VTY shell pager name. */
char *vtysh_pager_name = NULL;

/* VTY shell client structure. */
struct vtysh_client
{
  int fd;
  const char *name;
  int flag;
  const char *path;
} vtysh_client[] =
{
    { .fd = -1, .name = "test", .flag = VTYSH_TEST, .path = TEST_VTYSH_PATH},
};


/* We need direct access to ripd to implement vtysh_exit_ripd_only. */
//static struct vtysh_client *ripd_client = NULL;


/* Using integrated config from Quagga.conf. Default is no. */
int vtysh_writeconfig_integrated = 1;

extern char config_default[];

static int
vtysh_exit (struct vty *vty);

static void
vclient_close (struct vtysh_client *vclient)
{
  if (vclient->fd >= 0)
    {
      /*fprinftf(stderr,
	      "Warning: closing connection to %s because of an I/O error!\n",
	      vclient->name);*/
      close (vclient->fd);
      vclient->fd = -1;
    }
}

/* Following filled with debug code to trace a problematic condition
 * under load - it SHOULD handle it. */
#define ERR_WHERE_STRING "vtysh(): vtysh_client_config(): "
static int
vtysh_client_config (struct vtysh_client *vclient, char *line)
{
    int ret;
    char *buf;
    size_t bufsz;
    char *pbuf;
    size_t left;
    char *eoln;
    int nbytes;
    int i;
    int readln;

    if (vclient->fd < 0)
        return CMD_SUCCESS;

    ret = write (vclient->fd, line, strlen (line) + 1);
    if (ret <= 0)
    {
        vclient_close (vclient);
        return CMD_SUCCESS;
    }

    /* Allow enough room for buffer to read more than a few pages from socket. */
    bufsz = 5 * getpagesize() + 1;
    buf = XMALLOC(MTYPE_TMP, bufsz);
    memset(buf, 0, bufsz);
    pbuf = buf;

    while (1)
    {
        if (pbuf >= ((buf + bufsz) -1))
        {
            fprintf(stderr, ERR_WHERE_STRING \
                "warning - pbuf beyond buffer end.\n");
            return CMD_WARNING;
        }

        readln = (buf + bufsz) - pbuf - 1;
        nbytes = read (vclient->fd, pbuf, readln);

        if (nbytes <= 0)
        {

            if (errno == EINTR)
                continue;

            /*fprinftf(stderr, ERR_WHERE_STRING "(%u)", errno);*/
            perror("");

            if (errno == EAGAIN || errno == EIO)
                continue;

            vclient_close (vclient);
            XFREE(MTYPE_TMP, buf);
            return CMD_SUCCESS;
        }

        pbuf[nbytes] = '\0';

        if (nbytes >= 4)
        {
            i = nbytes - 4;
            if (pbuf[i] == '\0' && pbuf[i + 1] == '\0' && pbuf[i + 2] == '\0')
            {
                ret = pbuf[i + 3];
                break;
            }
        }
        pbuf += nbytes;

        /* See if a line exists in buffer, if so parse and consume it, and
        * reset read position. */
        if ((eoln = strrchr(buf, '\n')) == NULL)
            continue;

        if (eoln >= ((buf + bufsz) - 1))
        {
            fprintf(stderr, ERR_WHERE_STRING \
                "warning - eoln beyond buffer end.\n");
        }
        vtysh_config_parse(buf);

        eoln++;
        left = (size_t)(buf + bufsz - eoln);
        memmove(buf, eoln, left);
        buf[bufsz-1] = '\0';
        pbuf = buf + strlen(buf);
    }

    /* Parse anything left in the buffer. */

    vtysh_config_parse (buf);

    XFREE(MTYPE_TMP, buf);
    return ret;
}





static int
vtysh_client_execute (struct vtysh_client *vclient, const char *line, FILE *fp)
{
  int ret;
  char buf[1001];
  int nbytes;
  int i;
  int numnulls = 0;

  if (vclient->fd < 0)
    return CMD_SUCCESS;

  ret = write (vclient->fd, line, strlen (line) + 1);
  if (ret <= 0)
    {
      vclient_close (vclient);
      return CMD_SUCCESS;
    }

  while (1)
    {
      nbytes = read (vclient->fd, buf, sizeof(buf)-1);

      if (nbytes <= 0 && errno != EINTR)
	{
	  vclient_close (vclient);
	  return CMD_SUCCESS;
	}

      if (nbytes > 0)
	{
	  if ((numnulls == 3) && (nbytes == 1))
	    return buf[0];

	  buf[nbytes] = '\0';
	  fputs (buf, fp);
	  fflush (fp);

	  /* check for trailling \0\0\0<ret code>,
	   * even if split across reads
	   * (see lib/vty.c::vtysh_read)
	   */
          if (nbytes >= 4)
            {
              i = nbytes-4;
              numnulls = 0;
            }
          else
            i = 0;

          while (i < nbytes && numnulls < 3)
            {
              if (buf[i++] == '\0')
                numnulls++;
              else
                numnulls = 0;
            }

          /* got 3 or more trailing NULs? */
          if ((numnulls >= 3) && (i < nbytes))
            return (buf[nbytes-1]);
	}
    }
}

int vtysh_node_get(const char *line)
{
    int vty_node = -1;
    while (isspace ((int) *line) && *line != '\0' && *line != '\r' && *line != '\n')
    {
        line++;
    }

    if (*line == '\0' || *line == '\r' || *line == '\n')
    {
        return -1;
    }

    switch (line[0])
    {
        case 'd':
        {
            if (strncmp(line, "dba-profile profile-id", strlen("dba-profile profile-id")) == 0)
            {
                vty_node = DBA_PROFILE_NODE;
            }
            break;
        }
        case 'o':
        {
            if (strncmp(line, "ont-lineprofile profile-id", strlen("ont-lineprofile profile-id")) == 0)
            {
                vty_node = LINE_PROFILE_NODE;
            }
            else if (strncmp(line, "ont-srvprofile profile-id", strlen("ont-srvprofile profile-id")) == 0)
            {
                vty_node = SRV_PROFILE_NODE;
            }
            else if (strncmp(line, "ont-slaprofile profile-id", strlen("ont-slaprofile profile-id")) == 0)
            {
                vty_node = SLA_PROFILE_NODE;
            }
            else if (strncmp(line, "classification profile-id", strlen("classification profile-id")) == 0)
            {
                vty_node = CLASSIFICATION_PROFILE_NODE;
            }
#if 0/*closed by Gerhard Lao	2016/02/29 */
            else if (strncmp(line, "ont-sipagent-profile profile-id", strlen("ont-sipagent-profile profile-id")) == 0)
            {
                vty_node = SIPAGENT_PROFILE_NODE;
            }
#endif
            break;
        }
        case 'i':
        {
            if (strncmp(line, "interface gtf", strlen("interface gtf")) == 0)
            {
                vty_node = INTERFACE_GNODE;
            }
            else if (strncmp(line, "interface ge", strlen("interface ge")) == 0)
            {
                vty_node = INTERFACE_GE_NODE;
            }
            else if (strncmp(line, "interface xge", strlen("interface xge")) == 0)
            {
                vty_node = INTERFACE_XGE_NODE;
            }
            else if (strncmp(line, "interface link-aggregation", strlen("interface link-aggregation")) == 0)
            {
                vty_node = INTERFACE_SA_NODE;
            }
            else if (strncmp(line, "interface mgmt", strlen("interface mgmt")) == 0)
            {
                vty_node = INTERFACE_MGMT_NODE;
            }
            else if (strncmp(line, "interface vlanif", strlen("interface vlanif")) == 0)
            {
                vty_node = INTERFACE_VLANIF_NODE;
            }
            break;
        }
        case 'm':
        {
            if (strncmp(line, "multicast-vlan", strlen("multicast-vlan")) == 0)
            {
                vty_node = INTERFACE_MVLAN_NODE;
            }
            else if(strncmp(line, "manu", 4) == 0)
            {
                vty_node = MANU_NODE;
            }
            break;
        }
        case 'a':
        {
            if (strncmp(line, "acl ipv6", strlen("acl ipv6")) == 0)
            {
                vty_node = ACL6_BASIC_NODE;
            }
            /* Other acl node must be judged before this */
            else if (strncmp(line, "acl ", strlen("acl ")) == 0)
            {
                vty_node = ACL_BASIC_NODE;
            }
            break;
        }
		case 'b':
        {
            if (strncmp(line, "btv", strlen("btv")) == 0)
            {
                vty_node = INTERFACE_BTV_NODE;
            }
            break;
        }
        default:
            break;
    }

    return vty_node;
}

int vtysh_cmd_execute (struct vty *vty, const char *cmd)
{
    //printf("cmd:%s\n", cmd);
    strcpy(vty->buf, cmd);
    vty->length = strlen(vty->buf);
    return vty_execute (vty);
}

/* Configration make from file. */
int
vtysh_config_from_file (struct vty *vty, FILE *fp)
{
    int ret;
    vector vline;
    struct cmd_element *cmd = NULL;
    int  skip = FALSE;
    char *cp = NULL;

    while (fgets (vty->buf, VTY_BUFSIZ, fp))
    {
        if (vty->buf[0] == '!' || vty->buf[1] == '#')
            continue;

        cp = vty->buf;
        while (isspace ((int) *cp) && *cp != '\0' && *cp != '\r' && *cp != '\n')
        {
            cp++;
        }

        if (*cp == '\0' || *cp == '\r' || *cp == '\n')
        {
            continue;
        }

        memmove(vty->buf, cp, VTY_BUFSIZ - (cp - vty->buf));

        /* skip error node command */
        if (skip)
        {
            if (strncmp(vty->buf, "exit", strlen("exit")) == 0)
            {
                skip = FALSE;
            }
            continue;
        }

        vline = cmd_make_strvec (vty->buf);

        /* In case of comment line. */
        if (vline == NULL)
            continue;

        /* Execute configuration command : this is strict match. */
        //ret = cmd_execute_command_strict (vline, vty, &cmd);
        ret = cmd_execute_command (vline, vty, &cmd, 0);

        cmd_free_strvec (vline);

        /* skip error node */
        if (ret != CMD_SUCCESS)
        {
            if (vtysh_node_get(vty->buf) >= 0)
            {
                skip = TRUE;
            }
        }

        switch (ret)
        {
            case CMD_WARNING:
                if (vty->type == VTY_FILE)
                    fprintf(stdout,"Warning...\r\n");
                break;
            case CMD_ERR_AMBIGUOUS:
                fprintf(stdout,"%% Ambiguous command.\r\n");
                break;
            case CMD_ERR_NO_MATCH:
                fprintf(stdout,"%% Unknown command: %s\r\n", vty->buf);
                break;
            case CMD_ERR_INCOMPLETE:
                fprintf(stdout,"%% Command incomplete.\r\n");
                break;
        }
    }
    return CMD_SUCCESS;
}

#if 000
int vtysh_execute_func_to_all_daemon(struct vty *vty, const char *line)
{
    int i, cmd_stat;
    printf("line:%s\n", line); //stephen, 20150928

    for (i = 0; i < array_size(vtysh_client); i++)
    {
        //cmd_stat = vtysh_client_daemon_execute(vty, &vtysh_client[i], line);
        if (cmd_stat == CMD_WARNING)
            break;
    }

    return 0;
}
#endif

extern void vtysh_config_parse_line (const char *line);
int vtysh_top_config_write()
{
    /* 日志优先级 */
    int prio;
    const char const * prioStr[] =
    {
        "emerg",
        "alert",
        "critical",
        "error",
        "warning",
        "notice",
        "info",
        "debug"
    };
    char line[128] = {0};

	if (host.name) {
		if (strcmp(host.name,HOST_NAME_DEFAULT) != 0) {
			snprintf(line, sizeof(line), "sysname %s", host.name);
			vtysh_config_parse_line(line);
		}
	}

    prio = tflog_alarm_priority_get();
    if (LOG_NOTICE != prio && prio != -1)
    {
        snprintf(line, sizeof(line), "alarm priority %s", prioStr[prio]);
        vtysh_config_parse_line(line);
    }

    prio = tflog_syslog_priority_get();
    if (LOG_DEBUG != prio && prio != -1)
    {
        snprintf(line, sizeof(line), "syslog priority severity %s", prioStr[prio]);
        vtysh_config_parse_line(line);
    }

    snmp_config_write();

    return 0;
}

void vtysh_set_termio_echo_flag_disable(void)
{
    struct termios termio;

    tcgetattr ( STDIN_FILENO,  &termio );

    //termio.c_lflag &= ~ICANON;
    termio.c_lflag &= ~ECHO;
    //termio.c_oflag &= ~TABDLY;
    //termio.c_oflag |=  TAB0;

    tcsetattr ( STDIN_FILENO, TCSANOW, &termio );
}

void vtysh_set_termio_echo_flag_enable(void)
{
    struct termios termio;

    tcgetattr ( STDIN_FILENO,  &termio );

    //termio.c_lflag |= ICANON;
    termio.c_lflag |= ECHO;
    //termio.c_oflag |= TABDLY;
    //termio.c_oflag &=  ~TAB0;

    tcsetattr ( STDIN_FILENO, TCSANOW, &termio );
}

 int

vtysh_execute (const char *line)
{
    //printf("line:%s\n", line);
    strcpy(vtysh->buf, line);
    vtysh->length = strlen(vtysh->buf);
    vty_execute (vtysh);

    if(vtysh->node == AUTH_NODE){
        vtysh_set_termio_echo_flag_disable();
    }else{
        vtysh_set_termio_echo_flag_enable();
    }
    return 0;
}

#if VTYSH_ENABLE_CONSOLE
/* Result of cmd_complete_command() call will be stored here
 * and used in new_completion() in order to put the space in
 * correct places only. */
int complete_status;

static char *
command_generator (const char *text, int state)
{
    vector vline;
    static char **matched = NULL;
    static int index = 0;

    /* First call. */
    if (! state){
        index = 0;

        if (vtysh->node == USER_NODE || vtysh->node == AUTH_NODE || vtysh->node == AUTH_ENABLE_NODE)
            return NULL;

        vline = cmd_make_strvec (rl_line_buffer);
        if (vline == NULL)
    	    return NULL;

        if (rl_end && isspace ((int) rl_line_buffer[rl_end - 1]))
    	    vector_set (vline, '\0');

        matched = cmd_complete_command (vline, vtysh, &complete_status);
    }

    if (matched && matched[index])
        return matched[index++];

    return NULL;
}


static char **
new_completion (char *text, int start, int end)
{
    char **matches;

    matches = rl_completion_matches (text, command_generator);

    if (matches)
    {
        rl_point = rl_end;
        if (complete_status != CMD_COMPLETE_FULL_MATCH)
        /* only append a space on full match */
        rl_completion_append_character = '\0';
    }

    return matches;
}

/* We don't care about the point of the cursor when '?' is typed. */
int
vtysh_rl_describe (void)
{
  int ret;
  unsigned int i;
  vector vline;
  vector describe;
  int width;
  struct cmd_token *token;

  /* Add by keith.gong 20151022 */
  if (vtysh->node == USER_NODE || vtysh->node == AUTH_NODE || vtysh->node == AUTH_ENABLE_NODE)
  {
      rl_insert_text("?");
      return 0;
  }

  vline = cmd_make_strvec (rl_line_buffer);

  /* In case of '> ?'. */
  if (vline == NULL)
    {
      vline = vector_init (1);
      vector_set (vline, '\0');
    }
  else
    if (rl_end && isspace ((int) rl_line_buffer[rl_end - 1]))
      vector_set (vline, '\0');

  describe = cmd_describe_command (vline, vtysh, &ret);

  fprintf(stdout,"\n");

  /* Ambiguous and no match error. */
  switch (ret)
    {
    case CMD_ERR_AMBIGUOUS:
      cmd_free_strvec (vline);
      fprintf(stdout,"%% Ambiguous command.\n");
      rl_on_new_line ();
      return 0;
      break;
    case CMD_ERR_NO_MATCH:
      cmd_free_strvec (vline);
      fprintf(stdout,"%% There is no matched command.\n");
      rl_on_new_line ();
      return 0;
      break;
    }

  /* Get width of command string. */
  width = 0;
  for (i = 0; i < vector_active (describe); i++)
    if ((token = vector_slot (describe, i)) != NULL)
      {
	int len;

	if (token->cmd[0] == '\0')
	  continue;

	len = strlen (token->cmd);
	if (token->cmd[0] == '.')
	  len--;

	if (width < len)
	  width = len;
      }

  for (i = 0; i < vector_active (describe); i++)
    if ((token = vector_slot (describe, i)) != NULL)
      {
	if (token->cmd[0] == '\0')
	  continue;

	if (! token->desc)
	  fprintf(stdout,"  %-s\n",
		   token->cmd[0] == '.' ? token->cmd + 1 : token->cmd);
	else
	  fprintf(stdout,"  %-*s  %s\n",
		   width,
		   token->cmd[0] == '.' ? token->cmd + 1 : token->cmd,
		   token->desc);
      }

  cmd_free_strvec (vline);
  vector_free (describe);

  rl_on_new_line();
  rl_set_prompt((char *)vty_prompt (vtysh));

  return 0;
}


#ifdef _ENABLE_VTY_SPACE_EXPAND_ /* Start Add by keith.gong 20151022 */
void vtysh_rl_backward_pure_word()
{
    int idx = rl_end;
    int count = 0;

    while (idx > 0 && rl_line_buffer[--idx] != ' ')
        count+=1;

    rl_backward(count, 0);
    rl_delete(count, 0);
}

int
vtysh_rl_complete()
{
    int i;
    int ret;
    char **matched = NULL;
    vector vline;

    if (vty->node == USER_NODE || vty->node == AUTH_NODE || vty->node == AUTH_ENABLE_NODE)
    {
        rl_insert_text(" ");
        return 0;
    }

    vline = cmd_make_strvec (rl_line_buffer);

    /* In case of '> \t'. */
    if (vline == NULL)
    {
        vline = vector_init (1);
        vector_set (vline, '\0');
    }
    else if (rl_end && isspace ((int) rl_line_buffer[rl_end - 1]))
        vector_set (vline, '\0');

    matched = cmd_complete_command (vline, vty, &ret);

    cmd_free_strvec (vline);

    switch (ret)
    {
        case CMD_ERR_AMBIGUOUS:
            fprintf(stdout, "%% Ambiguous command.%s", VTY_NEWLINE);
            rl_on_new_line();
            rl_set_prompt((char *)vty_prompt (vty));
            break;
        case CMD_ERR_NO_MATCH:
            /* fprintf(stdout, "%% There is no matched command.%s", VTY_NEWLINE); */
            break;
        case CMD_COMPLETE_FULL_MATCH:
            if (matched)
            {
                vtysh_rl_backward_pure_word();
                rl_insert_text(matched[0]);
                rl_insert_text (" ");
                XFREE (MTYPE_TMP, matched[0]);
            }
            else
                rl_insert_text (" ");
            break;
        case CMD_COMPLETE_MATCH:
            vtysh_rl_backward_pure_word();
            rl_insert_text(matched[0]);
            XFREE (MTYPE_TMP, matched[0]);
            vector_only_index_free (matched);
            return 0;
        case CMD_COMPLETE_LIST_MATCH:
            fprintf(stdout, "%s", VTY_NEWLINE);
            for (i = 0; matched[i] != NULL; i++)
            {
                if (i != 0 && ((i % 6) == 0))
                    fprintf(stdout, "%s", VTY_NEWLINE);
                fprintf(stdout, "%-10s ", matched[i]);
                XFREE (MTYPE_TMP, matched[i]);
            }
            fprintf(stdout, "%s%s", VTY_NEWLINE, VTY_NEWLINE);
            rl_on_new_line();
            rl_set_prompt((char *)vty_prompt (vty));
            break;
        case CMD_ERR_NOTHING_TODO:
            break;
        default:
            break;
    }

    if (matched)
        vector_only_index_free (matched);

    return 0;
}

#endif  /* End, Add by keith.gong 20151022 */
#endif

/* Start. Add by keith.gong 20161020 。用于日志告警逆序显示*/
#define LINE_MAX_LEN    512   /* 可以为任意值，vtysh_tac 可以将一行分几次输出*/
typedef struct LINE_NODE_ST{
    struct LINE_NODE_ST *pPrev;
    long offset;
}LINE_NODE;

int
vtysh_tac(struct vty *vty, const char *fileName, const bool rightCtrlFlag)
{
    FILE *pF;
    char *pBuf;
    LINE_NODE *pLineNodeHead = NULL, *pLineNode = NULL;
    int endChar;
    long lastOffset;
    char user[VTY_USER_MAX_LEN + 1];
    char *lfcr = NULL;

    pBuf = malloc(LINE_MAX_LEN);
    if(!pBuf)
    {
        perror("malloc:");
        return -1;
    }

    pF = fopen(fileName, "r");
    if(pF == NULL)
    {
        perror("fopen:");
        return -1;
    }

    if(rightCtrlFlag)
        snprintf(user, sizeof(user), "%s@", vty->sign_user.name);

    fseek(pF, 0, SEEK_SET);
    pBuf[LINE_MAX_LEN - 2] = 0;

    do
    {
        if(pBuf[LINE_MAX_LEN - 2]) /* buffer is full , go on reading */
        {
            fseek(pF, -1, SEEK_CUR);
            endChar = fgetc(pF);
            if(endChar != '\n')
                continue;

            pBuf[LINE_MAX_LEN - 2] = 0;
        }

        pLineNode = malloc(sizeof(LINE_NODE));
        if(!pLineNode)
        {
            perror("malloc:");
            return -1;
        }

        pLineNode->pPrev = pLineNodeHead;
        pLineNode->offset = ftell(pF);
        pLineNodeHead = pLineNode;
    }
    while(fgets(pBuf, LINE_MAX_LEN, pF));

    lastOffset = ftell(pF);
    while(pLineNodeHead)
    {
        fseek(pF, pLineNodeHead->offset, SEEK_SET);

        while(fgets(pBuf, LINE_MAX_LEN, pF))
        {
            if(rightCtrlFlag)
            {
                if (!cmd_element_access_allowed(CLI_ACCESS_ROOT, vty->sign_user.level))
                {
                    if (strstr(pBuf, user) == NULL)
                        break;
                }
            }

            if ((lfcr = strrchr(pBuf, '\n')) != NULL)
                *lfcr = '\0';

            vty_out(vty, "%s%s", pBuf, VTY_NEWLINE);
            if(ftell(pF) == lastOffset)
                break;
        }

        lastOffset = pLineNodeHead->offset;
        pLineNode = pLineNodeHead;
        pLineNodeHead = pLineNode->pPrev;
        free(pLineNode);
    }

    fclose(pF);
    free(pBuf);

    return 0;
}
/* End. Add by keith.gong 20161020 */


/* Vty node structures. */

#ifdef VTYSH_TEST_CMD
static struct cmd_node test_node =
{
  TEST_NODE,
  "%s(config-router-%s)# "
};
#endif

#if 0	/*close by jq.deng, 20151110*/
static struct cmd_node ge_node =
{
    INTERFACE_GE_NODE,
    "%s(config-interface-ge)# "
};
#endif

static struct cmd_node acl_basic_node = {
    ACL_BASIC_NODE,
    "%s(acl-basic-%d)# "
};

static struct cmd_node acl_adv_node = {
    ACL_ADV_NODE,
    "%s(acl-adv-%d)# "
};

#ifdef ACL_SUPPORT_IPV6
static struct cmd_node acl6_basic_node = {
    ACL6_BASIC_NODE,
    "%s(acl6-basic-%d)# "
};

static struct cmd_node acl6_adv_node = {
    ACL6_ADV_NODE,
    "%s(acl6-adv-%d)# "
};
#endif
static struct cmd_node acl_link_node = {
    ACL_LINK_NODE,
    "%s(acl-link-%d)# "
};

static struct cmd_node acl_user_node = {
    ACL_USER_NODE,
    "%s(acl-user-%d)# "
};

static struct cmd_node acl_node = {
    ACL_NODE,
    "%s(acl-tf-%d)# "
};

static struct cmd_node vtyNodeDbaProfile =
{
    DBA_PROFILE_NODE,
    "%s(config-dba-profile-%d)# "
};

static struct cmd_node vtyNodeDbaProfileInterAction =
{
    DBA_PROFILE_INTERACTION_NODE,
    "\r\nPlease commit this profile before exit, or the latest configuration will lost. Are you sure to continue? (y/n):"
};

static struct cmd_node vtyNodeLineProfile =
{
    LINE_PROFILE_NODE,
    "%s(config-ont-lineprofile-%d)# "
};

static struct cmd_node vtyNodeLineProfileInterAction =
{
    LINE_PROFILE_INTERACTION_NODE,
    "\r\nPlease commit this profile before exit, or the latest configuration will lost. Are you sure to continue? (y/n):"
};

static struct cmd_node vtyNodeSrvProfile =
{
    SRV_PROFILE_NODE,
    "%s(config-ont-srvprofile-%d)# "
};

static struct cmd_node vtyNodeSrvProfileInterAction =
{
    SRV_PROFILE_INTERACTION_NODE,
    "\r\nPlease commit this profile before exit, or the latest configuration will lost. Are you sure to continue? (y/n):"
};

static struct cmd_node vtyNodeSlaProfile =
{
    SLA_PROFILE_NODE,
    "%s(sla-%d)# "
};

static struct cmd_node vtyNodeSlaProfileInterAction =
{
    SLA_PROFILE_INTERACTION_NODE,
    "\r\nPlease commit this profile before exit, or the latest configuration will lost. Are you sure to continue? (y/n):"
};

#if 0/*closed by Gerhard Lao	2016/08/10 */
static struct cmd_node vtyNodeClassificationProfile =
{
    CLASSIFICATION_PROFILE_NODE,
    "%s(classification-%d)# "
};

static struct cmd_node vtyNodeClassificationProfileInterAction =
{
    CLASSIFICATION_PROFILE_INTERACTION_NODE,
    "\r\nPlease commit this profile before exit, or the latest configuration will lost. Are you sure to continue? (y/n):"
};
#endif

static struct cmd_node vtyNodeGtfOntDelInterAction =
{
    GONT_DELETE_ALL_INTERACTION_NODE,
    "\r\nThis command will delete all the ONTs in port. Are you sure to execute this command? (y/n):"
};

static struct cmd_node vtyNodeIfG=
{
    INTERFACE_GNODE,
    "%s(config-interface-gtf-%d)# "
};

static struct cmd_node vtyNodeIfGe =
{
    INTERFACE_GE_NODE,
    "%s(config-interface-ge)# "
};

static struct cmd_node vtyNodeIfXge =
{
    INTERFACE_XGE_NODE,
    "%s(config-interface-xge)# "
};

static struct cmd_node vtyNodeIfSa =
{
    INTERFACE_SA_NODE,
    "%s(config-interface-aggregation)# "
};

static struct cmd_node mgmt_if_node =
{
    INTERFACE_MGMT_NODE,
    "%s(config-interface-mgmt)# "
};

static struct cmd_node vlan_if_node =
{
    INTERFACE_VLANIF_NODE,
    "%s(config-interface-vlanif-%d)# "
};

static struct cmd_node vtyNodeMvlan =
{
    INTERFACE_MVLAN_NODE,
    "%s(config-multicast-vlan-%d)# "
};

static struct cmd_node vtyNodeBtv =
{
    INTERFACE_BTV_NODE,
    "%s(config-btv)# "
};

static struct cmd_node vtyNodeEnableRebootInterAction =
{
    ENABLE_REBOOT_INTERACTION_NODE,
    "  Please check whether data has saved, \r\n"
    "  the unsaved data will lose if reboot system.\r\n"
    "  Are you sure to reboot system? (y/n):"
};

static struct cmd_node vtyNodeConfigRebootInterAction =
{
    CONFIG_REBOOT_INTERACTION_NODE,
    "  Please check whether data has saved, \r\n"
    "  the unsaved data will lose if reboot system.\r\n"
    "  Are you sure to reboot system? (y/n):"
};

static struct cmd_node vtyNodeConfigLoadCfgInterAction =
{
    CONFIG_LOAD_CFG_INTERACTION_NODE,
    "  The new configuration file will overwrite the old one\r\n"
    "  Are you sure to load new configuration file? (y/n):"
};

static struct cmd_node vtyNodeConfigEraseCfgInterAction =
{
    CONFIG_ERASE_CFG_INTERACTION_NODE,
    "  This command will clear the active board data that has been saved\r\n"
    "  Please remember to backup the system configuration data\r\n"
    "  Are you sure to continue? (y/n):"
};

static struct cmd_node manu_node =
{
    MANU_NODE,
    "%s(manufacture)# "
};
static struct cmd_node vtyNodeConfigDisableDhcpSnoopingInterAction =
{
    CONFIG_DISABLE_DHCP_SNOOPING_NODE,
    "  The saved DHCP snooping configuratons wouldn't be restored after reboot system! \r\n"
    "  Are you sure to continue? (y/n):"
};


/* Defined in lib/vty.c */
extern struct cmd_node vty_node;


DEFUNSH (VTYSH_TEST,
	 router_test,
	 router_test_cmd,
	 "router test",
	 "Enable a routing process\n"
	 "Start test configuration\n")
{
  vty->node = TEST_NODE;
  return CMD_SUCCESS;
}

/*add by jq.deng, 20160304*/
DEFUNSH (VTYSH_DHCPSN,
    vtysh_disable_dhcp_snooping_yes,
    vtysh_disable_dhcp_snooping_yes_cmd,
    "y",
    "\n")
{
	if(vty->node==CONFIG_DISABLE_DHCP_SNOOPING_NODE)
		vty->node = CONFIG_NODE;
	return CMD_SUCCESS;
}

/*add by jq.deng, 20160304*/
DEFUNSH (VTYSH_DHCPSN,
    vtysh_disable_dhcp_snooping_no,
    vtysh_disable_dhcp_snooping_no_cmd,
    "n",
    "\n")
{
	if(vty->node==CONFIG_DISABLE_DHCP_SNOOPING_NODE)
		vty->node = CONFIG_NODE;
	return CMD_SUCCESS;
}

/*add by jq.deng, 20160304*/
DEFUNSH (VTYSH_DHCPSN,
    vty_dhcpsn_switch,
    vty_dhcpsn_switch_cmd,
    "dhcp-snooping (disable|enable)",
    "<Group> DHCP snooping command group\n"
    "Disable DHCP snooping\n"
    "Enable DHCP snooping\n")
{
	if(!strcmp(argv[0], "disable"))
	{
		if(vty->node==CONFIG_NODE)
       		vty->node = CONFIG_DISABLE_DHCP_SNOOPING_NODE;
	}
	return CMD_SUCCESS;
}



/*close by jq.deng, 20151110*/
#if 0
DEFUNSH (VTYSH_QOSACL | VTYSH_DEVCTRL|VTYSH_LAYER2,
    interface_ge,
    interface_ge_cmd,
    "interface ge",
     "Enable a ge process\n"
    "Start ge configuration\n")
{
  vty->node = INTERFACE_GE_NODE;
  return CMD_SUCCESS;
}
#endif

DEFUNSH (VTYSH_LAYER2|VTYSH_RSTP|VTYSH_LACP|VTYSH_DOT1X,
    vty_exit_interface_ge_node,
    vty_exit_interface_ge_node_cmd,
     "exit",
     "Exit current mode and down to previous mode\n")
{
    return vtysh_exit (vty);
}



DEFUNSH (VTYSH_ALL,
	 vtysh_enable,
	 vtysh_enable_cmd,
	 "enable",
	 "Turn on privileged mode command\n")
{
  //printf("defunsh enable.\n");  //stephen.liu debug , 20151008
  //vty->node = ENABLE_NODE;
  #if 0
  if ((host.enable == NULL && host.enable_encrypt == NULL) ||
      vty->type == VTY_SHELL_SERV)
    vty->node = ENABLE_NODE;
  else
    vty->node = AUTH_ENABLE_NODE;
  #endif

  vty->node = ENABLE_NODE;

  return CMD_SUCCESS;
}

DEFUNSH (VTYSH_ALL,
	 vtysh_disable,
	 vtysh_disable_cmd,
	 "disable",
	 "Turn off privileged mode command\n")
{
  if (vty->node == ENABLE_NODE)
    vty->node = VIEW_NODE;
  return CMD_SUCCESS;
}

DEFUNSH (VTYSH_ALL,
	 vtysh_config_terminal,
	 vtysh_config_terminal_cmd,
	 "config",
	 "Configuration from vty interface\n")
{
  vty->node = CONFIG_NODE;
  return CMD_SUCCESS;
}

static int
vtysh_exit (struct vty *vty)
{
    switch (vty->node)
    {
        case VIEW_NODE:
#if VTYSH_ENABLE_CONSOLE
            if(vty_shell(vty)){
                vty->node = USER_NODE;
                clear_history();
            }else
#endif
                vty->status = VTY_CLOSE;
        case ENABLE_NODE:
            vty->node = VIEW_NODE;
            break;
        case CONFIG_NODE:
        case DEBUG_NODE:
        case MANU_NODE:
            vty->node = ENABLE_NODE;
            break;
        case INTERFACE_NODE:
        case TEST_NODE:

        case INTERFACE_GE_NODE:
        case INTERFACE_XGE_NODE:
        case INTERFACE_SA_NODE:
        case INTERFACE_GNODE:
        case INTERFACE_MGMT_NODE:
        case INTERFACE_VLANIF_NODE:
		case INTERFACE_MVLAN_NODE:/* add by ben.zheng  2015.11.12*/
		case INTERFACE_BTV_NODE:

        case DBA_PROFILE_NODE:
        case LINE_PROFILE_NODE:
        case SRV_PROFILE_NODE:
        case SLA_PROFILE_NODE:
        case CLASSIFICATION_PROFILE_NODE:

        case ACL_BASIC_NODE:
	    case ACL_ADV_NODE:
        case ACL6_BASIC_NODE:
        case ACL6_ADV_NODE:
	    case ACL_LINK_NODE:
	    case ACL_USER_NODE:
        case ACL_NODE:
            //stephen.liu, add, 20150928
            //vtysh_execute("end");
            //vtysh_execute("configure");
            vty->node = CONFIG_NODE;
            break;
        default:
            break;
    }

    return CMD_SUCCESS;
}


DEFUNSH(VTYSH_TEST, test_config_input_yn,
       test_config_input_yn_cmd,
       "test input",
       "Test Configuration\n"
       "Input y or n\n")
{

#if 0
    int rv;
    char c = 0;
    int num = 0;
    char buf[512];
    int j;
    vtysh_client_info *vtysh_info = NULL;
    printf("input\n");

    vty_out(vty, "input Y/N? ");
    _vty_flush(vty);
    printf("start\n");
    rv = vty_command_get_data(vty->fd, buf);
    if(rv == RT_OK){
        vtysh_info = (vtysh_client_info *) vty->vtysh_info;
        prin("daemon_num=%d\n", vty->daemon_num);
        for (j = 0; j < vty->daemon_num; j++) {
            //ret = vtysh_client_config (&vtysh_info[j], cmd_line);
            if(vtysh_info[j].flag == VTYSH_TEST){
                send();
            }
        }
        if(buf[0] == 'y'){
            vty_out(vty, "  OK, test input success y!%s", VTY_NEWLINE);
        }
        if(buf[0] == 'n'){
            vty_out(vty, "  OK, test input success n!%s", VTY_NEWLINE);
        }
        //vty_out(vty, "your input:%s%s", buf, VTY_NEWLINE);
    }
#endif

    return CMD_SUCCESS;
}


/* End of configuration. */
DEFUNSH (VTYSH_ALL,vtysh_config_end_all,
       vtysh_config_end_all_cmd,
       "end",
       "End current mode and change to view mode")
{
    switch (vty->node)
    {
        case VIEW_NODE:
            /* Nothing to do. */
            break;
        case ENABLE_NODE:
        case CONFIG_NODE:
        case DEBUG_NODE:
        case INTERFACE_NODE:
        case TEST_NODE:
        case INTERFACE_GE_NODE:
        case INTERFACE_XGE_NODE:
        case INTERFACE_SA_NODE:
        case INTERFACE_GNODE:
		case INTERFACE_MGMT_NODE:
        case INTERFACE_VLANIF_NODE:
		case INTERFACE_MVLAN_NODE:/* add by ben.zheng  2015.11.12*/
		case INTERFACE_BTV_NODE:

        case DBA_PROFILE_NODE:
        case LINE_PROFILE_NODE:
        case SRV_PROFILE_NODE:
        case SLA_PROFILE_NODE:
        case CLASSIFICATION_PROFILE_NODE:
        case ACL_BASIC_NODE:
	    case ACL_ADV_NODE:
	    case ACL_LINK_NODE:
	    case ACL_USER_NODE:
        case ACL_NODE:
        case ACL6_BASIC_NODE:
        case ACL6_ADV_NODE:
        case MANU_NODE:
            vty->node = VIEW_NODE;
            break;
        default:
            break;
    }

    return CMD_SUCCESS;
}


DEFUNSH (VTYSH_ALL,
	 vtysh_exit_all,
	 vtysh_exit_all_cmd,
	 "exit",
	 "Exit current mode and down to previous mode\n")
{
  return vtysh_exit (vty);
}

ALIAS (vtysh_exit_all,
       vtysh_quit_all_cmd,
       "quit",
       "Exit current mode and down to previous mode\n")



DEFUNSH (VTYSH_TEST,
	 vtysh_exit_test,
	 vtysh_exit_test_cmd,
	 "exit",
	 "Exit current mode and down to previous mode\n")
{
  return vtysh_exit (vty);
}

ALIAS (vtysh_exit_test,
       vtysh_quit_test_cmd,
       "quit",
       "Exit current mode and down to previous mode\n")




DEFUNSH (VTYSH_ALL,
         vtysh_exit_line_vty,
         vtysh_exit_line_vty_cmd,
         "exit",
         "Exit current mode and down to previous mode\n")
{
  return vtysh_exit (vty);
}

ALIAS (vtysh_exit_line_vty,
       vtysh_quit_line_vty_cmd,
       "quit",
       "Exit current mode and down to previous mode\n")






/* Memory */
DEFUN (vtysh_show_memory,
       vtysh_show_memory_cmd,
       "show memory",
       SHOW_STR
       "Memory statistics\n")
{
  unsigned int i;
  int ret = CMD_SUCCESS;
  char line[] = "show memory\n";

  for (i = 0; i < array_size(vtysh_client); i++)
    if ( vtysh_client[i].fd >= 0 )
      {
        fprintf(stdout, "Memory statistics for %s:\n",
                 vtysh_client[i].name);
        ret = vtysh_client_execute (&vtysh_client[i], line, stdout);
        fprintf(stdout,"\n");
      }

  return ret;
}

/* Logging commands. */
DEFUN (vtysh_show_logging,
       vtysh_show_logging_cmd,
       "show logging",
       SHOW_STR
       "Show current logging configuration\n")
{
  unsigned int i;
  int ret = CMD_SUCCESS;
  char line[] = "show logging\n";

  for (i = 0; i < array_size(vtysh_client); i++)
    if ( vtysh_client[i].fd >= 0 )
      {
        fprintf(stdout,"Logging configuration for %s:\n",
                 vtysh_client[i].name);
        ret = vtysh_client_execute (&vtysh_client[i], line, stdout);
        fprintf(stdout,"\n");
      }

  return ret;
}

DEFUNSH (VTYSH_ALL,
	 vtysh_log_stdout,
	 vtysh_log_stdout_cmd,
	 "log stdout",
	 "Logging control\n"
	 "Set stdout logging level\n")
{
  return CMD_SUCCESS;
}

DEFUNSH (VTYSH_ALL,
	 vtysh_log_stdout_level,
	 vtysh_log_stdout_level_cmd,
	 "log stdout "LOG_LEVELS,
	 "Logging control\n"
	 "Set stdout logging level\n"
	 LOG_LEVEL_DESC)
{
  return CMD_SUCCESS;
}

DEFUNSH (VTYSH_ALL,
	 no_vtysh_log_stdout,
	 no_vtysh_log_stdout_cmd,
	 "no log stdout [LEVEL]",
	 NO_STR
	 "Logging control\n"
	 "Cancel logging to stdout\n"
	 "Logging level\n")
{
  return CMD_SUCCESS;
}

DEFUNSH (VTYSH_ALL,
	 vtysh_log_file,
	 vtysh_log_file_cmd,
	 "log file FILENAME",
	 "Logging control\n"
	 "Logging to file\n"
	 "Logging filename\n")
{
  return CMD_SUCCESS;
}

DEFUNSH (VTYSH_ALL,
	 vtysh_log_file_level,
	 vtysh_log_file_level_cmd,
	 "log file FILENAME "LOG_LEVELS,
	 "Logging control\n"
	 "Logging to file\n"
	 "Logging filename\n"
	 LOG_LEVEL_DESC)
{
  return CMD_SUCCESS;
}

DEFUNSH (VTYSH_ALL,
	 no_vtysh_log_file,
	 no_vtysh_log_file_cmd,
	 "no log file [FILENAME]",
	 NO_STR
	 "Logging control\n"
	 "Cancel logging to file\n"
	 "Logging file name\n")
{
  return CMD_SUCCESS;
}

ALIAS_SH (VTYSH_ALL,
	  no_vtysh_log_file,
	  no_vtysh_log_file_level_cmd,
	  "no log file FILENAME LEVEL",
	  NO_STR
	  "Logging control\n"
	  "Cancel logging to file\n"
	  "Logging file name\n"
	  "Logging level\n")

DEFUNSH (VTYSH_ALL,
	 vtysh_log_monitor,
	 vtysh_log_monitor_cmd,
	 "log monitor",
	 "Logging control\n"
	 "Set terminal line (monitor) logging level\n")
{
  return CMD_SUCCESS;
}

DEFUNSH (VTYSH_ALL,
	 vtysh_log_monitor_level,
	 vtysh_log_monitor_level_cmd,
	 "log monitor "LOG_LEVELS,
	 "Logging control\n"
	 "Set terminal line (monitor) logging level\n"
	 LOG_LEVEL_DESC)
{
  return CMD_SUCCESS;
}

DEFUNSH (VTYSH_ALL,
	 no_vtysh_log_monitor,
	 no_vtysh_log_monitor_cmd,
	 "no log monitor [LEVEL]",
	 NO_STR
	 "Logging control\n"
	 "Disable terminal line (monitor) logging\n"
	 "Logging level\n")
{
  return CMD_SUCCESS;
}

DEFUNSH (VTYSH_ALL,
	 vtysh_log_syslog,
	 vtysh_log_syslog_cmd,
	 "log syslog",
	 "Logging control\n"
	 "Set syslog logging level\n")
{
  return CMD_SUCCESS;
}

DEFUNSH (VTYSH_ALL,
	 vtysh_log_syslog_level,
	 vtysh_log_syslog_level_cmd,
	 "log syslog "LOG_LEVELS,
	 "Logging control\n"
	 "Set syslog logging level\n"
	 LOG_LEVEL_DESC)
{
  return CMD_SUCCESS;
}

DEFUNSH (VTYSH_ALL,
	 no_vtysh_log_syslog,
	 no_vtysh_log_syslog_cmd,
	 "no log syslog [LEVEL]",
	 NO_STR
	 "Logging control\n"
	 "Cancel logging to syslog\n"
	 "Logging level\n")
{
  return CMD_SUCCESS;
}

DEFUNSH (VTYSH_ALL,
	 vtysh_log_facility,
	 vtysh_log_facility_cmd,
	 "log facility "LOG_FACILITIES,
	 "Logging control\n"
	 "Facility parameter for syslog messages\n"
	 LOG_FACILITY_DESC)

{
  return CMD_SUCCESS;
}

DEFUNSH (VTYSH_ALL,
	 no_vtysh_log_facility,
	 no_vtysh_log_facility_cmd,
	 "no log facility [FACILITY]",
	 NO_STR
	 "Logging control\n"
	 "Reset syslog facility to default (daemon)\n"
	 "Syslog facility\n")

{
  return CMD_SUCCESS;
}

DEFUNSH_DEPRECATED (VTYSH_ALL,
		    vtysh_log_trap,
		    vtysh_log_trap_cmd,
		    "log trap "LOG_LEVELS,
		    "Logging control\n"
		    "(Deprecated) Set logging level and default for all destinations\n"
		    LOG_LEVEL_DESC)

{
  return CMD_SUCCESS;
}

DEFUNSH_DEPRECATED (VTYSH_ALL,
		    no_vtysh_log_trap,
		    no_vtysh_log_trap_cmd,
		    "no log trap [LEVEL]",
		    NO_STR
		    "Logging control\n"
		    "Permit all logging information\n"
		    "Logging level\n")
{
  return CMD_SUCCESS;
}

DEFUNSH (VTYSH_ALL,
	 vtysh_log_record_priority,
	 vtysh_log_record_priority_cmd,
	 "log record-priority",
	 "Logging control\n"
	 "Log the priority of the message within the message\n")
{
  return CMD_SUCCESS;
}

DEFUNSH (VTYSH_ALL,
	 no_vtysh_log_record_priority,
	 no_vtysh_log_record_priority_cmd,
	 "no log record-priority",
	 NO_STR
	 "Logging control\n"
	 "Do not log the priority of the message within the message\n")
{
  return CMD_SUCCESS;
}

DEFUNSH (VTYSH_ALL,
	 vtysh_log_timestamp_precision,
	 vtysh_log_timestamp_precision_cmd,
	 "log timestamp precision <0-6>",
	 "Logging control\n"
	 "Timestamp configuration\n"
	 "Set the timestamp precision\n"
	 "Number of subsecond digits\n")
{
  return CMD_SUCCESS;
}

DEFUNSH (VTYSH_ALL,
	 no_vtysh_log_timestamp_precision,
	 no_vtysh_log_timestamp_precision_cmd,
	 "no log timestamp precision",
	 NO_STR
	 "Logging control\n"
	 "Timestamp configuration\n"
	 "Reset the timestamp precision to the default value of 0\n")
{
  return CMD_SUCCESS;
}

DEFUNSH (VTYSH_ALL,
	 vtysh_service_password_encrypt,
	 vtysh_service_password_encrypt_cmd,
	 "service password-encryption",
	 "Set up miscellaneous service\n"
	 "Enable encrypted passwords\n")
{
  return CMD_SUCCESS;
}

DEFUNSH (VTYSH_ALL,
	 no_vtysh_service_password_encrypt,
	 no_vtysh_service_password_encrypt_cmd,
	 "no service password-encryption",
	 NO_STR
	 "Set up miscellaneous service\n"
	 "Enable encrypted passwords\n")
{
  return CMD_SUCCESS;
}

DEFUNSH (VTYSH_ALL,
	 vtysh_config_password,
	 vtysh_password_cmd,
	 "password (8|) WORD",
	 "Assign the terminal connection password\n"
	 "Specifies a HIDDEN password will follow\n"
	 "dummy string \n"
	 "The HIDDEN line password string\n")
{
  return CMD_SUCCESS;
}

DEFUNSH (VTYSH_ALL,
	 vtysh_password_text,
	 vtysh_password_text_cmd,
	 "password LINE",
	 "Assign the terminal connection password\n"
	 "The UNENCRYPTED (cleartext) line password\n")
{
  return CMD_SUCCESS;
}

DEFUNSH (VTYSH_ALL,
	 vtysh_config_enable_password,
	 vtysh_enable_password_cmd,
	 "enable password (8|) WORD",
	 "Modify enable password parameters\n"
	 "Assign the privileged level password\n"
	 "Specifies a HIDDEN password will follow\n"
	 "dummy string \n"
	 "The HIDDEN 'enable' password string\n")
{
  return CMD_SUCCESS;
}

DEFUNSH (VTYSH_ALL,
	 vtysh_enable_password_text,
	 vtysh_enable_password_text_cmd,
	 "enable password LINE",
	 "Modify enable password parameters\n"
	 "Assign the privileged level password\n"
	 "The UNENCRYPTED (cleartext) 'enable' password\n")
{
  return CMD_SUCCESS;
}

DEFUNSH (VTYSH_ALL,
	 no_vtysh_config_enable_password,
	 no_vtysh_enable_password_cmd,
	 "no enable password",
	 NO_STR
	 "Modify enable password parameters\n"
	 "Assign the privileged level password\n")
{
  return CMD_SUCCESS;
}

DEFUN (vtysh_write_terminal,
       vtysh_write_terminal_cmd,
       "show current-config (rstp|etf|swAcl|layer2|igmpsn|dhcpsn|lacp|dot1x|hal|sysctrl)",
       "Show running configuration to memory, network, or terminal\n"
       "Show to terminal\n"
       "rstp\n"
       "etf\n"
       "swAcl\n"
       "layer2\n"
       "igmpsn\n"
       "dhcpsn\n"
       "lacp\n"
       "dotx1\n"
       "hal\n"
       "sysctrl\n")
{
  u_int idx;
  int ret;
  char *cmd_line = NULL;
  struct vtysh_client *vtysh_info = NULL;

  if (argc != 0) {
      cmd_line = "show current-config";
      if(vty->type == VTY_TERM || vty->type == VTY_TERM_LOCAL || vty->type == VTY_SSH){
        vtysh_info = (struct vtysh_client *)vty->vtysh_info;
        for (idx = 0; idx < vty->daemon_num; idx++){
            if (strcmp(argv[0],vtysh_info[idx].name) == 0) {
                vty_out (vty, "%sCurrent configuration for module[%s]:%s", VTY_NEWLINE,vtysh_info[idx].name,
                      VTY_NEWLINE);
                vty_out (vty, "!%s", VTY_NEWLINE);
                vtysh_config_start();
                ret = vtysh_client_config (&vtysh_info[idx], cmd_line);
                if(ret != RT_OK){
                    fprintf(stdout, "vtysh execute [%s] failed.\r\n", vtysh_info[idx].name);
                }
                vtysh_top_config_write ();
                vtysh_show_running_config (vty);
                vty_out (vty, "end%s", VTY_NEWLINE);
            }
        }
     }
  } else {
      vty_out (vty, "%sCurrent configuration:%s", VTY_NEWLINE,
    	   VTY_NEWLINE);
      vty_out (vty, "!%s", VTY_NEWLINE);

      /*printf("start.cmdstr:%s\n", self->string);*/
      cmd_line = (char *)self->string;

      vtysh_config_start();

      //2 stephen.liu modified , 20151009
      if(vty->type == VTY_TERM || vty->type == VTY_TERM_LOCAL || vty->type == VTY_SSH){
            vtysh_info = (struct vtysh_client *)vty->vtysh_info;
            /*fprintf(stdout, "daemon_num=%d\r\n", vty->daemon_num);*/
            for (idx = 0; idx < vty->daemon_num; idx++){
                ret = vtysh_client_config (&vtysh_info[idx], cmd_line);
                if(ret != RT_OK){
                    fprintf(stdout, "vtysh execute [%s] failed.\r\n", vtysh_info[idx].name);
                }
            }
        }
      /* Integrate vtysh specific configuration. */
      vtysh_top_config_write ();

      vtysh_show_running_config (vty);


      vty_out (vty, "end%s", VTY_NEWLINE);
  }
  return CMD_SUCCESS;
}

ALIAS(vtysh_write_terminal,
    vtysh_system_show_current_cfg_cmd,
    "show current-config",
    DESC_SHOW
    "Show current configuration\n")

DEFUN (vtysh_integrated_config,
       vtysh_integrated_config_cmd,
       "service integrated-vtysh-config",
       "Set up miscellaneous service\n"
       "Write configuration into integrated file\n")
{
  vtysh_writeconfig_integrated = 1;
  return CMD_SUCCESS;
}

DEFUN (no_vtysh_integrated_config,
       no_vtysh_integrated_config_cmd,
       "no service integrated-vtysh-config",
       NO_STR
       "Set up miscellaneous service\n"
       "Write configuration into integrated file\n")
{
  vtysh_writeconfig_integrated = 0;
  return CMD_SUCCESS;
}

#if 0
DEFUN (vtysh_default_interface,
       vtysh_default_interface_cmd,
       "default interface (ge "GE_CMD_STR"|xge "XGE_CMD_STR"|e"ECMD_STR"|lagstatic "SA_CMD_STR"|laglacp "LACP_CMD_STR")",
       "restore a interface's config to default\n"
       "based on interface\n")
{
    int ret = CMD_SUCCESS;
    vtysh_client_info *vtysh_info = NULL;
    int j;
    char *cmd_line = NULL;

    cmd_line = (char *)vty->buf;
    fprinftf(stdout,"\r\ncmd_str: %s %s %s %s %s\r\n",
            GE_CMD_STR, XGE_CMD_STR, ECMD_STR, SA_CMD_STR, LACP_CMD_STR);

    if (vty->type == VTY_TERM || vty->type == VTY_TERM_LOCAL || vty->type == VTY_SSH)
    {
        vtysh_info = (vtysh_client_info *)vty->vtysh_info;
        for (j = vty->daemon_num -1; j >= 0 ; j--) {
            if (! strncmp("lacp", vtysh_info[j].name, strlen("lacp"))
                || ! strncmp("dot1x",vtysh_info[j].name, strlen("dot1x")))
            {
                ret = vtysh_client_daemon_execute (vty, &vtysh_info[j], cmd_line);
                if(ret != RT_OK) {
                    fprintf(stdout,"vtysh execute [%s] failed.\r\n", vtysh_info[j].name);
                }
            }
        }
    }

    vty_out(vty, "%sdefault interface's config done%s", VTY_NEWLINE, VTY_NEWLINE);
    return CMD_SUCCESS;
}
#endif

#if 0
static int
write_config_integrated(void)
{
  u_int i;
  int ret;
  char line[] = "write terminal\n";
  FILE *fp;
  char *integrate_sav = NULL;

  integrate_sav = malloc (strlen (integrate_default) +
			  strlen (CONF_BACKUP_EXT) + 1);
  strcpy (integrate_sav, integrate_default);
  strcat (integrate_sav, CONF_BACKUP_EXT);

  fprintf(stdout,"Building Configuration...\n");

  /* Move current configuration file to backup config file. */
  unlink (integrate_sav);
  rename (integrate_default, integrate_sav);
  free (integrate_sav);

  fp = fopen (integrate_default, "w");
  if (fp == NULL)
    {
      fprintf(stdout,"%% Can't open configuration file %s.\n",
	       integrate_default);
      return CMD_SUCCESS;
    }

  for (i = 0; i < array_size(vtysh_client); i++)
    ret = vtysh_client_config (&vtysh_client[i], line);

  vtysh_config_dump (fp);

  fclose (fp);

  if (chmod (integrate_default, CONFIGFILE_MASK) != 0)
    {
      fprintf(stdout,"%% Can't chmod configuration file %s: %s (%d)\n",
	integrate_default, safe_strerror(errno), errno);
      return CMD_WARNING;
    }

  fprinftf(stdout,"Integrated configuration saved to %s\n",integrate_default);

  fprintf(stdout,"[OK]\n");

  return CMD_SUCCESS;
}
#endif

int vtysh_reboot_req(int type)
{
    tfSysRebootCtrl_t rebootCtrl;
    short retCode = 0;
    int   rc = 0;

    memset(&rebootCtrl, 0, sizeof(rebootCtrl));
    rebootCtrl.type = type;
    rc = ipc_if_exe_cmd(MODULE_SYSCTRL, IPC_SYS_REBOOT_CTRL, (char*)&rebootCtrl, sizeof(rebootCtrl), &retCode);

    return rc | retCode;
}

static int
write_config_integrated_vtysh(struct vty* vty, char *line)
{
    u_int i;
    int ret;
    FILE *fp;
    char *integrate_sav = NULL;
    vtysh_client_info *vtysh_info = NULL;
    char msg[128] = {0};
    #ifdef VTYSH_CONFIG_PROGRESS
    struct timespec *tsp = NULL;
    struct timespec  res;
    #endif

    fprintf(stdout,"Building Configuration...\r\n");

    if (opendir(SYSCONFDIR) == NULL)
    {
        if (mkdir(SYSCONFDIR, 0775) != 0)
        {
            return CMD_SUCCESS;
        }
    }

    #if 1 /* Unnecessary */
    if (access(integrate_default, F_OK) == 0)
    {
        integrate_sav = malloc (strlen (integrate_default) + strlen (CONF_BACKUP_EXT) + 1);
        strcpy (integrate_sav, integrate_default);
        strcat (integrate_sav, CONF_BACKUP_EXT);
        /* Move current configuration file to backup config file. */
        unlink (integrate_sav);
        rename (integrate_default, integrate_sav);
        free (integrate_sav);
    }
    #endif

    fp = fopen (integrate_default, "w+");
    if (fp == NULL)
    {
        fprintf(stdout,"%% Can't open configuration file %s.\r\n", integrate_default);
        return CMD_SUCCESS;
    }

    vtysh_reboot_req(SYS_REBOOT_DISABLE);

    vtysh_config_start();

    #ifdef VTYSH_CONFIG_PROGRESS
    tsp = XMALLOC(MTYPE_TMP, sizeof(struct timespec) * (vty->daemon_num + 3));
    clock_gettime(CLOCK_MONOTONIC, &tsp[0]);
    #endif

    if(vty->type == VTY_TERM || vty->type == VTY_TERM_LOCAL || vty->type == VTY_SHELL_SERV || vty->type == VTY_SSH){
        vtysh_info = (vtysh_client_info *)vty->vtysh_info;
        /*fprintf(stdout, "daemon_num=%d\r\n", vty->daemon_num);*/
        for (i = 0; i < vty->daemon_num; i++){
            #ifdef VTYSH_CONFIG_PROGRESS
            clock_gettime(CLOCK_MONOTONIC, &tsp[i+1]);
            #endif
            snprintf(msg, sizeof(msg), "\r\n The percentage of saved data is: %d%% \r\n", 50 * i / vty->daemon_num);
            vty_out_to_all(msg);
            ret = vtysh_client_config ((struct vtysh_client *)&vtysh_info[i], "write terminal\n");
            if(ret != RT_OK){
                fprintf(stdout, "vtysh execute [%s] failed.\r\n", vtysh_info[i].name);
            }
        }
    }

    vtysh_top_config_write();

    #ifdef VTYSH_CONFIG_PROGRESS
    clock_gettime(CLOCK_MONOTONIC, &tsp[i++]);
    #endif
    vtysh_config_header(vty, fp);
    vtysh_config_dump (fp);
    snprintf(msg, sizeof(msg), "\r\n The percentage of saved data is: 100%% \r\n");
    vty_out_to_all(msg);
    #ifdef VTYSH_CONFIG_PROGRESS
    clock_gettime(CLOCK_MONOTONIC, &tsp[i++]);

    clock_getres(CLOCK_MONOTONIC, &res);
    fprinftf(stderr, "CLOCK_MONOTONIC resolution: %d.%ld\n", (int)res.tv_sec, res.tv_nsec);
    for(i = 0; i < vty->daemon_num + 3; i++)
    {
        fprinftf(stderr, "idx[%d] timespec=%d.%ld\n", i, (int)tsp[i].tv_sec, tsp[i].tv_nsec);
    }

    if (tsp != NULL)
        XFREE(MTYPE_TMP, tsp);
    #endif

    fclose (fp);

    vtysh_reboot_req(SYS_REBOOT_ENABLE);

    if (chmod (integrate_default, CONFIGFILE_MASK) != 0)
    {
        fprintf(stdout,"%% Can't chmod configuration file %s: %s (%d)\r\n",
                 integrate_default, safe_strerror(errno), errno);
        return CMD_WARNING;
    }

    fprinftf(stdout,"Integrated configuration saved to %s\r\n",integrate_default);

    fprintf(stdout,"[OK]\r\n");

    return CMD_SUCCESS;
}


int vtysh_write_config_file(struct vty *vty)
{
    extern vector cmdvec;
    unsigned int i;
    int fd;
    struct cmd_node *node;
    char *config_file;
    char *config_file_tmp = NULL;
    char *config_file_sav = NULL;
    int ret = CMD_SUCCESS;
    struct vty *file_vty;

    /* Check and see if we are operating under vtysh configuration */
    if (host.config == NULL){
        vty_out (vty, "PID[%d] Can't save to configuration file, using vtysh.%s",
                getpid(), VTY_NEWLINE);
        return CMD_SUCCESS;
    }

    /* Get filename. */
    config_file = host.config;

    config_file_sav =
    XMALLOC (MTYPE_TMP, strlen (config_file) + strlen (CONF_BACKUP_EXT) + 1);
    strcpy (config_file_sav, config_file);
    strcat (config_file_sav, CONF_BACKUP_EXT);


    config_file_tmp = XMALLOC (MTYPE_TMP, strlen (config_file) + 8);
    sprin(config_file_tmp, "%s.XXXXXX", config_file);

    /* Open file to configuration write. */
    fd = mkstemp (config_file_tmp);
    if (fd < 0)
    {
        vty_out (vty, "Can't open configuration file %s.%s", config_file_tmp,
        VTY_NEWLINE);
        goto finished;
    }

    /* Make vty for configuration file. */
    file_vty = vty_new ();
    file_vty->fd = fd;
    file_vty->type = VTY_FILE;

    /* Config file header print. */
    vty_out (file_vty, "!%s! Zebra configuration saved from vty%s!   ",VTY_NEWLINE,VTY_NEWLINE);
    vty_time_print (file_vty, 1);
    vty_out (file_vty, "!%s",VTY_NEWLINE);

    for (i = 0; i < vector_active (cmdvec); i++)
        if ((node = vector_slot (cmdvec, i)) && node->func)
        {
            if ((*node->func) (file_vty))
            vty_out (file_vty, "!%s",VTY_NEWLINE);
        }
    vty_close (file_vty);

    if (unlink (config_file_sav) != 0)
        if (errno != ENOENT)
        {
            vty_out (vty, "Can't unlink backup configuration file %s.%s", config_file_sav,
            VTY_NEWLINE);
            goto finished;
        }
    if (link (config_file, config_file_sav) != 0)
    {
        vty_out (vty, "Can't backup old configuration file %s.%s", config_file_sav,
        VTY_NEWLINE);
        goto finished;
    }
    sync ();
    if (unlink (config_file) != 0)
    {
        vty_out (vty, "Can't unlink configuration file %s.%s", config_file,
        VTY_NEWLINE);
        goto finished;
    }
    if (link (config_file_tmp, config_file) != 0)
    {
        vty_out (vty, "Can't save configuration file %s.%s", config_file,
        VTY_NEWLINE);
        goto finished;
    }
    sync ();

    if (chmod (config_file, CONFIGFILE_MASK) != 0)
    {
        vty_out (vty, "Can't chmod configuration file %s: %s (%d).%s",
        config_file, safe_strerror(errno), errno, VTY_NEWLINE);
        goto finished;
    }

    vty_out (vty, "Configuration saved to %s%s", config_file,
    VTY_NEWLINE);
    ret = CMD_SUCCESS;

finished:
    unlink (config_file_tmp);
    XFREE (MTYPE_TMP, config_file_tmp);
    XFREE (MTYPE_TMP, config_file_sav);
    return ret;
}



DEFUN (vtysh_write_memory,
       vtysh_write_memory_cmd,
       "write memory",
       "Write running configuration to memory, network, or terminal\n"
       "Write configuration to the file (same as write file)\n")
{
    int ret = CMD_SUCCESS;
    vtysh_client_info *vtysh_info = NULL;
    u_int j;
    char *cmd_line = NULL;

    /*fprinftf(stdout, "start.cmdstr:%s\r\n", self->string);*/
    cmd_line = (char *)self->string;

    /* If integrated Quagga.conf explicitely set. */
    if (vtysh_writeconfig_integrated)
        return write_config_integrated_vtysh(vty, cmd_line);

    fprintf(stdout,"Building Configuration...\r\n");
	//remote write
    if(vty->type == VTY_TERM || vty->type == VTY_TERM_LOCAL || vty->type == VTY_SSH){
        vtysh_info = (vtysh_client_info *)vty->vtysh_info;
        /*fprintf(stdout,"daemon_num=%d\r\n", vty->daemon_num);*/
        for (j = 0; j < vty->daemon_num; j++){
            ret = vtysh_client_daemon_execute (vty, &vtysh_info[j], cmd_line);
            if(ret != RT_OK){
                fprintf(stdout,"vtysh execute [%s] failed.\r\n", vtysh_info[j].name);
            }
        }
    }

    //local write, now not write
    vtysh_write_config_file(vty);

    return CMD_SUCCESS;
}

ALIAS (vtysh_write_memory,
       vtysh_copy_runningconfig_startupconfig_cmd,
       "copy running-config startup-config",
       "Copy from one file to another\n"
       "Copy from current system configuration\n"
       "Copy to startup configuration\n")

ALIAS (vtysh_write_memory,
       vtysh_write_file_cmd,
       "write file",
       "Write running configuration to memory, network, or terminal\n"
       "Write configuration to the file (same as write memory)\n")

ALIAS (vtysh_write_memory,
       vtysh_write_cmd,
       "write",
       "Write running configuration to memory, network, or terminal\n")

ALIAS (vtysh_write_terminal,
       vtysh_show_running_config_cmd,
       "show running-config",
       SHOW_STR
       "Current operating configuration\n")

ALIAS (vtysh_write_memory,
       vtysh_system_cfg_save_cmd,
       "save",
       "Save configuration information\n")

DEFUN (vtysh_terminal_length,
       vtysh_terminal_length_cmd,
       "terminal length <0-512>",
       "Set terminal line parameters\n"
       "Set number of lines on a screen\n"
       "Number of lines on screen (0 for no pausing)\n")
{
  int lines;
  char *endptr = NULL;
  char default_pager[10];

  lines = strtol (argv[0], &endptr, 10);
  if (lines < 0 || lines > 512 || *endptr != '\0')
    {
      vty_out (vty, "length is malformed%s", VTY_NEWLINE);
      return CMD_WARNING;
    }

  if (vtysh_pager_name)
    {
      free (vtysh_pager_name);
      vtysh_pager_name = NULL;
    }

  if (lines != 0)
    {
      snprintf(default_pager, 10, "more -%i", lines);
      vtysh_pager_name = strdup (default_pager);
    }

  return CMD_SUCCESS;
}


DEFUN (vtysh_show_daemons,
       vtysh_show_daemons_cmd,
       "show daemons",
       SHOW_STR
       "Show list of running daemons\n")
{
  u_int i;

  for (i = 0; i < array_size(vtysh_client); i++)
    if ( vtysh_client[i].fd >= 0 )
      vty_out(vty, " %s", vtysh_client[i].name);
  vty_out(vty, "%s", VTY_NEWLINE);

  return CMD_SUCCESS;
}

/* Execute command in child process. */
static int
execute_command_out (struct vty *vty, const char *command, const char *args)
{
    char cmd[1024] = {0};
    int cmdLen;
    #if 1
    FILE *fp = NULL;
    #else
    pid_t pid = 0;
    int   fds[2] = {0};
    int   nbytes = 0;
    int status;
    int ret;
    char *pnext = NULL;
    #endif

    if (vty == NULL || command == NULL)
        return -1;

    if (args == NULL)
    {
        snprintf(cmd, sizeof(cmd), "%s", command);
    }
    else
    {
        snprintf(cmd, sizeof(cmd), "%s %s", command, args);
    }

    #if 1
    fp = popen(cmd, "r");
    if (fp == NULL)
    {
        vty_out_line(vty, "  Error: Execute %s failed!", command);
        return -1;
    }

    while (fgets(cmd, sizeof(cmd), fp) != NULL)
    {
        /* modified by keith.gong 20160128 */
        cmdLen = strlen(cmd);
        if(cmdLen < 2 || (cmd[cmdLen-1] != '\r' && cmd[cmdLen-2] != '\r'))
            vty_out(vty, "%s\r", cmd);
        else
            vty_out(vty, "%s", cmd);
        _vty_flush(vty);
    }
    pclose(fp);
    #else
    if (pipe(fds) < 0)
    {
        vty_out_line(vty, "pipe error :%s", strerror(errno));
        return -1;
    }

    pid = fork();
    if (pid == 0)
    {
        close(fds[0]);/* close read */
        dup2(fds[1], STDOUT_FILENO);
        dup2(fds[1], STDERR_FILENO);
        execlp("ping", "ping", args, "-c", "4", NULL);
    }
    else if (pid > 0)
    {
        close(fds[1]);
        memset(cmd, 0, sizeof(cmd));
        pnext = cmd;
        while((nbytes = read(fds[0], pnext, sizeof(cmd) - 1 - (pnext - cmd))) > 0)
        {
            pnext[nbytes] = '\0';
            if (strchr(cmd, '\n') != NULL)
            {
                vty_out_line(vty, "%.*s", nbytes, cmd);
                pnext = cmd;
            }
            else
                pnext = &pnext[nbytes];
        }
        ret = wait4 (pid, &status, 0, NULL);
    }
    else
    {
        vty_out_line(vty, "  Error: Execute %s failed!", command);
    }
    #endif
    return 0;
}


/* Execute command in child process. */
static int
execute_command (int fd, const char *command, int argc, const char *arg1,
		 const char *arg2)
{
    int ret;
    pid_t pid;
    int status;

    /* Call fork(). */
    pid = fork ();

    if (pid < 0)
    {
        /* Failure of fork(). */
        fprintf(stderr, "Can't fork: %s\n", safe_strerror (errno));
        exit (1);
    }
    else if (pid == 0)
    {
        dup2(fd, STDIN_FILENO);
        dup2(fd, STDOUT_FILENO);
        dup2(fd, STDERR_FILENO);
        /* This is child process. */
        switch (argc)
        {
            case 0:
                ret = execlp (command, command, (const char *)NULL);
                break;
            case 1:
                ret = execlp (command, command, arg1, (const char *)NULL);
                break;
            case 2:
                ret = execlp (command, command, arg1, arg2, (const char *)NULL);
                break;
        }

        /* When execlp suceed, this part is not executed. */
        fprintf(stderr, "Can't execute %s: %s\n", command, safe_strerror (errno));
        exit (1);
    }
    else
    {
        /* This is parent. */
        execute_flag = 1;
        ret = wait4 (pid, &status, 0, NULL);
        execute_flag = 0;
    }
    return 0;
}
#if 0
DEFUN (vtysh_ping,
       vtysh_ping_cmd,
       "ping DESTINATION {size <64-1518>|count <5-10000>}",
       "Send echo messages\n"
       "Ping destination address or hostname\n"
       "Size of the ICMP packet\n"
       "Size of the ICMP packet\n"
       "Number of the packet to send\n"
       "Number of the packet to send\n")
{
    char args[256] = {0};
	
#if 1
	if (argc == 1) {
    	snprintf(args, sizeof(args), "%s -c 4", argv[0]);
	} else {
		if ((argv[1] != NULL)&&(argv[2] == NULL)) {
			snprintf(args, sizeof(args), "%s -c 4 -s %s", argv[0],argv[1]);
		} else if ((argv[2] != NULL)&&(argv[1] == NULL)){
			snprintf(args, sizeof(args), "%s -c %s", argv[0],argv[2]);
		} else if ((argv[1] != NULL)&&(argv[2] != NULL)){
			snprintf(args, sizeof(args), "%s -c %s -s %s", argv[0],argv[2],argv[1]);
		}
	} 

    execute_command_out (vty, "ping", args);
#endif	
    return CMD_SUCCESS;
}

ALIAS (vtysh_ping,
       vtysh_ping_ip_cmd,
       "ping DESTINATION",
       "Send echo messages\n"
       "Ping destination address or hostname\n")
#endif
DEFUN (vtysh_ping,
       vtysh_ping_cmd,
       "ping DESTINATION",
       "Send echo messages\n"
       "Ping destination address or hostname\n")
{
    char args[256] = {0};
    snprintf(args, sizeof(args), "%s -c 4 -w 5", argv[0]);

    execute_command_out (vty, "ping", args);
    return CMD_SUCCESS;
}

ALIAS (vtysh_ping,
       vtysh_ping_ip_cmd,
       "ping ip WORD",
       "Send echo messages\n"
       "IP echo\n"
       "Ping destination address or hostname\n")

DEFUN (vtysh_traceroute,
       vtysh_traceroute_params_cmd,
       "traceroute DESTINATION {ttl <1-30> | hops <1-40> | timeout <1-5>}",
       "Trace route to destination\n"
       "Trace route to destination address or hostname\n"
       "Number of probes per TTL\n"
       "Number of probes per TTL\n"
       "Max time-to-live\n"
       "Max time-to-live\n"
       "Time in seconds to wait for a restfse\n"
       "Time in seconds to wait for a restfse\n")
{
    char args[256] = {0};
    char tmp[256] = {0};

    snprintf(args, sizeof(args), "%s -I -n", argv[0]);

    if (argc == 4)
    {
        if (argv[1] != NULL)
        {
            if (atoi(argv[1]) > 30 || atoi(argv[1]) < 1)
            {
                vty_out_line(vty, "  Error: Invalid ttl!");
                return CMD_SUCCESS;
            }
            snprintf(tmp, sizeof(tmp), " -q %d", atoi(argv[1]));
            strcat(args, tmp);
        }

        if (argv[2] != NULL)
        {
            if (atoi(argv[2]) > 40 || atoi(argv[2]) < 1)
            {
                vty_out_line(vty, "  Error: Invalid hops!");
                return CMD_SUCCESS;
            }
            snprintf(tmp, sizeof(tmp), " -m %d", atoi(argv[2]));
            strcat(args, tmp);
        }

        if (argv[3] != NULL)
        {
            if (atoi(argv[3]) > 5 || atoi(argv[3]) < 1)
            {
                vty_out_line(vty, "  Error: Invalid timeout!");
                return CMD_SUCCESS;
            }
            snprintf(tmp, sizeof(tmp), " -w %d", atoi(argv[3]));
            strcat(args, tmp);
        }
    }

    execute_command_out (vty, "traceroute", args);
    return CMD_SUCCESS;
}

ALIAS (vtysh_traceroute,
       vtysh_traceroute_cmd,
       "traceroute DESTINATION",
       "Trace route to destination\n"
       "Trace route to destination address or hostname\n")

#ifdef HAVE_IPV6
DEFUN (vtysh_ping6,
       vtysh_ping6_cmd,
       "ping ipv6 WORD",
       "Send echo messages\n"
       "IPv6 echo\n"
       "Ping destination address or hostname\n")
{
  execute_command (vty->fd, "ping6", 1, argv[0], NULL);
  return CMD_SUCCESS;
}

DEFUN (vtysh_traceroute6,
       vtysh_traceroute6_cmd,
       "traceroute ipv6 WORD",
       "Trace route to destination\n"
       "IPv6 trace\n"
       "Trace route to destination address or hostname\n")
{
  execute_command (vty->fd, "traceroute6", 1, argv[0], NULL);
  return CMD_SUCCESS;
}
#endif

static char *get_name(char *name, char *p)
{
	/* Extract <name> from nul-terminated p where p matches
	   <name>: after leading whitespace.
	   If match is not made, set name empty and return unchanged p */
	int namestart = 0, nameend = 0;

	while (isspace(p[namestart]))
		namestart++;
	nameend = namestart;
	while (p[nameend] && p[nameend] != ':' && !isspace(p[nameend]))
		nameend++;
	if (p[nameend] == ':') {
		if ((nameend - namestart) < IFNAMSIZ) {
			memcpy(name, &p[namestart], nameend - namestart);
			name[nameend - namestart] = '\0';
			p = &p[nameend];
		} else {
			/* Interface name too large */
			name[0] = '\0';
		}
	} else {
		/* trailing ':' not found - return empty */
		name[0] = '\0';
	}
	return p + 1;
}

int islocalhost(const char *name)
{
    int ret = 0;
    FILE *fh = NULL;
    char  buf[512] = {0};
    UINT32 ipaddr = 0;
    int skfd;
    struct sockaddr_in in_addr;

    if (name == NULL)
    {
        return ret;
    }

    if (isalpha(name[0]))
    {
        if (strcmp(name, "localhost") == 0)
        {
            ret = 1;
        }
    }
    else if (isdigit(name[0]))
    {
        ipaddr = inet_addr(name);
        fh = fopen("/proc/net/dev", "r");
        if (!fh)
        {
            ret = 0;
            return ret;
        }
        fgets(buf, sizeof buf, fh); /* eat line */
        fgets(buf, sizeof buf, fh);

        skfd = socket(AF_INET, SOCK_DGRAM, 0);
        if (!(skfd < 0))
        {
            while (fgets(buf, sizeof(buf), fh))
            {
        		char *s, name[128];
        		struct ifreq ifr;

        		s = get_name(name, buf);

        		strncpy(ifr.ifr_name, name, sizeof(ifr.ifr_name));
        		ifr.ifr_addr.sa_family = AF_INET;
                if (ioctl(skfd, SIOCGIFADDR, &ifr) == 0)
                {
                    memset(&in_addr, 0, sizeof(in_addr));
                    /* it is localhost */
                    memcpy(&in_addr, &ifr.ifr_addr, sizeof(ifr.ifr_addr));
                    if (in_addr.sin_addr.s_addr == ipaddr)
                    {
                        ret = 1;
                        break;
                    }
                }
        	}
        	close(skfd);
    	}
    	fclose(fh);

    }

    return ret;
}

DEFUN (vtysh_telnet,
       vtysh_telnet_cmd,
       "telnet WORD [PORT]",
       "Open a telnet connection\n"
       "IP address or hostname of a remote system\n"
       "TCP Port number\n")
{
    char port[16] = {0};

    if (argc == 2 && argv[1] != NULL)
    {
        snprintf(port, sizeof(port), "%s", argv[1]);
    }
    else
    {
        snprintf(port, sizeof(port), "%d", VTYSH_PORT);
    }

    if (argv[0] != NULL)
    {
        if (!islocalhost(argv[0]) || atoi(port) != VTYSH_PORT)
        {
            execute_command (vty->fd, "telnet", 2, argv[0], port);
        }
        else
        {
            vty_out(vty, "  Error: It is localhost!%s", VTY_NEWLINE);
        }
    }
    return CMD_SUCCESS;
}

DEFUN (vtysh_ssh,
       vtysh_ssh_cmd,
       "ssh WORD",
       "Open an ssh connection\n"
       "[user@]host\n")
{
  execute_command (vty->fd, "ssh", 1, argv[0], NULL);
  return CMD_SUCCESS;
}

DEFUN (vtysh_start_shell,
       vtysh_start_shell_cmd,
       "start-shell",
       "Start UNIX shell\n")
{
    if(vty->type == VTY_TERM_LOCAL || vty->type == VTY_SSH)
        execute_command (vty->fd, "telnet", 2, "127.0.0.1", "2323");
    else
        vty_out_line(vty, "  Error: This command can just be accessed through console or SSH!");

    return CMD_SUCCESS;
}


DEFUN (vtysh_start_bash,
       vtysh_start_bash_cmd,
       "start-shell bash",
       "Start UNIX shell\n"
       "Start bash\n")
{
  execute_command (vty->fd, "bash", 0, NULL, NULL);
  return CMD_SUCCESS;
}

DEFUN (vtysh_start_zsh,
       vtysh_start_zsh_cmd,
       "start-shell zsh",
       "Start UNIX shell\n"
       "Start Z shell\n")
{
  execute_command (vty->fd, "zsh", 0, NULL, NULL);
  return CMD_SUCCESS;
}

DEFUN (show_vtysh_client_connect_daemon,
       show_vtysh_client_connect_daemon_cmd,
       "show daemons",
       SHOW_STR
       "All other daemons\n")
{
    vtysh_client_info *s = NULL;
    int i;

    s = (vtysh_client_info *)vty->vtysh_info;
    if(!s){
        vty_out(vty, " Connect null daemons.%s", VTY_NEWLINE);
        return CMD_SUCCESS;
    }
    vty_print_string_line(vty, "", __PROMPT_SEPARATOR_LENGTH__, __PROMPT_SEPARATOR_CHAR__);
    vty_out(vty, "  %-7s %-16s %-10s %s %s", "fd", "name", "flag", "path", VTY_NEWLINE);
    vty_print_string_line(vty, "", __PROMPT_SEPARATOR_LENGTH__, __PROMPT_SEPARATOR_CHAR__);
    for(i = 0; i < vty->daemon_num; i++){
        vty_out(vty, "%-7d %-16s 0x%x %s %s"
            , s[i].fd
            , s[i].name
            , s[i].flag
            , s[i].path, VTY_NEWLINE);

    }
    return CMD_SUCCESS;
}

#ifdef VTYSH_TEST_CMD
DEFSH (VTYSH_TEST,
       test_func_port_cmd,
       "show WORD",
       SHOW_STR
       "You can input word\n");

DEFUN (show_test_print_to_all_vty,
       show_test_print_to_all_vty_cmd,
       "show test",
       SHOW_STR
       "Print ALL info to all vty\n")
{
    char *buf = "Hello, Alarm!";

    vty_out_to_all(buf);

    return CMD_SUCCESS;
}

DEFUN (debug_test_print_msg,
       debug_test_print_msg_cmd,
       "debug show .MESSAGE",
       DEBUG_STR
       SHOW_STR
       "input\n")
{
    //char *buf = "Hello, Alarm!";

    //vty_out_to_all(buf);
    vty_out(vty, "argc:%d, argv:%s%s", argc, argv[0], VTY_NEWLINE);
    return CMD_SUCCESS;
}

#if 0
DEFUN (debug_test_multiple_parameter,
       debug_test_multiple_parameter_cmd,
       "debug multiple test WORD",
       DEBUG_STR
       "multiple\n"
       "test\n")
{
    //char *buf = "Hello, Alarm!";

    //vty_out_to_all(buf);
    vty_out(vty, "argc:%d, argv:%s%s", argc, argv[0], VTY_NEWLINE);
    return CMD_SUCCESS;
}
#endif

#endif

DEFUN (vtysh_logout,
       vtysh_logout_cmd,
       "logout",
       "Log off this system\n")
{
    vty->status = VTY_CLOSE;

    return CMD_SUCCESS;
}


/* add by ben.zheng  2015.11.12*/
DEFUNSH (VTYSH_IGMPSN,
    vty_enter_multicast_vlan_node,
    vty_enter_multicast_vlan_node_cmd,
    "multicast-vlan "VLAN_CMD_STR,
    "Multicast vlan common\n"
    "Multicast vlan. <U>"VLAN_CMD_STR"\n")
{
	int mvlanId = 0;
	PARSE_ULONG_STRING (argv[0], mvlanId, mvlanId);

    vty->node = INTERFACE_MVLAN_NODE;
	vty->user.env.mvlan_id = mvlanId;

    return CMD_SUCCESS;
}

DEFUNSH (VTYSH_IGMPSN,
    vty_exit_multicast_vlan_node,
    vty_exit_multicast_vlan_node_cmd,
     "exit",
     "Exit current mode and down to previous mode\n")
{
  return vtysh_exit (vty);
}


/* end add by ben.zheng */


/* add by ben.zheng  2016.3.1*/
DEFUNSH (VTYSH_IGMPSN,
    vty_enter_btv_node,
    vty_enter_btv_node_cmd,
    "btv",
    "Btv common\n")
{
    vty->node = INTERFACE_BTV_NODE;

    return CMD_SUCCESS;
}

DEFUNSH (VTYSH_IGMPSN,
    vty_exit_btv_node,
    vty_exit_btv_node_cmd,
     "exit",
     "Exit current mode and down to previous mode\n")
{
  return vtysh_exit (vty);
}


/* end add by ben.zheng */

#define SW_ACL_
DEFUNSH (VTYSH_QOSACL,
         interface_acl,
         interface_acl_cmd,
         "acl "CMD_ACL_ID"",
         "ACL configuration\n"
         DESC_ACL_INDEX)
{
    int aclId = 0;

    VTY_GET_ULONG (aclId, aclId, argv[0]);

    //vty_out(vty, "aclId: %d%s", aclId, VTY_NEWLINE);

    if (BasicAcl(aclId)) {
        vty->node = ACL_BASIC_NODE;
    }else if (AdvancedAcl(aclId)) {
        vty->node = ACL_ADV_NODE;
    }else if (LinkAcl(aclId)) {
        vty->node = ACL_LINK_NODE;
    }else if (UserAcl(aclId)) {
        vty->node = ACL_USER_NODE;
    }else {
        //vty_out (vty, " ACL ID not defined!%s", VTY_NEWLINE);
        return CMD_SUCCESS;
    }
    vty->user.env.aclId = aclId;

    return CMD_SUCCESS;
}

DEFUNSH (VTYSH_GTF,
         interface_eacl,
         interface_eacl_cmd,
         "acl "CMD_EACL_ID"",
         "ACL configuration\n"
         DESC_EACL_INDEX)
{
    int aclId = 0;

    VTY_GET_ULONG (aclId, aclId, argv[0]);

    vty->node = ACL_NODE;
    vty->user.env.aclId = aclId;

    return CMD_SUCCESS;
}


DEFUNSH (VTYSH_QOSACL,
	 vtysh_exit_acl,
	 vtysh_exit_acl_cmd,
	 "exit",
	 "Exit current mode and down to previous mode\n")
{
  return vtysh_exit (vty);
}


DEFUNSH (VTYSH_GTF,
	 vtysh_eexit_acl,
	 vtysh_eexit_acl_cmd,
	 "exit",
	 "Exit current mode and down to previous mode\n")
{
  return vtysh_exit (vty);
}


DEFUNSH (VTYSH_QOSACL,
    interface_acl6, 
    interface_acl6_cmd,
    "acl ipv6 <1000-3999>",
    DESC_ACL_STR
    IPV6_STR
    "ACL ID: <2000-2999>basic acl/<3000-4999>advanced acl\n")
{
    int aclId = 0;
    PARSE_ULONG_STRING (argv[0], aclId, aclId);
    //vty_out(vty, "aclId: %d%s", aclId, VTY_NEWLINE);
    if (BasicAcl(aclId)) {
        vty->node = ACL6_BASIC_NODE;
    }else if (AdvancedAcl(aclId)) {
        vty->node = ACL6_ADV_NODE;
    }else {
        vty_out (vty, " ACL ID not defined!!!%s", VTY_NEWLINE);
        return CMD_SUCCESS;
    }
    
    vty->user.env.aclId = aclId;
    
    return CMD_SUCCESS;
}


DEFUNSH (VTYSH_GTF,
    vty_enter_dba_profile_node,
    vty_enter_dba_profile_node_cmd,
    "dba-profile {profile-id <0-128>|profile-name PROFILE-NAME}",
    "<Group> DBA profile configuration command group\n"
    "By profile ID\n"
    DESC_GDBA_PROFILE_ID
    "By profile name\n"
    DESC_GDBA_PROFILE_NAME)
{
    vtysh_user_env_get_from_serv(vty, self);

    vty->node = DBA_PROFILE_NODE;

    vtysh_gprofile_node_enter_check(vty);
    
    return CMD_SUCCESS;
}

ALIAS_SH(VTYSH_GTF,
    vty_enter_dba_profile_node,
    vty_enter_dba_profile_node_none_arg_cmd,
    "dba-profile",
    "<Group> DBA profile configuration command group\n")

DEFUNSH (VTYSH_GTF,
	 vtysh_exit_dba_profile_node,
	 vtysh_exit_dba_profile_node_cmd,
	 "exit",
	 "Exit current mode and down to previous mode\n")
{
    if (atoi(argv[argc - 1]) == CMD_SUCCESS_INTERACTION)
    {
        vty->node = DBA_PROFILE_INTERACTION_NODE;
        return CMD_SUCCESS;
    }
    else
        return vtysh_exit (vty);
}

DEFUNSH (VTYSH_GTF,
     vtysh_dba_profile_interaction_yes,
     vtysh_dba_profile_interaction_yes_cmd,
     "y",
     "\n")
{
    vty->node = CONFIG_NODE;
    return CMD_SUCCESS;
}

DEFUNSH (VTYSH_GTF,
     vtysh_dba_profile_interaction_no,
     vtysh_dba_profile_interaction_no_cmd,
     "n",
     "\n")
{
    vty->node = DBA_PROFILE_NODE;
    return CMD_SUCCESS;
}

DEFUNSH (VTYSH_GTF,
    vty_enter_line_profile_node,
    vty_enter_line_profile_node_cmd,
    "ont-lineprofile g{profile-id <1-512>|profile-name PROFILE-NAME}",
    "<Group> line profile configuration command group\n"
    "By profile ID\n"
    DESC_GLINE_PROFILE_ID
    "By profile name\n"
    DESC_GLINE_PROFILE_NAME)
{
    vtysh_user_env_get_from_serv(vty, self);

    vty->node = LINE_PROFILE_NODE;

    vtysh_gprofile_node_enter_check(vty);
      
    return CMD_SUCCESS;
}

ALIAS_SH(VTYSH_GTF,
    vty_enter_line_profile_node,
    vty_enter_line_profile_node_none_arg_cmd,
    "ont-lineprofile gtf",
    "<Group> line profile configuration command group\n")

DEFUNSH (VTYSH_GTF,
	 vtysh_exit_line_profile_node,
	 vtysh_exit_line_profile_node_cmd,
	 "exit",
	 "Exit current mode and down to previous mode\n")
{
    if (atoi(argv[argc - 1]) == CMD_SUCCESS_INTERACTION)
    {
        vty->node = LINE_PROFILE_INTERACTION_NODE;
        return CMD_SUCCESS;
    }
    else
        return vtysh_exit (vty);
}

DEFUNSH (VTYSH_GTF,
     vtysh_line_profile_interaction_yes,
     vtysh_line_profile_interaction_yes_cmd,
     "y",
     "\n")
{
    vty->node = CONFIG_NODE;
    return CMD_SUCCESS;
}

DEFUNSH (VTYSH_GTF,
     vtysh_line_profile_interaction_no,
     vtysh_line_profile_interaction_no_cmd,
     "n",
     "\n")
{
    vty->node = LINE_PROFILE_NODE;
    return CMD_SUCCESS;
}
    
DEFUNSH (VTYSH_GTF,
    vty_enter_srv_profile_node,
    vty_enter_srv_profile_node_cmd,
    "ont-srvprofile g{profile-id <1-512>|profile-name PROFILE-NAME}",
    "<Group> service profile configuration command group\n"
    "By profile ID\n"
    DESC_GSRV_PROFILE_ID
    "By profile name\n"
    DESC_GSRV_PROFILE_NAME)
{
    vtysh_user_env_get_from_serv(vty, self);

    vty->node = SRV_PROFILE_NODE;

    vtysh_gprofile_node_enter_check(vty);
    
    return CMD_SUCCESS;
}

ALIAS_SH(VTYSH_GTF,
    vty_enter_srv_profile_node,
    vty_enter_srv_profile_node_none_arg_cmd,
    "ont-srvprofile gtf",
    "<Group> service profile configuration command group\n")

DEFUNSH (VTYSH_GTF,
	 vtysh_exit_srv_profile_node,
	 vtysh_exit_srv_profile_node_cmd,
	 "exit",
	 "Exit current mode and down to previous mode\n")
{
    if (atoi(argv[argc - 1]) == CMD_SUCCESS_INTERACTION)
    {
        vty->node = SRV_PROFILE_INTERACTION_NODE;
        return CMD_SUCCESS;
    }
    else
        return vtysh_exit (vty);
}

DEFUNSH (VTYSH_GTF,
     vtysh_srv_profile_interaction_yes,
     vtysh_srv_profile_interaction_yes_cmd,
     "y",
     "\n")
{
    vty->node = CONFIG_NODE;
    return CMD_SUCCESS;
}

DEFUNSH (VTYSH_GTF,
     vtysh_srv_profile_interaction_no,
     vtysh_srv_profile_interaction_no_cmd,
     "n",
     "\n")
{
    vty->node = SRV_PROFILE_NODE;
    return CMD_SUCCESS;
}

DEFUNSH (VTYSH_GTF,
    vty_enter_sla_profile_node,
    vty_enter_sla_profile_node_cmd,
    "ont-slaprofile {profile-id <1-256>|profile-name PROFILE-NAME}",
    "<Group> sla profile configuration command group\n"
    "By profile ID\n"
    DESC_GSLA_PROFILE_ID
    "By profile name\n"
    DESC_GSLA_PROFILE_NAME)
{
    vtysh_user_env_get_from_serv(vty, self);

    vty->node = SLA_PROFILE_NODE;

    vtysh_gprofile_node_enter_check(vty);
    
    return CMD_SUCCESS;
}

ALIAS_SH(VTYSH_GTF,
    vty_enter_sla_profile_node,
    vty_enter_sla_profile_node_none_arg_cmd,
    "ont-slaprofile",
    "<Group> sla profile configuration command group\n")

DEFUNSH (VTYSH_GTF,
	 vtysh_exit_sla_profile_node,
	 vtysh_exit_sla_profile_node_cmd,
	 "exit",
	 "Exit current mode and down to previous mode\n")
{
    if (atoi(argv[argc - 1]) == CMD_SUCCESS_INTERACTION)
    {
        vty->node = SLA_PROFILE_INTERACTION_NODE;
        return CMD_SUCCESS;
    }
    else
        return vtysh_exit (vty);
}

DEFUNSH (VTYSH_GTF,
     vtysh_sla_profile_interaction_yes,
     vtysh_sla_profile_interaction_yes_cmd,
     "y",
     "\n")
{
    vty->node = CONFIG_NODE;
    return CMD_SUCCESS;
}

DEFUNSH (VTYSH_GTF,
     vtysh_sla_profile_interaction_no,
     vtysh_sla_profile_interaction_no_cmd,
     "n",
     "\n")
{
    vty->node = SLA_PROFILE_NODE;
    return CMD_SUCCESS;
}

DEFUNSH (VTYSH_GTF,
    vty_enter_classification_profile_node,
    vty_enter_classification_profile_node_cmd,
    "classification {profile-id <1-512>|profile-name PROFILE-NAME}",
    "<Group> classification profile configuration command group\n"
    "By profile ID\n"
    DESC_GCLASSIFICATION_PROFILE_ID
    "By profile name\n"
    DESC_GCLASSIFICATION_PROFILE_NAME)
{
    vtysh_user_env_get_from_serv(vty, self);

    vty->node = CLASSIFICATION_PROFILE_NODE;

    vtysh_gprofile_node_enter_check(vty);
    
    return CMD_SUCCESS;
}

ALIAS_SH(VTYSH_GTF,
    vty_enter_classification_profile_node,
    vty_enter_classification_profile_node_none_arg_cmd,
    "classification",
    "<Group> classification profile configuration command group\n")

DEFUNSH (VTYSH_GTF,
	 vtysh_exit_classification_profile_node,
	 vtysh_exit_classification_profile_node_cmd,
	 "exit",
	 "Exit current mode and down to previous mode\n")
{
    if (atoi(argv[argc - 1]) == CMD_SUCCESS_INTERACTION)
    {
        vty->node = CLASSIFICATION_PROFILE_INTERACTION_NODE;
        return CMD_SUCCESS;
    }
    else
        return vtysh_exit (vty);
}

DEFUNSH (VTYSH_GTF,
     vtysh_classification_profile_interaction_yes,
     vtysh_classification_profile_interaction_yes_cmd,
     "y",
     "\n")
{
    vty->node = CONFIG_NODE;
    return CMD_SUCCESS;
}

DEFUNSH (VTYSH_GTF,
     vtysh_classification_profile_interaction_no,
     vtysh_classification_profile_interaction_no_cmd,
     "n",
     "\n")
{
    vty->node = CLASSIFICATION_PROFILE_NODE;
    return CMD_SUCCESS;
}

DEFUNSH (VTYSH_GTF,
     vty_interface_gont_delete_all_yes,
     vty_interface_gont_delete_all_yes_cmd,
     "y",
     "\n")
{
    vty->node = INTERFACE_GNODE;
        
    return CMD_SUCCESS;
}

DEFUNSH (VTYSH_GTF,
     vty_interface_gont_delete_all_no,
     vty_interface_gont_delete_all_no_cmd,
     "n",
     "\n")
{
    vty->node = INTERFACE_GNODE;
    
    return CMD_SUCCESS;
}

DEFUNSH (VTYSH_GTF,
    vty_interface_gont_delete_all,
    vty_interface_gont_delete_all_cmd,
    "ont delete "CMD_STR" all",
    "<Group> ont command group\n"
    "Delete ONT(s)\n"
    DESC_PORT_ID
    DESC_GONT_ALL)
{
    vty->node = GONT_DELETE_ALL_INTERACTION_NODE;
    VTY_ID2ENV(vty->user.env.tfId, atoi(argv[0]) - 1);
        
    return CMD_SUCCESS_INTERACTION;
}


/*add by jq.deng, 20151110*/
DEFUNSH (VTYSH_LAYER2|VTYSH_RSTP|VTYSH_LACP|VTYSH_DOT1X,
    vty_enter_interface_ge_node,
    vty_enter_interface_ge_node_cmd,
    "interface ge",
    "<Group> interface command group\n"
    "Change into GE command mode\n")
{
  vty->node = INTERFACE_GE_NODE;
  return CMD_SUCCESS;
}

DEFUNSH (VTYSH_LAYER2|VTYSH_RSTP|VTYSH_LACP|VTYSH_DOT1X,
	 vtysh_exit_interface_ge_node,
	 vtysh_exit_interface_ge_node_cmd,
	 "exit",
	 "Exit current mode and down to previous mode\n")
{
  return vtysh_exit (vty);
}

DEFUNSH (VTYSH_LAYER2|VTYSH_RSTP|VTYSH_LACP|VTYSH_DOT1X,
    vty_enter_interface_xge_node,
    vty_enter_interface_xge_node_cmd,
    "interface xge",
    "<Group> interface command group\n"
    "Change into XGE command mode\n")
{

    vty->node = INTERFACE_XGE_NODE;
    return CMD_SUCCESS;

}

DEFUNSH (VTYSH_LAYER2|VTYSH_RSTP|VTYSH_LACP|VTYSH_DOT1X,
	 vtysh_exit_interface_xge_node,
	 vtysh_exit_interface_xge_node_cmd,
	 "exit",
	 "Exit current mode and down to previous mode\n")
{
  return vtysh_exit (vty);
}
DEFUNSH (VTYSH_LAYER2|VTYSH_HAL|VTYSH_RSTP|VTYSH_LACP|VTYSH_DOT1X,
    vty_enter_interface_sa_node,
    vty_enter_interface_sa_node_cmd,
    "interface link-aggregation",
    "<Group> interface command group\n"
    "Change into link-aggregation command mode\n")
{
    vty->node = INTERFACE_SA_NODE;
  
    return CMD_SUCCESS;
}

DEFUNSH (VTYSH_LAYER2|VTYSH_HAL|VTYSH_RSTP|VTYSH_LACP|VTYSH_DOT1X,
    vty_exit_interface_sa_node,
    vty_exit_interface_sa_node_cmd,
     "exit",
     "Exit current mode and down to previous mode\n")
{
    return vtysh_exit (vty);
}

DEFUNSH (VTYSH_GTF|VTYSH_LAYER2,
    vty_enter_interface_gnode,
    vty_enter_interface_gnode_cmd,
    "interface g<0-0>",
    "<Group> interface command group\n"
    "Change into GTF command mode\n"
     "Slot ID. <U><0-0>\n")
{
    VTY_SLOT_ID2ENV(vty->user.env.slot_id, atoi(argv[0]));
    vty->node = INTERFACE_GNODE;
    return CMD_SUCCESS;
}

DEFUNSH (VTYSH_LAYER2,
	 vtysh_exit_interface_gnode,
	 vtysh_exit_interface_gnode_cmd,
	 "exit",
	 "Exit current mode and down to previous mode\n")
{
  return vtysh_exit (vty);
}
DEFUNSH (VTYSH_ALL,
    debug_mode_vty,
    debug_mode_vty_cmd,
   "debug",
   "Change into debug mode\n")
{
    vty->node = DEBUG_NODE;

    return CMD_SUCCESS;
}

DEFUNSH (VTYSH_ALL,
    debug_exit_mode_vty,
    debug_exit_mode_vty_cmd,
    "exit",
    "Exit current mode and down to previous mode\n")
{
    return vtysh_exit (vty);
}

#undef SW_ACL_

static void
vtysh_install_default (enum node_type node)
{
  /*install_element (node, &config_list_cmd);*/
  /*install_element (node, &vtysh_exit_all_cmd);*/
  /*install_element (node, &vtysh_quit_all_cmd);*/
  install_element (node, &vtysh_config_end_all_cmd);
}

int vtysh_cmd(struct vty * vty, char *cmd)
{
    struct cmd_node *cnode;
    unsigned int index = 0;
    void *comm = NULL;
    int flag = 0;

    printf("%s %d, node=%d, \r\n", __func__, __LINE__, vty->node);
    if (!cmdvec)
        return CMD_ERR_NOTHING_TODO;
    printf("%s %d, node=%d, \n", __func__, __LINE__, vty->node);
    cnode = vector_slot (cmdvec, vty->node);
    if (cnode == NULL) {
        //fprintf(stderr, "Command node %d doesn't exist, please check it\n", ntype);
        //exit (1);
        printf("%s %d, node=%d, cnode null\r\n", __func__, __LINE__, vty->node);
        return CMD_ERR_NOTHING_TODO;
    }

    if(!strcmp(cmd, "end")){
        printf("%s %d, act=%d, \r\n", __func__, __LINE__, vector_active (cnode->cmd_vector));
        for (index = 0; index <= vector_active (cnode->cmd_vector); index++) {
            comm = vector_lookup (cnode->cmd_vector, index);
            if(!comm)
                printf("%s %d, index=%d, comm null\r\n", __func__, __LINE__, index);
            if(comm&&(comm == (void *)&vtysh_config_end_all_cmd)){
                flag = 1;
                break;
            }
        }
        if(flag){
            printf("%s %d, index=%d, \r\n", __func__, __LINE__, index);
            vty_out (vty, "%s", VTY_NEWLINE);
            vtysh_cmd_execute(vty,cmd);
            //vty_prompt (vty);
            vty->cp = 0;
        }
    }
}



#if VTYSH_ENABLE_CONSOLE
/* To disable readline's filename completion. */
static char *
vtysh_completion_entry_function (const char *ignore, int invoking_key)
{
  return NULL;
}


#endif
DEFUNSH(VTYSH_ALL,
        test_func_port_alarm,
       test_func_port_alarm_cmd,
       "debug alarm",
       DEBUG_STR
       "Alarm\n")
{
    printf("we have alarm.\n");
    return CMD_SUCCESS;
}
static bool g_vtysh_timeout_flag = FALSE;

void vtysh_set_timeout_flag(bool s)
{
    g_vtysh_timeout_flag = s;
}

DEFUNSH (VTYSH_SYSCTRL,
    vty_enter_interface_mgmt_node,
    vty_enter_interface_mgmt_node_cmd,
    "interface mgmt",
    "<Group> interface command group\n"
    "Change into mgmt command mode\n")
{
    vty->node = INTERFACE_MGMT_NODE;
    return CMD_SUCCESS;
}

DEFUNSH (VTYSH_SYSCTRL,
	 vtysh_exit_interface_mgmt_node,
	 vtysh_exit_interface_mgmt_node_cmd,
	 "exit",
	 "Exit current mode and down to previous mode\n")
{
  return vtysh_exit (vty);
}

DEFUNSH (VTYSH_SYSCTRL,
    vty_enter_interface_vlanif_node,
    vty_enter_interface_vlanif_node_cmd,
    "interface vlanif "VLAN_CMD_STR,
    "<Group> interface command group\n"
    "Change into vlanif command mode\n"
    "VLAN ID. <U>"VLAN_CMD_STR"\n")
{
    short vlanId = 0;

    PARSE_ULONG_STRING(argv[0], vlanId, vlanId);

    vty->node = INTERFACE_VLANIF_NODE;
    vty->user.env.vlanif_id = vlanId;

    return CMD_SUCCESS;
}

DEFUNSH (VTYSH_SYSCTRL,
	 vtysh_exit_interface_vlanif_node,
	 vtysh_exit_interface_vlanif_node_cmd,
	 "exit",
	 "Exit current mode and down to previous mode\n")
{
  return vtysh_exit (vty);
}



DEFUNSH (VTYSH_QOSACL,
        terminal_show_process_info,
       terminal_show_process_info_cmd,
       "show process",
       SHOW_STR
       "All process infomation\n")
{
    char task_name[128];

    memset(task_name, 0x00, sizeof(task_name));
    vty_out(vty, "%4d %6s%s", getpid(), (char *)getNameByPid(getpid(), task_name)
        , VTY_NEWLINE);
    vty_out(vty, "----------------------------------------%s", VTY_NEWLINE);
    vty_out(vty, " pid  name %s", VTY_NEWLINE);

    return CMD_SUCCESS;
}

DEFUN (vtysh_cat,
       vtysh_cat_cmd,
       "cat WORD",
       "Print FILE to stdout\n"
       "File name\n")
{
    FILE *fp = NULL;
    char buf[128] = {0};

    if (argv[0] == '\0')
        return CMD_SUCCESS;

    fp = fopen(argv[0], "r");

    if (fp == NULL)
        return CMD_SUCCESS;

    while(fgets(buf, sizeof(buf) - 1, fp) != NULL)
    {
        buf[sizeof(buf) - 1] = '\0';
        vty_out(vty, "%s", buf);
        memset(buf, 0, sizeof(buf));
    }

    fclose(fp);

    return CMD_SUCCESS;
}

DEFUN (vtysh_show_log,
       vtysh_show_log_cmd,
       "show log",
       SHOW_STR
       "User Operation Log\n")
{
#if 0
    FILE *fp = NULL;
    char buf[512] = {0};
    char user[VTY_USER_MAX_LEN + 1] = {0};
    char *lfcr = NULL;

    fp = fopen(OPER_LOG_FILE, "r");

    if (fp == NULL)
        return CMD_SUCCESS;

    snprintf(user, sizeof(user), "@%s", vty->sign_user.name);
    while(fgets(buf, sizeof(buf) - 1, fp) != NULL)
    {
        buf[sizeof(buf) - 1] = '\0';
        if ((lfcr = strchr(buf, '\n')) != NULL)
            *lfcr = '\0';

        /* 权限控制 */
        if (!cmd_element_access_allowed(CLI_ACCESS_ROOT, vty->sign_user.level))
        {
            if (strstr(buf, user) == NULL)
                continue;
        }

        vty_out(vty, "%s%s", buf, VTY_NEWLINE);
        memset(buf, 0, sizeof(buf));
    }

    fclose(fp);
#else
    vtysh_tac(vty, OPER_LOG_FILE, TRUE);
#endif

    return CMD_SUCCESS;
}

DEFUN (vtysh_show_alarm_history,
       vtysh_show_alarm_history_cmd,
       "show alarm history",
       SHOW_STR
       "Show alarm correlation information\n"
       "Show historical alarm records\n")
{
#if 0
    FILE *fp = NULL;
    char buf[512] = {0};
    char *lfcr = NULL;
    
    fp = fopen(ALARM_LOG_FILE, "r");

    if (fp == NULL)
        return CMD_SUCCESS;

    while(fgets(buf, sizeof(buf) - 1, fp) != NULL)
    {
        buf[sizeof(buf) - 1] = '\0';
        if ((lfcr = strchr(buf, '\n')) != NULL)
            *lfcr = '\0';
        vty_out(vty, "%s%s", buf, VTY_NEWLINE);
        memset(buf, 0, sizeof(buf));
    }

    fclose(fp);
#else
    vtysh_tac(vty, ALARM_LOG_FILE, FALSE);
#endif
    
    return CMD_SUCCESS;
}

DEFUN (
    vtysh_system_reboot,
    vtysh_system_reboot_cmd,
    "reboot",
    "Reboot system\n")
{
    if (vty->node == ENABLE_NODE)
        vty->node = ENABLE_REBOOT_INTERACTION_NODE;
    else if (vty->node == CONFIG_NODE)
        vty->node = CONFIG_REBOOT_INTERACTION_NODE;

    return CMD_SUCCESS;
}

#if 0
int
boardReset_alarm(void)
{
    ALARM_INFO_STRU pAlarmInfo;
    char deviceId=1, slotId=1,ret;
    memset(&pAlarmInfo, 0, sizeof(ALARM_INFO_STRU));

    pAlarmInfo.code = BOARD_RESET_ALARM;
    pAlarmInfo.severity = BOARD_RESET_SEVERITYTYPE;
    pAlarmInfo.instance[0] = deviceId;
    pAlarmInfo.instance[1] = slotId;
    ret = ipc_if_exe_cmd(MODULE_SYSCTRL, IPC_SYS_ALARM_TRIGGER, (char *)&pAlarmInfo, sizeof(ALARM_INFO_STRU),NULL);
    if(ret!=RTN_OK){
        printf("[%s: %d] handle alarm failed\r\n", __func__, __LINE__);
        return RTN_ERROR;
    }

    return RTN_OK;
}

int
deviceReset_event(void)
{
    ALARM_INFO_STRU pAlarmInfo;
    char deviceId=1, slotId=1,ret;
    memset(&pAlarmInfo, 0, sizeof(ALARM_INFO_STRU));

    pAlarmInfo.code = DEVICE_RESET_EVENT;
    pAlarmInfo.instance[0] = deviceId;
    ret = ipc_if_exe_cmd(MODULE_SYSCTRL, IPC_SYS_EVENT_TRIGGER, (char *)&pAlarmInfo, sizeof(ALARM_INFO_STRU),NULL);
    if(ret!=RTN_OK){
        printf("[%s: %d] handle event failed\r\n", __func__, __LINE__);
    }

    return RTN_OK;
}
#endif

DEFUN (
    vtysh_system_reboot_yes,
    vtysh_system_reboot_yes_cmd,
    "y",
    "\n")
{
    char msg[128] = {0};
    tfSysRebootCtrl_t rebootCtrl;
    short rc = 0, retCode = 0;

    memset(&rebootCtrl, 0, sizeof(rebootCtrl));
    rebootCtrl.type = SYS_REBOOT_NORMAL;
    rc = ipc_if_exe_cmd(MODULE_SYSCTRL, IPC_SYS_REBOOT_CTRL, &rebootCtrl, sizeof(rebootCtrl), &retCode);

    if((0 == rc) && (0 == retCode))
    {
        snprintf(msg, sizeof(msg), "%sSystem is about to reboot, please wait!%s", VTY_NEWLINE, VTY_NEWLINE);
        vty_out_to_all(msg);

        //boardReset_alarm();
        //deviceReset_event();
    }
    else
    {
        vty_out_line(vty, "System is busy, Please try again later!");
    }

    if (vty->node == ENABLE_REBOOT_INTERACTION_NODE)
        vty->node = ENABLE_NODE;
    else if (vty->node == CONFIG_REBOOT_INTERACTION_NODE)
        vty->node = CONFIG_NODE;
    return CMD_SUCCESS;
}

DEFUN (
    vtysh_system_reboot_no,
    vtysh_system_reboot_no_cmd,
    "n",
    "\n")
{
    if (vty->node == ENABLE_REBOOT_INTERACTION_NODE)
        vty->node = ENABLE_NODE;
    else if (vty->node == CONFIG_REBOOT_INTERACTION_NODE)
        vty->node = CONFIG_NODE;

    return CMD_SUCCESS;
}

DEFUNSH (VTYSH_ALL,
       vtysh_alarm_priority_severity,
       vtysh_alarm_priority_severity_cmd,
       "alarm priority (critical|error|warning|notice)",
       "<Group> Alarm information\n"
       "Alarm priority setup\n"
       "Critical level\n"
       "Error level\n"
       "Warning level\n"
       "Notice level\n")
{
    const char *severityStr[] = {"critical", "error", "warning", "notice"};
    int   severity[array_size(severityStr)] = {LOG_CRIT, LOG_ERR, LOG_WARNING, LOG_NOTICE};
    int   idx = 0;

    if (argv[0] == NULL || argv[0][0] == '\0')
    {
        vty_out_line(vty, "  Error: Invalid parameters!");
        return CMD_SUCCESS;
    }

    for (idx = 0; idx < array_size(severityStr); idx++)
    {
        if (0 == strcmp(severityStr[idx], argv[0]))
        {
            break;
        }
    }

    if (idx < array_size(severityStr))
    {
        if (tflog_alarm_priority_set(severity[idx]) < 0)
            vty_out_line(vty, "  Error: Internal error, please try again later!");
    }
    else
    {
        vty_out_line(vty, "  Error: Unknown priority severity!");
    }
    return CMD_SUCCESS;
}

DEFUNSH (VTYSH_ALL,
       vtysh_syslog_priority_severity,
       vtysh_syslog_priority_severity_cmd,
       "syslog priority severity (critical|error|warning|notice|debug)",
       "<Group> Syslog information\n"
       "Syslog priority setup\n"
       "Syslog priority severity setup\n"
       "Critical level\n"
       "Error level\n"
       "Warning level\n"
       "Notice level\n"
       "Debug level\n")
{
    const char *severityStr[] = {"critical", "error", "warning", "notice", "debug"};
    int   severity[array_size(severityStr)] = {LOG_CRIT, LOG_ERR, LOG_WARNING, LOG_NOTICE, LOG_DEBUG};
    int   idx = 0;

    if (argv[0] == NULL || argv[0][0] == '\0')
    {
        vty_out_line(vty, "  Error: Invalid parameters!");
        return CMD_SUCCESS;
    }

    for (idx = 0; idx < array_size(severityStr); idx++)
    {
        if (0 == strcmp(severityStr[idx], argv[0]))
        {
            break;
        }
    }

    if (idx < array_size(severityStr))
    {
        if (tflog_syslog_priority_set(severity[idx]) < 0)
            vty_out_line(vty, "  Error: Internal error, please try again later!");
    }
    else
    {
        vty_out_line(vty, "  Error: Unknown priority severity!");
    }
    return CMD_SUCCESS;
}

DEFUN (
    vtysh_show_alarm_priority_severity,
    vtysh_show_alarm_priority_severity_cmd,
    "show alarm priority",
    DESC_SHOW
    "Show syslog information\n"
    "Show syslog priority information\n")
{
    int             prio;
    const char const * prioStr[] =
    {
        "emerg",
        "alert",
        "critical",
        "error",
        "warning",
        "notice",
        "info",
        "debug"
    };

    prio = tflog_alarm_priority_get();

    if (prio < 0)
    {
        vty_out_line(vty, "  Error: Internal error, please try again later!");
    }
    else
    {
        vty_print_string_line (vty, "", __PROMPT_SEPARATOR_LENGTH__, __PROMPT_SEPARATOR_CHAR__);
        vty_out_line(vty, " Alarm priority severity: %s", prioStr[prio % (sizeof(prioStr) / sizeof(prioStr[0]))]);
        vty_print_string_line (vty, "", __PROMPT_SEPARATOR_LENGTH__, __PROMPT_SEPARATOR_CHAR__);
    }
    return CMD_SUCCESS;
}

DEFUN (
    vtysh_show_syslog_priority_severity,
    vtysh_show_syslog_priority_severity_cmd,
    "show syslog priority severity",
    DESC_SHOW
    "Show syslog information\n"
    "Show syslog priority information\n"
    "Show syslog priority severity information\n")
{
    int             prio;
    const char const * prioStr[] =
    {
        "emerg",
        "alert",
        "critical",
        "error",
        "warning",
        "notice",
        "info",
        "debug"
    };

    prio = tflog_syslog_priority_get();

    if (prio < 0)
    {
        vty_out_line(vty, "  Error: Internal error, please try again later!");
    }
    else
    {
        vty_print_string_line (vty, "", __PROMPT_SEPARATOR_LENGTH__, __PROMPT_SEPARATOR_CHAR__);
        vty_out_line(vty, " Syslog priority severity: %s", prioStr[prio % (sizeof(prioStr) / sizeof(prioStr[0]))]);
        vty_print_string_line (vty, "", __PROMPT_SEPARATOR_LENGTH__, __PROMPT_SEPARATOR_CHAR__);
    }
    return CMD_SUCCESS;
}

DEFUNSH (VTYSH_SYSCTRL,
    vtysh_system_cfg_erase,
    vtysh_system_cfg_erase_cmd,
    "erase saved-config",
    "Erase certain data from internal storage\n"
    "Erase saved configurations\n")
{
    if (vty->node == CONFIG_NODE)
        vty->node = CONFIG_ERASE_CFG_INTERACTION_NODE;
    return CMD_SUCCESS;
}

DEFUNSH (VTYSH_SYSCTRL,
     vtysh_system_cfg_erase_yes,
     vtysh_system_cfg_erase_yes_cmd,
     "y",
     "\n")
{
    if (vty->node == CONFIG_ERASE_CFG_INTERACTION_NODE)
        vty->node = CONFIG_NODE;
    return CMD_SUCCESS;
}

DEFUNSH (VTYSH_SYSCTRL,
     vtysh_system_cfg_erase_no,
     vtysh_system_cfg_erase_no_cmd,
     "n",
     "\n")
{
    if (vty->node == CONFIG_ERASE_CFG_INTERACTION_NODE)
        vty->node = CONFIG_NODE;
    return CMD_SUCCESS;
}

DEFUNSH (VTYSH_SYSCTRL,
    vtysh_system_cfg_load,
    vtysh_system_cfg_load_cmd,
    "load configuration ftp A.B.C.D USER-NAME USER-PASSWORD FILE-NAME",
    "<Group> load command group\n"
    "Configuration file\n"
    "FTP mode\n"
    "FTP Server's IP address. <I><X.X.X.X>\n"
    "FTP user name. <S><Length 1-32>\n"
    "FTP user password. <S><Length 1-32>\n"
    "Load file name. <S><Length 1-64>\n")
{
    if (vty->node == CONFIG_NODE)
        vty->node = CONFIG_LOAD_CFG_INTERACTION_NODE;
    return CMD_SUCCESS;
}

DEFUNSH (VTYSH_SYSCTRL,
     vtysh_system_cfg_load_yes,
     vtysh_system_cfg_load_yes_cmd,
     "y",
     "\n")
{
    if (vty->node == CONFIG_LOAD_CFG_INTERACTION_NODE)
        vty->node = CONFIG_NODE;
    return CMD_SUCCESS;
}

DEFUNSH (VTYSH_SYSCTRL,
     vtysh_system_cfg_load_no,
     vtysh_system_cfg_load_no_cmd,
     "n",
     "\n")
{
    if (vty->node == CONFIG_LOAD_CFG_INTERACTION_NODE)
        vty->node = CONFIG_NODE;
    return CMD_SUCCESS;
}

DEFUNSH (VTYSH_SYSCTRL,
    vty_enter_manu_node,
    vty_enter_manu_node_cmd,
    "manufacture",
    "Change into manufacture mode\n")
{
    vty->node = MANU_NODE;
    return CMD_SUCCESS;
}

DEFUNSH (VTYSH_SYSCTRL,
	 vty_exit_manu_node,
	 vty_exit_manu_node_cmd,
	 "exit",
	 "Exit current mode and down to previous mode\n")
{
  return vtysh_exit (vty);
}

DEFUNSH (VTYSH_ALL,
       vtysh_show_process_info,
       vtysh_show_process_info_cmd,
       "show process info",
       SHOW_STR
       "Show process\n"
       "Process information\n")
{
    char buf[128] = {0};
    int  len = 0;
    /* process name, pid, pgid */
    len = readlink("/proc/self/exe", buf, sizeof(buf));
    if (len > 0)
    {
        len = (len == sizeof(buf)) ? len - 1 : len;
        buf[len] = '\0';
        vty_out_line(vty, "Name: %s", buf);
    }

    vty_out_line(vty, "Pid:%d", getpid());
    vty_out_line(vty, "PGid:%d", getpgrp());

    return CMD_SUCCESS;
}


#if VTYSH_ENABLE_CONSOLE

void
vtysh_timeout(int u)
{
    if(!g_vtysh_timeout_flag){
        printf("\r\nVty connection is timed out.\n");

        vty_clear_user_data(vtysh);
        vty_user_update_hist_state(vtysh->port, VTY_LOGOUT);
        clear_history();
        vtysh_execute("end");
        vtysh->node = USER_NODE;
        vtysh_set_timeout_flag(TRUE);
    }
}

void
vtysh_readline_init (void)
{
  /* readline related settings. */
  rl_bind_key ('?', (rl_command_func_t *) vtysh_rl_describe);
#ifdef _ENABLE_VTY_SPACE_EXPAND_
  rl_bind_key (' ', (rl_command_func_t *) vtysh_rl_complete);
#endif
  rl_completion_entry_function = vtysh_completion_entry_function;
  rl_attempted_completion_function = (rl_completion_func_t *)new_completion;
  rl_set_keyboard_input_timeout (1000);

}
#endif

/* SIGTSTP handler.  This function care user's ^Z input. */
void
sigtstp (int sig)
{
  #if 0
  /* Execute "end" command. */
  vtysh_execute ("end");

  /* Initialize readline. */
  rl_initialize ();
  prin("\n");

  /* Check jmpflag for duplicate siglongjmp(). */
  if (! jmpflag)
    return;

  jmpflag = 0;

  /* Back to main command loop. */
  siglongjmp (jmpbuf, 1);
  #endif
}

/* SIGINT handler.  This function care user's ^Z input.  */
void
sigint (int sig)
{
  #if 0
  /* Check this process is not child process. */
  if (! execute_flag)
    {
      rl_initialize ();
      prin("\n");
      rl_forced_update_display ();
    }
  #endif
}

/* Signale wrapper for vtysh. We don't use sigevent because
 * vtysh doesn't use threads. TODO */
RETSIGTYPE *
vtysh_signal_set (int signo, void (*func)(int))
{
  int ret;
  struct sigaction sig;
  struct sigaction osig;

  sig.sa_handler = func;
  sigemptyset (&sig.sa_mask);
  sig.sa_flags = 0;
#ifdef SA_RESTART
  sig.sa_flags |= SA_RESTART;
#endif /* SA_RESTART */

  ret = sigaction (signo, &sig, &osig);

  if (ret < 0)
    return (SIG_ERR);
  else
    return (osig.sa_handler);
}

/* Initialization of signal handles. */
void
vtysh_signal_init ()
{
	/* First establish some default handlers that can be overridden by
	   the application. */
	//trap_default_signals();

	vtysh_signal_set (SIGINT, SIG_IGN);
	vtysh_signal_set (SIGTSTP, sigtstp);
	vtysh_signal_set (SIGPIPE, SIG_IGN);
}

extern int g_vtysh_flag;
void vtysh_init()
{
    g_vtysh_flag = 1;  //stephen ,20150929

	vtysh_signal_init();
    //stephen.liu, for vty_read execute vtysh function. 20160811
    g_vtysh_exec_command = vtysh_cmd;

    //stephen add, 20150928
    #ifdef VTYSH_TEST_CMD
    install_node (&test_node, NULL); //stephen, 20150928
    vtysh_install_default (TEST_NODE);
    install_element (CONFIG_NODE, &router_test_cmd);
    install_element (TEST_NODE, &test_config_input_yn_cmd);
    install_element (CONFIG_NODE, &terminal_show_process_info_cmd);
    #endif

    install_element (ENABLE_NODE, &vtysh_exit_all_cmd);
    install_element (CONFIG_NODE, &vtysh_exit_all_cmd);

	/*add by jq.deng, 20160304*/
    install_node(&vtyNodeConfigDisableDhcpSnoopingInterAction, NULL);
    install_element(CONFIG_DISABLE_DHCP_SNOOPING_NODE, &vtysh_disable_dhcp_snooping_yes_cmd);
    install_element(CONFIG_DISABLE_DHCP_SNOOPING_NODE, &vtysh_disable_dhcp_snooping_no_cmd);
    install_element (CONFIG_NODE, &vty_dhcpsn_switch_cmd);

#if 0	/*close by jq.deng, 20151110*/
    install_node (&ge_node, NULL); //stephen, 20150928
    vtysh_install_default (INTERFACE_GE_NODE);
    install_element (CONFIG_NODE, &interface_ge_cmd);
#endif

    install_node (&vtyNodeIfGe, NULL); 			/*add by jq.deng, 20151110*/
    install_element (CONFIG_NODE, &vty_enter_interface_ge_node_cmd);
    install_element (INTERFACE_GE_NODE, &vtysh_exit_interface_ge_node_cmd);
    install_element (INTERFACE_GE_NODE, &vtysh_config_end_all_cmd);

    install_element (CONFIG_NODE, &interface_acl_cmd);
    install_node (&acl_basic_node, NULL);       //stephen, 20150928
    vtysh_install_default (ACL_BASIC_NODE);
    install_element (ACL_BASIC_NODE, &vtysh_exit_acl_cmd);

    install_node (&acl_adv_node, NULL);
    vtysh_install_default (ACL_ADV_NODE);
    install_element (ACL_ADV_NODE, &vtysh_exit_acl_cmd);
    install_node (&acl_link_node, NULL);
    vtysh_install_default (ACL_LINK_NODE);
    install_element (ACL_LINK_NODE, &vtysh_exit_acl_cmd);
    install_node (&acl_user_node, NULL);
    vtysh_install_default (ACL_USER_NODE);
    install_element (ACL_USER_NODE, &vtysh_exit_acl_cmd);

    install_element (CONFIG_NODE, &interface_eacl_cmd);
    install_node (&acl_node, NULL);
    vtysh_install_default (ACL_NODE);
    install_element (ACL_NODE, &vtysh_eexit_acl_cmd);

 #ifdef ACL_SUPPORT_IPV6
    install_element (CONFIG_NODE, &interface_acl6_cmd);
    install_node (&acl6_basic_node, NULL);
    vtysh_install_default (ACL6_BASIC_NODE);
    install_node (&acl6_adv_node, NULL);
    vtysh_install_default (ACL6_ADV_NODE);
#endif

    /*system command */
    install_element (ENABLE_NODE, &vtysh_ping_cmd);
    install_element (ENABLE_NODE, &vtysh_ping_ip_cmd);
    install_element (ENABLE_NODE, &vtysh_traceroute_cmd);
    install_element (ENABLE_NODE, &vtysh_traceroute_params_cmd);
    /*install_element (ENABLE_NODE, &vtysh_telnet_cmd);*/
    install_element (ENABLE_NODE, &vtysh_start_shell_cmd);

    install_element_with_right (ENABLE_NODE, CLI_ACCESS_DIAG, &show_vtysh_client_connect_daemon_cmd);

    #ifdef VTYSH_TEST_CMD
    install_element (ENABLE_NODE, &vtysh_cat_cmd);
    #endif
    install_element (ENABLE_NODE, &vtysh_show_log_cmd);
    install_element (ENABLE_NODE, &vtysh_show_alarm_history_cmd);

    install_element (VIEW_NODE, &vtysh_enable_cmd);
    install_element_with_right (ENABLE_NODE, CLI_ACCESS_ADMIN, &vtysh_config_terminal_cmd);

    install_element (VIEW_NODE, &vtysh_exit_all_cmd);
    vtysh_install_default(ENABLE_NODE);
    vtysh_install_default(CONFIG_NODE);

    #ifdef VTYSH_TEST_CMD
    install_element (CONFIG_NODE, &test_func_port_cmd);
    install_element (CONFIG_NODE, &show_test_print_to_all_vty_cmd);
    install_element (CONFIG_NODE, &test_func_port_alarm_cmd);
    install_element (CONFIG_NODE, &debug_test_print_msg_cmd);
    #endif

    install_element (CONFIG_NODE, &vtysh_ping_cmd);
    install_element (CONFIG_NODE, &vtysh_ping_ip_cmd);
    install_element (CONFIG_NODE, &vtysh_traceroute_cmd);
    install_element (CONFIG_NODE, &vtysh_traceroute_params_cmd);
    /*install_element (CONFIG_NODE, &vtysh_telnet_cmd);*/
    install_element (CONFIG_NODE, &vtysh_start_shell_cmd);
    install_element (CONFIG_NODE, &vtysh_show_log_cmd);
    install_element (CONFIG_NODE, &vtysh_show_alarm_history_cmd);
    //install_element (CONFIG_NODE, &debug_test_multiple_parameter_cmd);

    /*install_element (ENABLE_NODE, &vtysh_show_running_config_cmd);*/

    /*install_element (ENABLE_NODE, &vtysh_copy_runningconfig_startupconfig_cmd);*/
    /*install_element (ENABLE_NODE, &vtysh_write_file_cmd);*/
    install_element_with_right (ENABLE_NODE, ENUM_ACCESS_OP_ROOT, &vtysh_system_cfg_save_cmd);
    //install_element (ENABLE_NODE, &vtysh_system_show_saved_cfg_cmd);
    install_element (ENABLE_NODE, &vtysh_system_show_current_cfg_cmd);
    install_element_with_right (CONFIG_NODE, ENUM_ACCESS_OP_ROOT, &vtysh_system_cfg_save_cmd);
    //install_element (CONFIG_NODE, &vtysh_system_show_saved_cfg_cmd);
    install_element (CONFIG_NODE, &vtysh_system_show_current_cfg_cmd);

    install_node (&vtyNodeDbaProfile, NULL);
    install_node (&vtyNodeLineProfile, NULL);
    install_node (&vtyNodeSrvProfile, NULL);
    install_node (&vtyNodeSlaProfile, NULL);
    //install_node (&vtyNodeClassificationProfile, NULL);
    install_node (&vtyNodeIfXge, NULL);
    install_node (&vtyNodeIfSa, NULL);
    install_node (&vtyNodeIfGtf, NULL);

    install_element_with_style (CONFIG_NODE, CLI_STYLE_GENERAL, &vty_enter_dba_profile_node_cmd);
    install_element_with_style (CONFIG_NODE, CLI_STYLE_GENERAL, &vty_enter_dba_profile_node_none_arg_cmd);
    install_element_with_style (DBA_PROFILE_NODE, CLI_STYLE_GENERAL, &vtysh_exit_dba_profile_node_cmd);

    install_element_with_style (CONFIG_NODE, CLI_STYLE_GENERAL, &vty_enter_line_profile_node_cmd);
    install_element_with_style (CONFIG_NODE, CLI_STYLE_GENERAL, &vty_enter_line_profile_node_none_arg_cmd);
    install_element_with_style (LINE_PROFILE_NODE, CLI_STYLE_GENERAL, &vtysh_exit_line_profile_node_cmd);
    //install_element (LINE_PROFILE_NODE, &vtysh_config_end_all_cmd);

    install_element_with_style (CONFIG_NODE, CLI_STYLE_GENERAL, &vty_enter_srv_profile_node_cmd);
    install_element_with_style (CONFIG_NODE, CLI_STYLE_GENERAL, &vty_enter_srv_profile_node_none_arg_cmd);
    install_element_with_style (SRV_PROFILE_NODE, CLI_STYLE_GENERAL, &vtysh_exit_srv_profile_node_cmd);
    //install_element (SRV_PROFILE_NODE, &vtysh_config_end_all_cmd);

    install_element_with_style (CONFIG_NODE, CLI_STYLE_GENERAL, &vty_enter_sla_profile_node_cmd);
    install_element_with_style (CONFIG_NODE, CLI_STYLE_GENERAL, &vty_enter_sla_profile_node_none_arg_cmd);
    install_element_with_style (SLA_PROFILE_NODE, CLI_STYLE_GENERAL, &vtysh_exit_sla_profile_node_cmd);
    //install_element (SLA_PROFILE_NODE, &vtysh_config_end_all_cmd);

    //install_element_with_style (CONFIG_NODE, CLI_STYLE_GENERAL, &vty_enter_classification_profile_node_cmd);
    //install_element_with_style (CONFIG_NODE, CLI_STYLE_GENERAL, &vty_enter_classification_profile_node_none_arg_cmd);
    //install_element_with_style (CLASSIFICATION_PROFILE_NODE, CLI_STYLE_GENERAL, &vtysh_exit_classification_profile_node_cmd);
    //install_element (CLASSIFICATION_PROFILE_NODE, &vtysh_config_end_all_cmd);

    install_node (&vtyNodeDbaProfileInterAction, NULL);
    install_element_with_style (DBA_PROFILE_INTERACTION_NODE, CLI_STYLE_GENERAL, &vtysh_dba_profile_interaction_yes_cmd);
    install_element_with_style (DBA_PROFILE_INTERACTION_NODE, CLI_STYLE_GENERAL, &vtysh_dba_profile_interaction_no_cmd);

    install_node (&vtyNodeLineProfileInterAction, NULL);
    install_element_with_style (LINE_PROFILE_INTERACTION_NODE, CLI_STYLE_GENERAL, &vtysh_line_profile_interaction_yes_cmd);
    install_element_with_style (LINE_PROFILE_INTERACTION_NODE, CLI_STYLE_GENERAL, &vtysh_line_profile_interaction_no_cmd);

    install_node (&vtyNodeSrvProfileInterAction, NULL);
    install_element_with_style (SRV_PROFILE_INTERACTION_NODE, CLI_STYLE_GENERAL, &vtysh_srv_profile_interaction_yes_cmd);
    install_element_with_style (SRV_PROFILE_INTERACTION_NODE, CLI_STYLE_GENERAL, &vtysh_srv_profile_interaction_no_cmd);

    install_node (&vtyNodeSlaProfileInterAction, NULL);
    install_element_with_style (SLA_PROFILE_INTERACTION_NODE, CLI_STYLE_GENERAL, &vtysh_sla_profile_interaction_yes_cmd);
    install_element_with_style (SLA_PROFILE_INTERACTION_NODE, CLI_STYLE_GENERAL, &vtysh_sla_profile_interaction_no_cmd);

    //install_node (&vtyNodeClassificationProfileInterAction, NULL);
    //install_element_with_style (CLASSIFICATION_PROFILE_INTERACTION_NODE, CLI_STYLE_GENERAL, &vtysh_classification_profile_interaction_yes_cmd);
    //install_element_with_style (CLASSIFICATION_PROFILE_INTERACTION_NODE, CLI_STYLE_GENERAL, &vtysh_classification_profile_interaction_no_cmd);
    
    install_element (INTERFACE_GNODE, &vty_interface_gont_delete_all_cmd);
    install_node (&vtyNodeGtfOntDelInterAction, NULL);
    install_element (GONT_DELETE_ALL_INTERACTION_NODE, &vty_interface_gont_delete_all_yes_cmd);
    install_element (GONT_DELETE_ALL_INTERACTION_NODE, &vty_interface_gont_delete_all_no_cmd);
    
    install_element (CONFIG_NODE, &vty_enter_interface_xge_node_cmd);    
    install_element (INTERFACE_XGE_NODE, &vtysh_exit_interface_xge_node_cmd);
    install_element (INTERFACE_XGE_NODE, &vtysh_config_end_all_cmd);

    install_element (CONFIG_NODE, &vty_enter_interface_sa_node_cmd);
    install_element (INTERFACE_SA_NODE, &vty_exit_interface_sa_node_cmd);
    install_element (INTERFACE_SA_NODE, &vtysh_config_end_all_cmd);
    
    install_element (CONFIG_NODE, &vty_enter_interface_gnode_cmd);
    install_element (INTERFACE_GNODE, &vtysh_exit_interface_gnode_cmd);
    install_element (INTERFACE_GNODE, &vtysh_config_end_all_cmd);

    install_node(&mgmt_if_node, NULL);
    vtysh_install_default(INTERFACE_MGMT_NODE);
    install_element (CONFIG_NODE, &vty_enter_interface_mgmt_node_cmd);
    install_element (INTERFACE_MGMT_NODE, &vtysh_exit_interface_mgmt_node_cmd);

    install_node(&vlan_if_node, NULL);
    vtysh_install_default(INTERFACE_VLANIF_NODE);
    install_element (CONFIG_NODE, &vty_enter_interface_vlanif_node_cmd);
    install_element (INTERFACE_VLANIF_NODE, &vtysh_exit_interface_vlanif_node_cmd);
	/* add by ben.zheng  2015.11.12*/
	install_node (&vtyNodeMvlan, NULL);
	install_element (CONFIG_NODE, &vty_enter_multicast_vlan_node_cmd);
	install_element (INTERFACE_MVLAN_NODE, &vty_exit_multicast_vlan_node_cmd);
	install_element (INTERFACE_MVLAN_NODE, &vtysh_config_end_all_cmd);
    install_element (INTERFACE_MVLAN_NODE, &vty_enter_btv_node_cmd);

	install_node (&vtyNodeBtv, NULL);
	install_element (CONFIG_NODE, &vty_enter_btv_node_cmd);
	install_element (INTERFACE_BTV_NODE, &vty_exit_btv_node_cmd);
	install_element (INTERFACE_BTV_NODE, &vtysh_config_end_all_cmd);
	install_element (INTERFACE_BTV_NODE, &vty_enter_multicast_vlan_node_cmd);

	install_node(&vtyNodeEnableRebootInterAction, NULL);
	install_element_with_right (ENABLE_NODE, ENUM_ACCESS_OP_ROOT | ENUM_ACCESS_OP_DIAGNOSE, &vtysh_system_reboot_cmd);
	install_element (ENABLE_REBOOT_INTERACTION_NODE, &vtysh_system_reboot_yes_cmd);
	install_element (ENABLE_REBOOT_INTERACTION_NODE, &vtysh_system_reboot_no_cmd);

	install_node(&vtyNodeConfigRebootInterAction, NULL);
	install_element_with_right (CONFIG_NODE, ENUM_ACCESS_OP_ROOT | ENUM_ACCESS_OP_DIAGNOSE, &vtysh_system_reboot_cmd);
	install_element (CONFIG_REBOOT_INTERACTION_NODE, &vtysh_system_reboot_yes_cmd);
	install_element (CONFIG_REBOOT_INTERACTION_NODE, &vtysh_system_reboot_no_cmd);

	install_element (CONFIG_NODE, &vtysh_alarm_priority_severity_cmd);
    install_element (CONFIG_NODE, &vtysh_syslog_priority_severity_cmd);
    install_element (CONFIG_NODE, &vtysh_show_alarm_priority_severity_cmd);
    install_element (CONFIG_NODE, &vtysh_show_syslog_priority_severity_cmd);

    install_element_with_right (CONFIG_NODE, ENUM_ACCESS_OP_DIAGNOSE, &vtysh_show_process_info_cmd);

    install_element_with_right(CONFIG_NODE, ENUM_ACCESS_OP_ROOT, &vtysh_system_cfg_erase_cmd);
    install_node(&vtyNodeConfigEraseCfgInterAction, NULL);
    install_element_with_right(CONFIG_ERASE_CFG_INTERACTION_NODE, ENUM_ACCESS_OP_ROOT, &vtysh_system_cfg_erase_yes_cmd);
    install_element_with_right(CONFIG_ERASE_CFG_INTERACTION_NODE, ENUM_ACCESS_OP_ROOT, &vtysh_system_cfg_erase_no_cmd);

    install_element_with_right(CONFIG_NODE, ENUM_ACCESS_OP_ROOT, &vtysh_system_cfg_load_cmd);
    install_node(&vtyNodeConfigLoadCfgInterAction, NULL);
    install_element_with_right(CONFIG_LOAD_CFG_INTERACTION_NODE, ENUM_ACCESS_OP_ROOT, &vtysh_system_cfg_load_yes_cmd);
    install_element_with_right(CONFIG_LOAD_CFG_INTERACTION_NODE, ENUM_ACCESS_OP_ROOT, &vtysh_system_cfg_load_no_cmd);

    install_node(&manu_node, NULL);
    install_element_with_right(ENABLE_NODE, CLI_ACCESS_MANU, &vty_enter_manu_node_cmd);
    install_element(MANU_NODE, &vty_exit_manu_node_cmd);
    install_element(MANU_NODE, &vtysh_config_end_all_cmd);

    install_element(VIEW_NODE, &vtysh_logout_cmd);
    install_element(ENABLE_NODE, &vtysh_logout_cmd);
    install_element(CONFIG_NODE, &vtysh_logout_cmd);
    install_element(MANU_NODE, &vtysh_logout_cmd);

    install_element(ENABLE_NODE, &debug_mode_vty_cmd);
    install_element(DEBUG_NODE, &debug_exit_mode_vty_cmd);
    install_element(DEBUG_NODE, &vtysh_write_terminal_cmd);

    /* install_element(CONFIG_NODE, &vtysh_default_interface_cmd); */
}

void vtysh_init_vty (void)
{
    /* Make vty structure. */
    vtysh = vty_new ();
    vtysh->fd = STDIN_FILENO;
    vtysh->type = VTY_SHELL;
    vtysh->node = USER_NODE;

    strcpy(vtysh->address, "serial");
    vtysh->port = 0;
    vtysh->fail = 0;
    vtysh->cp = 0;
    //printf("debug:%d\n", __LINE__);

    vty_clear_buf (vtysh);
    vtysh->length = 120;
    vtysh->width = 80;
    vtysh->height = 120;
    vtysh->lines = -1;
    memset (vtysh->hist, 0, sizeof (vtysh->hist));
    vtysh->hp = 0;
    vtysh->hindex = 0;
    vtysh->v_timeout = VTY_TIMEOUT_DEFAULT;
    //printf("debug:%d\n", __LINE__);
    //need init all daemon
    init_vty_all_daemon(vtysh);

}



