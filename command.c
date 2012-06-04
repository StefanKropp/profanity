/* 
 * command.c
 *
 * Copyright (C) 2012 James Booth <boothj5@gmail.com>
 * 
 * This file is part of Profanity.
 *
 * Profanity is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * Profanity is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with Profanity.  If not, see <http://www.gnu.org/licenses/>.
 *
 */

#include <string.h>
#include <stdlib.h>

#include <glib.h>

#include "command.h"
#include "contact_list.h"
#include "history.h"
#include "jabber.h"
#include "ui.h"
#include "util.h"
#include "preferences.h"
#include "prof_autocomplete.h"

static gboolean _handle_command(const char * const command, 
    const char * const inp);
static gboolean _cmd_quit(const char * const inp);
static gboolean _cmd_help(const char * const inp);
static gboolean _cmd_who(const char * const inp);
static gboolean _cmd_ros(const char * const inp);
static gboolean _cmd_connect(const char * const inp);
static gboolean _cmd_msg(const char * const inp);
static gboolean _cmd_close(const char * const inp);
static gboolean _cmd_set_beep(const char * const inp);
static gboolean _cmd_set_flash(const char * const inp);
static gboolean _cmd_set_showsplash(const char * const inp);
static gboolean _cmd_away(const char * const inp);
static gboolean _cmd_online(const char * const inp);
static gboolean _cmd_dnd(const char * const inp);
static gboolean _cmd_chat(const char * const inp);
static gboolean _cmd_xa(const char * const inp);
static gboolean _cmd_default(const char * const inp);
static void _update_presence(const jabber_presence_t presence, 
    const char * const show, const char * const inp);

struct cmd_t {
    const gchar *cmd;
    gboolean (*func)(const char * const inp);
};

static PAutocomplete commands_ac;

static struct cmd_t commands[] = {
    { "/away", _cmd_away },
    { "/beep", _cmd_set_beep },
    { "/chat", _cmd_chat },
    { "/close", _cmd_close },
    { "/connect", _cmd_connect },
    { "/dnd", _cmd_dnd },
    { "/flash", _cmd_set_flash },
    { "/help", _cmd_help },
    { "/msg", _cmd_msg },
    { "/online", _cmd_online },
    { "/quit", _cmd_quit },
    { "/ros", _cmd_ros },
    { "/showsplash", _cmd_set_showsplash },
    { "/who", _cmd_who },
    { "/xa", _cmd_xa },
};

static const int num_cmds = 15;
    
gboolean process_input(char *inp)
{
    gboolean result = FALSE;

    g_strstrip(inp);

    if (strlen(inp) > 0)
        history_append(inp);

    if (strlen(inp) == 0) {
        result = TRUE;
    } else if (inp[0] == '/') {
        char inp_cpy[strlen(inp) + 1];
        strcpy(inp_cpy, inp);
        char *command = strtok(inp_cpy, " ");
        result = _handle_command(command, inp);
    } else {
        result = _cmd_default(inp);
    }

    inp_clear();
    reset_search_attempts();
    win_page_off();

    return result;
}

void command_init(void)
{
    commands_ac = p_autocomplete_new();

    int i;
    for (i = 0; i < num_cmds; i++) {
        struct cmd_t *pcmd = commands+i;
        p_autocomplete_add(commands_ac, (gchar *)pcmd->cmd);
    }

    history_init();
}

char * cmd_complete(char *inp)
{
    return p_autocomplete_complete(commands_ac, inp);
}

void reset_command_completer(void)
{
    p_autocomplete_reset(commands_ac);
}

static gboolean _handle_command(const char * const command, const char * const inp)
{
    int i;
    for (i = 0; i < num_cmds; i++) {
        struct cmd_t *pcmd = commands+i;
        if (strcmp(pcmd->cmd, command) == 0) {
            return (pcmd->func(inp));
        }
    }

    return _cmd_default(inp);
}

static gboolean _cmd_connect(const char * const inp)
{
    gboolean result = FALSE;
    jabber_conn_status_t conn_status = jabber_connection_status();

    if ((conn_status != JABBER_DISCONNECTED) && (conn_status != JABBER_STARTED)) {
        cons_show("You are either connected already, or a login is in process.");
        result = TRUE;
    } else if (strlen(inp) < 10) {
        cons_show("Usage: /connect user@host");
        result = TRUE;
    } else {
        char *user;
        user = strndup(inp+9, strlen(inp)-9);
        status_bar_get_password();
        status_bar_refresh();
        char passwd[21];
        inp_block();
        inp_get_password(passwd);
        inp_non_block();
        
        conn_status = jabber_connect(user, passwd);
        if (conn_status == JABBER_CONNECTING)
            cons_show("Connecting...");
        if (conn_status == JABBER_DISCONNECTED)
            cons_bad_show("Connection to server failed.");

        result = TRUE;
    }
    
    return result;
}

static gboolean _cmd_quit(const char * const inp)
{
    return FALSE;
}

static gboolean _cmd_help(const char * const inp)
{
    cons_help();

    return TRUE;
}

static gboolean _cmd_ros(const char * const inp)
{
    jabber_conn_status_t conn_status = jabber_connection_status();

    if (conn_status != JABBER_CONNECTED)
        cons_show("You are not currently connected.");
    else
        jabber_roster_request();

    return TRUE;
}

static gboolean _cmd_who(const char * const inp)
{
    jabber_conn_status_t conn_status = jabber_connection_status();

    if (conn_status != JABBER_CONNECTED) {
        cons_show("You are not currently connected.");
    } else {
        GSList *list = get_contact_list();
        cons_show_online_contacts(list);
    }

    return TRUE;
}

static gboolean _cmd_msg(const char * const inp)
{
    char *usr = NULL;
    char *msg = NULL;

    jabber_conn_status_t conn_status = jabber_connection_status();

    if (conn_status != JABBER_CONNECTED) {
        cons_show("You are not currently connected.");
    } else {
        // copy input    
        char inp_cpy[strlen(inp) + 1];
        strcpy(inp_cpy, inp);

        // get user
        strtok(inp_cpy, " ");
        usr = strtok(NULL, " ");
        if ((usr != NULL) && (strlen(inp) > (5 + strlen(usr) + 1))) {
            // get message
            msg = strndup(inp+5+strlen(usr)+1, strlen(inp)-(5+strlen(usr)+1));
            if (msg != NULL) {
                jabber_send(msg, usr);
                win_show_outgoing_msg("me", usr, msg);
            } else {
                cons_show("Usage: /msg user@host message");
            }
        } else {
            cons_show("Usage: /msg user@host message");
        }
    }

    return TRUE;
}

static gboolean _cmd_close(const char * const inp)
{
    if (!win_close_win())
        cons_bad_command(inp);
    
    return TRUE;
}

static gboolean _cmd_set_beep(const char * const inp)
{
    if (strcmp(inp, "/beep on") == 0) {
        cons_show("Sound enabled.");
        prefs_set_beep(TRUE);
    } else if (strcmp(inp, "/beep off") == 0) {
        cons_show("Sound disabled.");
        prefs_set_beep(FALSE);
    } else {
        cons_show("Usage: /beep <on/off>");
    }        

    return TRUE;
}

static gboolean _cmd_set_flash(const char * const inp)
{
    if (strcmp(inp, "/flash on") == 0) {
        cons_show("Screen flash enabled.");
        prefs_set_flash(TRUE);
    } else if (strcmp(inp, "/flash off") == 0) {
        cons_show("Screen flash disabled.");
        prefs_set_flash(FALSE);
    } else {
        cons_show("Usage: /flash <on/off>");
    }        

    return TRUE;
}

static gboolean _cmd_set_showsplash(const char * const inp)
{
    if (strcmp(inp, "/showsplash on") == 0) {
        cons_show("Splash screen enabled.");
        prefs_set_showsplash(TRUE);
    } else if (strcmp(inp, "/showsplash off") == 0) {
        cons_show("Splash screen disabled.");
        prefs_set_showsplash(FALSE);
    } else {
        cons_show("Usage: /showsplash <on/off>");
    }

    return TRUE;
}

static gboolean _cmd_away(const char * const inp)
{
    _update_presence(PRESENCE_AWAY, "away", inp);
    return TRUE;
}

static gboolean _cmd_online(const char * const inp)
{
    _update_presence(PRESENCE_ONLINE, "online", inp);
    return TRUE;
}

static gboolean _cmd_dnd(const char * const inp)
{
    _update_presence(PRESENCE_DND, "dnd", inp);
    return TRUE;
}

static gboolean _cmd_chat(const char * const inp)
{
    _update_presence(PRESENCE_CHAT, "chat", inp);
    return TRUE;
}

static gboolean _cmd_xa(const char * const inp)
{
    _update_presence(PRESENCE_XA, "xa", inp);
    return TRUE;
}

static gboolean _cmd_default(const char * const inp)
{
    if (win_in_chat()) {
        char *recipient = win_get_recipient();
        jabber_send(inp, recipient);
        win_show_outgoing_msg("me", recipient, inp);
        free(recipient);
    } else {
        cons_bad_command(inp);
    }

    return TRUE;
}

static void _update_presence(const jabber_presence_t presence, 
    const char * const show, const char * const inp)
{
    char *msg;
    if (strlen(inp) > strlen(show) + 2) {
        msg = strndup(inp+(strlen(show) + 2), strlen(inp)-(strlen(show) + 2));
    } else {
        msg = NULL;
    }

    jabber_conn_status_t conn_status = jabber_connection_status();
    
    if (conn_status != JABBER_CONNECTED) {
        cons_show("You are not currently connected.");
    } else {
        jabber_update_presence(presence, msg);
        title_bar_set_status(presence);
        if (msg != NULL) {
            char str[14 + strlen(show) + 3 + strlen(msg) + 2];
            sprintf(str, "Status set to %s, \"%s\"", show, msg);
            cons_show(str);
            free(msg);
        } else {
            char str[14 + strlen(show) + 1];
            sprintf(str, "Status set to %s", show);
            cons_show(str);
        }
    }

} 
