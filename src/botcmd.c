/*
 * botcmd.c -- handles: commands that comes across the botnet userfile
 * transfer and update commands from sharebots
 * 
 * dprintf'ized, 10nov1995 
 */
/*
 * This file is part of the eggdrop source code copyright (c) 1997 Robey
 * Pointer and is distributed according to the GNU general public license.
 * For full details, read the top of 'main.c' or the file called COPYING
 * that was distributed with this code. 
 */

#include "main.h"
#include "tandem.h"
#include "users.h"
#include "chan.h"
#include "modules.h"

extern struct dcc_t *dcc;
extern char botnetnick[];
extern struct chanset_t *chanset;
extern int dcc_total;
extern char ver[];
extern char admin[];
extern Tcl_Interp *interp;
extern time_t now, online_since;
extern char network[];
extern struct userrec *userlist;
extern int remote_boots;
extern char motdfile[];
extern party_t *party;
extern int noshare;
extern module_entry *module_list;

static char TBUF[1024];		/* static buffer for goofy bot stuff */

static char base64to[256] =
{
  0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
  0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
  0, 0, 0, 0, 0, 0, 0, 0, 52, 53, 54, 55, 56, 57, 58, 59, 60, 61, 0, 0,
  0, 0, 0, 0, 0, 0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14,
  15, 16, 17, 18, 19, 20, 21, 22, 23, 24, 25, 62, 0, 63, 0, 0, 0, 26, 27, 28,
  29, 30, 31, 32, 33, 34, 35, 36, 37, 38, 39, 40, 41, 42, 43, 44, 45, 46, 47, 48,
  49, 50, 51, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
  0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
  0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
  0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
  0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
  0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
  0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0
};

int base64_to_int(char *buf)
{
  int i = 0;

  while (*buf) {
    i = i << 6;
    i += base64to[(int) *buf];
    buf++;
  }
  return i;
};

/* used for 1.0 compatibility: if a join message arrives with no sock#, */
/* i'll just grab the next "fakesock" # (incrementing to assure uniqueness) */
static int fakesock = 2300;

static void fake_alert(int idx, char *item, char *extra)
{
#ifndef NO_OLD_BOTNET
  if (b_numver(idx) < NEAT_BOTNET)
    dprintf(idx, "chat %s NOTICE: %s (%s != %s).\n",
	    botnetnick, NET_FAKEREJECT, item, extra);
  else
#endif
    dprintf(idx, "ct %s NOTICE: %s (%s != %s).\n",
	    botnetnick, NET_FAKEREJECT, item, extra);
  putlog(LOG_BOTS, "*", "%s %s (%s != %s).", dcc[idx].nick, NET_FAKEREJECT,
	 item, extra);
}

/* chan <from> <chan> <text> */
static void bot_chan2(int idx, char *msg)
{
  char *from, *p;
  int i, chan;

  context;
  if (bot_flags(dcc[idx].user) & BOT_ISOLATE)
    return;
  from = newsplit(&msg);
  p = newsplit(&msg);
#ifndef NO_OLD_BOTNET
  if (b_numver(idx) < NEAT_BOTNET)
    chan = atoi(p);
  else
#endif
    chan = base64_to_int(p);
  /* strip annoying control chars */
  for (p = from; *p;) {
    if ((*p < 32) || (*p == 127))
      strcpy(p, p + 1);
    else
      p++;
  }
  p = strchr(from, '@');
  if (p) {
    sprintf(TBUF, "<%s> %s", from, msg);
    *p = 0;
    if (!partyidle(p + 1, from)) {
      *p = '@';
      fake_alert(idx, "user", from);
      return;
    }
    *p = '@';
    p++;
  } else {
    sprintf(TBUF, "*** (%s) %s", from, msg);
    p = from;
  }
  i = nextbot(p);
  if (i != idx) {
    fake_alert(idx, "direction", p);
  } else {
    chanout_but(-1, chan, "%s\n", TBUF);
    /* send to new version bots */
    botnet_send_chan(idx, from, NULL, chan, msg);
    if (strchr(from, '@') != NULL)
      check_tcl_chat(from, chan, msg);
    else
      check_tcl_bcst(from, chan, msg);
  }
}

/* chat <from> <notice>  -- only from bots */
static void bot_chat(int idx, char *par)
{
  char *from;
  int i;

  context;
  if (bot_flags(dcc[idx].user) & BOT_ISOLATE)
    return;
  from = newsplit(&par);
  if (strchr(from, '@') != NULL) {
    fake_alert(idx, "bot", from);
    return;
  }
  /* make sure the bot is valid */
  i = nextbot(from);
  if (i != idx) {
    fake_alert(idx, "direction", from);
    return;
  }
  chatout("*** (%s) %s\n", from, par);
  botnet_send_chat(idx, from, par);
}

/* actchan <from> <chan> <text> */
static void bot_actchan(int idx, char *par)
{
  char *from, *p;
  int i, chan;

  context;
  if (bot_flags(dcc[idx].user) & BOT_ISOLATE)
    return;
  from = newsplit(&par);
  p = strchr(from, '@');
  if (p == NULL) {
    /* how can a bot do an action? */
    fake_alert(idx, "user@bot", from);
    return;
  }
  *p = 0;
  if (!partyidle(p + 1, from)) {
    *p = '@';
    fake_alert(idx, "user", from);
  }
  *p = '@';
  p++;
  i = nextbot(p);
  if (i != idx) {
    fake_alert(idx, "direction", p);
    return;
  }
  p = newsplit(&par);
#ifndef NO_OLD_BOTNET
  if (b_numver(idx) < NEAT_BOTNET)
    chan = atoi(p);
  else
#endif
    chan = base64_to_int(p);
  for (p = from; *p;) {
    if ((*p < 32) || (*p == 127))
      strcpy(p, p + 1);
    else
      p++;
  }
  chanout_but(-1, chan, "* %s %s\n", from, par);
  botnet_send_act(idx, from, NULL, chan, par);
  check_tcl_act(from, chan, par);
}

/* priv <from> <to> <message> */
static void bot_priv(int idx, char *par)
{
  char *from, *p, *to = TBUF, *tobot;
  int i;

  context;
  from = newsplit(&par);
  tobot = newsplit(&par);
  splitc(to, tobot, '@');
  p = strchr(from, '@');
  if ((to[0] == '!') || (to[0] == '#'))
    to++;
  if (p != NULL)
    p++;
  else
    p = from;
  i = nextbot(p);
  if (i != idx) {
    fake_alert(idx, "direction", p);
    return;
  }
  if (!to[0])
    return;			/* silently ignore notes to '@bot' this
				 * is legacy code */
  if (!strcasecmp(tobot, botnetnick)) {		/* for me! */
    if (p == from)
      add_note(to, from, par, -2, 0);
    else {
      i = add_note(to, from, par, -1, 0);
      if (from[0] != '@')
	switch (i) {
	case NOTE_ERROR:
	  botnet_send_priv(idx, botnetnick, from, NULL,
			   "%s %s.", BOT_NOSUCHUSER, to);
	  break;
	case NOTE_STORED:
	  botnet_send_priv(idx, botnetnick, from, NULL,
			   "%s", BOT_NOTESTORED2);
	  break;
	case NOTE_FULL:
	  botnet_send_priv(idx, botnetnick, from, NULL,
			   "%s", BOT_NOTEBOXFULL);
	  break;
	case NOTE_AWAY:
	  botnet_send_priv(idx, botnetnick, from, NULL,
			   "%s %s", to, BOT_NOTEISAWAY);
	  break;
	case NOTE_FWD:
	  botnet_send_priv(idx, botnetnick, from, NULL,
			   "%s %s", "Not online; note forwarded to:", to);
	  break;
	case NOTE_TCL:
	  break;		/* do nothing */
	case NOTE_OK:
	  botnet_send_priv(idx, botnetnick, from, NULL,
			   "%s %s.", BOT_NOTESENTTO, to);
	  break;
	}
    }
  } else {			/* pass it on */
    i = nextbot(tobot);
    if (i >= 0)
      botnet_send_priv(i, from, to, tobot, "%s", par);
  }
}

static void bot_bye(int idx, char *par)
{
  char s[1024];

  context;
  simple_sprintf(s, "%s %s. %s", BOT_DISCONNECTED, dcc[idx].nick, par);
  putlog(LOG_BOTS, "*", "%s", s);
  chatout("*** %s\n", s);
  botnet_send_unlinked(idx, dcc[idx].nick, s);
  dprintf(idx, "*bye\n");
  killsock(dcc[idx].sock);
  lostdcc(idx);
}

static void remote_tell_who(int idx, char *nick, int chan)
{
  int i = 10, k, l, ok = 0;
  char s[1024];
  struct chanset_t *c;

  context;
  strcpy(s, "Channels: ");
  c = chanset;
  while (c != NULL) {
    if (!channel_secret(c)) {
      l = strlen(c->name);
      if (i + l < 1021) {
	if (i > 10) {
	  s[i++] = ',';
	  s[i++] = ' ';
	}
	strcpy(s + i, c->name);
	i += (l + 2);
      }
    }
    c = c->next;
  }
  if (i > 10) {
    botnet_send_priv(idx, botnetnick, nick, NULL, "%s  (%s)", s, ver);
  } else
    botnet_send_priv(idx, botnetnick, nick, NULL, "%s  (%s)", BOT_NOCHANNELS, ver);
  if (admin[0])
    botnet_send_priv(idx, botnetnick, nick, NULL, "Admin: %s", admin);
  if (chan == 0)
    botnet_send_priv(idx, botnetnick, nick, NULL,
		     "%s  (* = %s, + = %s, @ = %s)",
		     BOT_PARTYMEMBS, MISC_OWNER, MISC_MASTER, MISC_OP);
  else {
    simple_sprintf(s, "assoc %d", chan);
    if ((Tcl_Eval(interp, s) != TCL_OK) || !interp->result[0])
      botnet_send_priv(idx, botnetnick, nick, NULL,
		       "%s %s%d:  (* = %s, + = %s, @ = %s)\n",
		       BOT_PEOPLEONCHAN,
		       (chan < GLOBAL_CHANS) ? "" : "*",
		       chan % GLOBAL_CHANS,
		       MISC_OWNER, MISC_MASTER, MISC_OP);
    else
      botnet_send_priv(idx, botnetnick, nick, NULL,
		       "%s '%s' (%s%d):  (* = %s, + = %s, @ = %s)\n",
		       BOT_PEOPLEONCHAN, interp->result,
		       (chan < 100000) ? "" : "*", chan % 100000,
		       MISC_OWNER, MISC_MASTER, MISC_OP);
  }
  for (i = 0; i < dcc_total; i++)
    if (dcc[i].type->flags & DCT_REMOTEWHO)
      if (dcc[i].u.chat->channel == chan) {
	k = sprintf(s, "  %c%-15s %s",
		    (geticon(i) == '-' ? ' ' : geticon(i)),
		    dcc[i].nick, dcc[i].host);
	if (now - dcc[i].timeval > 300) {
	  unsigned long days, hrs, mins;

	  days = (now - dcc[i].timeval) / 86400;
	  hrs = ((now - dcc[i].timeval) - (days * 86400)) / 3600;
	  mins = ((now - dcc[i].timeval) - (hrs * 3600)) / 60;
	  if (days > 0)
	    sprintf(s + k, " (%s %lud%luh)",
		    MISC_IDLE, days, hrs);
	  else if (hrs > 0)
	    sprintf(s + k, " (%s %luh%lum)",
		    MISC_IDLE, hrs, mins);
	  else
	    sprintf(s + k, " (%s %lum)",
		    MISC_IDLE, mins);
	}
	botnet_send_priv(idx, botnetnick, nick, NULL, "%s", s);
	if (dcc[i].u.chat->away != NULL)
	  botnet_send_priv(idx, botnetnick, nick, NULL, "      %s: %s",
			   MISC_AWAY, dcc[i].u.chat->away);
      }
  for (i = 0; i < dcc_total; i++)
    if (dcc[i].type == &DCC_BOT) {
      if (!ok) {
	ok = 1;
	botnet_send_priv(idx, botnetnick, nick, NULL,
			 "%s:", BOT_BOTSCONNECTED);
      }
      sprintf(s, "  %s%c%-15s %s",
	      dcc[i].status & STAT_CALLED ? "<-" : "->",
	      dcc[i].status & STAT_SHARE ? '+' : ' ',
	      dcc[i].nick, dcc[i].u.bot->version);
      botnet_send_priv(idx, botnetnick, nick, NULL, "%s", s);
    }
  ok = 0;
  for (i = 0; i < dcc_total; i++)
    if (dcc[i].type->flags & DCT_REMOTEWHO)
      if (dcc[i].u.chat->channel != chan) {
	if (!ok) {
	  ok = 1;
	  botnet_send_priv(idx, botnetnick, nick, NULL, "%s:",
			   BOT_OTHERPEOPLE);
	}
	l = sprintf(s, "  %c%-15s %s", (geticon(i) == '-' ? ' ' : geticon(i)),
		    dcc[i].nick, dcc[i].host);
	if (now - dcc[i].timeval > 300) {
	  k = (now - dcc[i].timeval) / 60;
	  if (k < 60)
	    sprintf(s + l, " (%s %dm)", MISC_IDLE, k);
	  else
	    sprintf(s + l, " (%s %dh%dm)", MISC_IDLE, k / 60, k % 60);
	}
	botnet_send_priv(idx, botnetnick, nick, NULL, "%s", s);
	if (dcc[i].u.chat->away != NULL)
	  botnet_send_priv(idx, botnetnick, nick, NULL,
			   "      %s: %s", MISC_AWAY,
			   dcc[i].u.chat->away);
      }
}

/* who <from@bot> <tobot> <chan#> */
static void bot_who(int idx, char *par)
{
  char *from, *to, *p;
  int i, chan;

  context;
  from = newsplit(&par);
  p = strchr(from, '@');
  if (!p) {
    sprintf(TBUF, "%s@%s", from, dcc[idx].nick);
    from = TBUF;
  }
  to = newsplit(&par);
  if (!strcasecmp(to, botnetnick))
    to[0] = 0;			/* (for me) */
#ifndef NO_OLD_BOTNET
  if (b_numver(idx) < NEAT_BOTNET)
    chan = atoi(par);
  else
#endif
    chan = base64_to_int(par);
  if (to[0]) {			/* pass it on */
    i = nextbot(to);
    if (i >= 0)
      botnet_send_who(i, from, to, chan);
  } else {
    remote_tell_who(idx, from, chan);
  }
}

static void bot_endlink(int idx, char *par)
{
  dcc[idx].status &= ~STAT_LINKING;
}

/* info? <from@bot>   -> send priv */
static void bot_infoq(int idx, char *par)
{
  char s[161], s2[32];
  struct chanset_t *chan;
  time_t now2;
  int hr, min;

  context;
  chan = chanset;
  now2 = now - online_since;
  s2[0] = 0;
  if (now2 > 86400) {
    int days = now2 / 86400;

    /* days */
    sprintf(s2, "%d day", days);
    if (days >= 2)
      strcat(s2, "s");
    strcat(s2, ", ");
    now2 -= days * 86400;
  }
  hr = (time_t) ((int) now2 / 3600);
  now2 -= (hr * 3600);
  min = (time_t) ((int) now2 / 60);
  sprintf(&s2[strlen(s2)], "%02d:%02d", (int) hr, (int) min);
  if (module_find("server", 0, 0)) {
    s[0] = 0;
    while (chan != NULL) {
      if (!channel_secret(chan)) {
	strcat(s, chan->name);
	strcat(s, ", ");
      }
      chan = chan->next;
    }
    if (s[0]) {
      s[strlen(s) - 2] = 0;
      botnet_send_priv(idx, botnetnick, par, NULL,
		       "%s <%s> (%s) [UP %s]", ver, network, s, s2);
    } else
      botnet_send_priv(idx, botnetnick, par, NULL,
		    "%s <%s> (%s) [UP %s]", ver, network, BOT_NOCHANNELS,
		       s2);
  } else
    botnet_send_priv(idx, botnetnick, par, NULL,
		     "%s <NO_IRC> [UP %s]", ver, s2);
  botnet_send_infoq(idx, par);
}

static void bot_ping(int idx, char *par)
{
  context;
  botnet_send_pong(idx);
}

static void bot_pong(int idx, char *par)
{
  context;
  dcc[idx].status &= ~STAT_PINGED;
}

/* link <from@bot> <who> <to-whom> */
static void bot_link(int idx, char *par)
{
  char *from, *bot, *rfrom;
  int i;

  context;
  from = newsplit(&par);
  bot = newsplit(&par);

  if (!strcasecmp(bot, botnetnick)) {
    if ((rfrom = strchr(from, ':')))
      rfrom++;
    else
      rfrom = from;
    putlog(LOG_CMDS, "*", "#%s# link %s", rfrom, par);
    if (botlink(from, -1, par))
      botnet_send_priv(idx, botnetnick, from, NULL, "%s %s ...",
		       BOT_LINKATTEMPT, par);
    else
      botnet_send_priv(idx, botnetnick, from, NULL, "%s.",
		       BOT_CANTLINKTHERE);
  } else {
    i = nextbot(bot);
    if (i >= 0)
      botnet_send_link(i, from, bot, par);
  }
}

/* unlink <from@bot> <linking-bot> <undesired-bot> <reason> */
static void bot_unlink(int idx, char *par)
{
  char *from, *bot, *rfrom, *p, *undes;
  int i;

  context;
  from = newsplit(&par);
  bot = newsplit(&par);
  undes = newsplit(&par);
  if (!strcasecmp(bot, botnetnick)) {
    if ((rfrom = strchr(from, ':')))
      rfrom++;
    else
      rfrom = from;
    putlog(LOG_CMDS, "*", "#%s# unlink %s (%s)", rfrom, undes, par[0] ? par :
	   "No reason");
    i = botunlink(-3, undes, par[0] ? par : NULL);
    if (i == 1) {
      p = strchr(from, '@');
      if (p) {
	/* idx will change after unlink -- get new idx */
	i = nextbot(p + 1);
	if (i >= 0)
	  botnet_send_priv(i, botnetnick, from, NULL,
			   "Unlinked from %s.", undes);
      }
    } else if (i == 0) {
      botnet_send_unlinked(-1, undes, "");
      p = strchr(from, '@');
      if (p) {
	/* ditto above, about idx */
	i = nextbot(p + 1);
	if (i >= 0)
	  botnet_send_priv(i, botnetnick, from, NULL,
			   "%s %s.", BOT_CANTUNLINK, undes);
      }
    } else {
      p = strchr(from, '@');
      if (p) {
	i = nextbot(p + 1);
	if (i >= 0)
	  botnet_send_priv(i, botnetnick, from, NULL,
			   "Can't remotely unlink sharebots.");
      }
    }
  } else {
    i = nextbot(bot);
    if (i >= 0)
      botnet_send_unlink(i, from, bot, undes, par);
  }
}

/* bot next share? */
static void bot_update(int idx, char *par)
{
  char *bot, x;
  int vnum;

  context;
  bot = newsplit(&par);
  x = par[0];
  if (x)
    par++;
#ifndef NO_OLD_BOTNET
  if (b_numver(idx) < NEAT_BOTNET)
    vnum = atoi(par);
  else
#endif
    vnum = base64_to_int(par);
  if (in_chain(bot))
    updatebot(idx, bot, x, vnum);
}

/* newbot next share? */
static void bot_nlinked(int idx, char *par)
{
  char *newbot, *next, *p, s[1024], x;
  int bogus = 0, i;
  struct userrec *u;

  context;
  newbot = newsplit(&par);
  next = newsplit(&par);
  s[0] = 0;
  if (!next[0]) {
    putlog(LOG_BOTS, "*", "Invalid eggnet protocol from %s (zapfing)",
	   dcc[idx].nick);
    simple_sprintf(s, "%s %s (%s)", MISC_DISCONNECTED, dcc[idx].nick,
		   MISC_INVALIDBOT);
    dprintf(idx, "error invalid eggnet protocol for 'nlinked'\n");
  } else if ((in_chain(newbot)) || (!strcasecmp(newbot, botnetnick))) {
    /* loop! */
    putlog(LOG_BOTS, "*", "%s %s (mutual: %s)",
	   BOT_LOOPDETECT, dcc[idx].nick, newbot);
    simple_sprintf(s, "%s (%s): %s %s", MISC_LOOP, newbot, MISC_DISCONNECTED,
		   dcc[idx].nick);
    dprintf(idx, "error Loop (%s)\n", newbot);
  }
  if (!s[0]) {
    for (p = newbot; *p; p++)
      if ((*p < 32) || (*p == 127) || ((p - newbot) >= HANDLEN))
	bogus = 1;
    i = nextbot(next);
    if (i != idx)
      bogus = 1;
  }
  if (bogus) {
    putlog(LOG_BOTS, "*", "%s %s!  (%s -> %s)", BOT_BOGUSLINK, dcc[idx].nick,
	   next, newbot);
    simple_sprintf(s, "%s: %s %s", BOT_BOGUSLINK, dcc[idx].nick,
		   MISC_DISCONNECTED);
    dprintf(idx, "error %s (%s -> %s)\n",
	    BOT_BOGUSLINK, next, newbot);
  }
  /* beautiful huh? :) */
  if (bot_flags(dcc[idx].user) & BOT_LEAF) {
    putlog(LOG_BOTS, "*", "%s %s  (%s %s)",
	   BOT_DISCONNLEAF, dcc[idx].nick, newbot,
	   BOT_LINKEDTO);
    simple_sprintf(s, "%s %s (to %s): %s",
		   BOT_ILLEGALLINK, dcc[idx].nick, newbot,
		   MISC_DISCONNECTED);
    dprintf(idx, "error %s\n", BOT_YOUREALEAF);
  }
  if (s[0]) {
    chatout("*** %s\n", s);
    botnet_send_unlinked(idx, dcc[idx].nick, s);
    dprintf(idx, "bye\n");
    killsock(dcc[idx].sock);
    lostdcc(idx);
    return;
  }
  x = par[0];
  if (x)
    par++;
  else
    x = '-';
#ifndef NO_OLD_BOTNET
  if (b_numver(idx) < NEAT_BOTNET)
    i = atoi(par);
  else
#endif
    i = base64_to_int(par);
  botnet_send_nlinked(idx, newbot, next, x, i);
  if (x == '!') {
    chatout("*** (%s) %s %s.\n", next, NET_LINKEDTO, newbot);
    x = '-';
  }
  addbot(newbot, dcc[idx].nick, next, x, i);
  check_tcl_link(newbot, next);
  u = get_user_by_handle(userlist, newbot);
  if (bot_flags(u) & BOT_REJECT) {
    botnet_send_reject(idx, botnetnick, NULL, newbot, NULL, NULL);
    putlog(LOG_BOTS, "*", "%s %s %s %s", BOT_REJECTING,
	   newbot, MISC_FROM, dcc[idx].nick);
  }
}

#ifndef NO_OLD_BOTNET
static void bot_linked(int idx, char *par)
{
  char s[1024];

  context;
  putlog(LOG_BOTS, "*", "%s", BOT_OLDBOT);
  simple_sprintf(s, "%s %s (%s)", MISC_DISCONNECTED,
		 dcc[idx].nick, MISC_OUTDATED);
  chatout("*** %s\n", s);
  botnet_send_unlinked(idx, dcc[idx].nick, s);
  killsock(dcc[idx].sock);
  lostdcc(idx);
}

#endif

static void bot_unlinked(int idx, char *par)
{
  int i;
  char *bot;

  context;
  bot = newsplit(&par);
  i = nextbot(bot);
  if ((i >= 0) && (i != idx))	/* bot is NOT downstream along idx, so
				 * BOGUS! */
    fake_alert(idx, "direction", bot);
  else if (i >= 0) {		/* valid bot downstream of idx */
    if (par[0])
      chatout("*** (%s) %s\n", lastbot(bot), par);
    botnet_send_unlinked(idx, bot, par);
    unvia(idx, findbot(bot));
    rembot(bot);
  }
  /* otherwise it's not even a valid bot, so just ignore! */
}

static void bot_trace(int idx, char *par)
{
  char *from, *dest;
  int i;

  /* trace <from@bot> <dest> <chain:chain..> */
  context;
  from = newsplit(&par);
  dest = newsplit(&par);
  simple_sprintf(TBUF, "%s:%s", par, botnetnick);
  botnet_send_traced(idx, from, TBUF);
  if (strcasecmp(dest, botnetnick) && ((i = nextbot(dest)) > 0))
    botnet_send_trace(i, from, dest, par);
}

static void bot_traced(int idx, char *par)
{
  char *to, *p;
  int i, sock;

  /* traced <to@bot> <chain:chain..> */
  context;
  to = newsplit(&par);
  p = strchr(to, '@');
  if (p == NULL)
    p = to;
  else {
    *p = 0;
    p++;
  }
  if (!strcasecmp(p, botnetnick)) {
    time_t t = 0;
    char *p = par, *ss = TBUF;

    splitc(ss, to, ':');
    if (ss[0])
      sock = atoi(ss);
    else
      sock = (-1);
    if (par[0] == ':') {
      t = atoi(par + 1);
      p = strchr(par + 1, ':');
      if (p)
	p++;
      else
	p = par + 1;
    }
    for (i = 0; i < dcc_total; i++)
      if ((dcc[i].type->flags & DCT_CHAT) &&
	  (!strcasecmp(dcc[i].nick, to)) &&
	  ((sock == (-1)) || (sock == dcc[i].sock))) {
	if (t) {
	  dprintf(i, "%s -> %s (%lu secs)\n", BOT_TRACERESULT, p, now - t);
	} else
	  dprintf(i, "%s -> %s\n", BOT_TRACERESULT, p);
      }
  } else {
    i = nextbot(p);
    if (p != to)
      *--p = '@';
    if (i >= 0)
      botnet_send_traced(i, to, par);
  }
}

/* reject <from> <bot> */
static void bot_reject(int idx, char *par)
{
  char *from, *who, *destbot, *frombot;
  struct userrec *u;
  int i;

  context;
  if (bot_flags(dcc[idx].user) & BOT_ISOLATE)
    return;
  from = newsplit(&par);
  frombot = strchr(from, '@');
  if (frombot)
    frombot++;
  else
    frombot = from;
  i = nextbot(frombot);
  if (i != idx) {
    fake_alert(idx, "direction", frombot);
    return;
  }
  who = newsplit(&par);
  if (!(destbot = strchr(who, '@'))) {
    /* rejecting a bot */
    i = nextbot(who);
    if (i < 0) {
      botnet_send_priv(idx, botnetnick, from, NULL, "%s %s (%s)",
		       BOT_CANTUNLINK, who, BOT_DOESNTEXIST);
    } else if (!strcasecmp(dcc[i].nick, who)) {
      char s[1024];

      /* i'm the connection to the rejected bot */
      putlog(LOG_BOTS, "*", "%s %s %s", from, MISC_REJECTED, dcc[i].nick);
      dprintf(i, "bye\n");
      simple_sprintf(s, "%s %s (%s: %s)",
		     MISC_DISCONNECTED, dcc[i].nick, from,
		     par[0] ? par : MISC_REJECTED);
      chatout("*** %s\n", s);
      botnet_send_unlinked(i, dcc[i].nick, s);
      killsock(dcc[i].sock);
      lostdcc(i);
    } else {
      if (i >= 0)
	botnet_send_reject(i, from, NULL, who, NULL, par);
    }
  } else {			/* rejecting user */
    *destbot++ = 0;
    if (!strcasecmp(destbot, botnetnick)) {
      /* kick someone here! */
      int ok = 0;

      if (remote_boots == 1) {
	frombot = strchr(from, '@');
	if (frombot == NULL)
	  frombot = from;
	else
	  frombot++;
	u = get_user_by_handle(userlist, frombot);
	if (!(bot_flags(u) & BOT_SHARE)) {
	  add_note(from, botnetnick, "No non sharebot boots.", -1, 0);
	  ok = 1;
	}
      } else if (remote_boots == 0) {
	botnet_send_priv(idx, botnetnick, from, NULL, "%s",
			 BOT_NOREMOTEBOOT);
	ok = 1;
      }
      for (i = 0; (i < dcc_total) && (!ok); i++)
	if ((!strcasecmp(who, dcc[i].nick)) &&
	    (dcc[i].type->flags & DCT_CHAT)) {
	  u = get_user_by_handle(userlist, dcc[i].nick);
	  if (u && (u->flags & USER_OWNER)) {
	    add_note(from, botnetnick, BOT_NOOWNERBOOT, -1, 0);
	    return;
	  }
	  do_boot(i, from, par);
	  ok = 1;
	  putlog(LOG_CMDS, "*", "#%s# boot %s (%s)", from, dcc[i].nick, par);
	}
    } else {
      i = nextbot(destbot);
      *--destbot = '@';
      if (i >= 0)
	botnet_send_reject(i, from, NULL, who, NULL, par);
    }
  }
}

static void bot_thisbot(int idx, char *par)
{
  context;
  if (strcasecmp(par, dcc[idx].nick) != 0) {
    char s[1024];

    putlog(LOG_BOTS, "*", NET_WRONGBOT, dcc[idx].nick, par);
    dprintf(idx, "bye\n");
    simple_sprintf(s, "%s %s (%s)", MISC_DISCONNECTED, dcc[idx].nick,
		   MISC_IMPOSTER);
    chatout("*** %s\n", s);
    botnet_send_unlinked(idx, dcc[idx].nick, s);
    unvia(idx, findbot(dcc[idx].nick));
    killsock(dcc[idx].sock);
    lostdcc(idx);
    return;
  }
  if (bot_flags(dcc[idx].user) & BOT_LEAF)
    dcc[idx].status |= STAT_LEAF;
  /* set capitalization the way they want it */
  noshare = 1;
  change_handle(dcc[idx].user, par);
  noshare = 0;
  strcpy(dcc[idx].nick, par);
}

static void bot_handshake(int idx, char *par)
{
  struct userrec *u = get_user_by_handle(userlist, dcc[idx].nick);

  /* only set a new password if no old one exists */
  context;
  if (u_pass_match(u, "-")) {
    noshare = 1;		/* we *don't* want botnet passwords
				 * migrating */
    set_user(&USERENTRY_PASS, u, par);
    noshare = 0;
  }
}

/* used to send a direct msg from Tcl on one bot to Tcl on another 
 * zapf <frombot> <tobot> <code [param]>   */
static void bot_zapf(int idx, char *par)
{
  char *from, *to;
  int i;

  context;
  from = newsplit(&par);
  to = newsplit(&par);
  i = nextbot(from);
  if (i != idx) {
    fake_alert(idx, "direction", from);
    return;
  }
  if (!strcasecmp(to, botnetnick)) {
    /* for me! */
    char *opcode;

    opcode = newsplit(&par);
    check_tcl_bot(from, opcode, par);
    return;
  }
  i = nextbot(to);
  if (i >= 0)
    botnet_send_zapf(i, from, to, par);
}

/* used to send a global msg from Tcl on one bot to every other bot 
 * zapf-broad <frombot> <code [param]> */
static void bot_zapfbroad(int idx, char *par)
{
  char *from, *opcode;
  int i;

  context;
  from = newsplit(&par);
  opcode = newsplit(&par);

  i = nextbot(from);
  if (i != idx) {
    fake_alert(idx, "direction", from);
    return;
  }
  check_tcl_bot(from, opcode, par);
  botnet_send_zapf_broad(idx, from, opcode, par);
}

/* show motd to someone */
static void bot_motd(int idx, char *par)
{
  FILE *vv;
  char *s = TBUF, *who, *p;
  int i;
  struct flag_record fr =
  {USER_BOT, 0, 0, 0, 0, 0};

  context;

  who = newsplit(&par);
  if (!par[0] || !strcasecmp(par, botnetnick)) {
    int irc = 0;

    p = strchr(who, ':');
    if (p)
      p++;
    else
      p = who;
    if (who[0] == '!') {
      irc = HELP_IRC;
      fr.global |=USER_HIGHLITE;

      who++;
    } else if (who[0] == '#') {
      fr.global |=USER_HIGHLITE;

      who++;
    }
    putlog(LOG_CMDS, "*", "#%s# motd", p);
    vv = fopen(motdfile, "r");
    if (vv != NULL) {
      botnet_send_priv(idx, botnetnick, who, NULL, "--- %s\n", MISC_MOTDFILE);
      help_subst(NULL, NULL, 0, irc, NULL);
      while (!feof(vv)) {
	fgets(s, 120, vv);
	if (!feof(vv)) {
	  if (s[strlen(s) - 1] == '\n')
	    s[strlen(s) - 1] = 0;
	  if (!s[0])
	    strcpy(s, " ");
	  help_subst(s, who, &fr, HELP_DCC, dcc[idx].nick);
	  if (s[0])
	    botnet_send_priv(idx, botnetnick, who, NULL, "%s", s);
	}
      }
      fclose(vv);
    } else
      botnet_send_priv(idx, botnetnick, who, NULL, "%s :(", MISC_NOMOTDFILE);
  } else {
    /* pass it on */
    i = nextbot(par);
    if (i >= 0)
      botnet_send_motd(i, who, par);
  }
}

/* these are still here, so that they will pass the relevant 
 * requests through even if no filesys is loaded */
/* filereject <bot:filepath> <sock:nick@bot> <reason...> */
static void bot_filereject(int idx, char *par)
{
  char *path, *to, *tobot, *p;
  int i;

  context;
  path = newsplit(&par);
  to = newsplit(&par);
  if ((tobot = strchr(to, '@')))
    tobot++;
  else
    tobot = to;			/* bot wants a file?! :) */
  if (strcasecmp(tobot, botnetnick)) {	/* for me! */
    p = strchr(to, ':');
    if (p != NULL) {
      *p = 0;
      for (i = 0; i < dcc_total; i++) {
	if (dcc[i].sock == atoi(to))
	  dprintf(i, "%s (%s): %s\n", BOT_XFERREJECTED, path, par);
      }
      *p = ':';
    }
    /* no ':'? malformed */
    putlog(LOG_FILES, "*", "%s %s: %s", path, MISC_REJECTED, par);
  } else {			/* pass it on */
    i = nextbot(tobot);
    if (i >= 0)
      botnet_send_filereject(i, path, to, par);
  }
}

/* filereq <sock:nick@bot> <bot:file> */
static void bot_filereq(int idx, char *tobot)
{
  char *from, *path;
  int i;

  context;
  from = newsplit(&tobot);
  if ((path = strchr(tobot, ':'))) {
    *path++ = 0;

    if (!strcasecmp(tobot, botnetnick)) {	/* for me! */
      /* process this */
      module_entry *fs = module_find("filesys", 0, 0);

      if (fs == NULL)
	botnet_send_priv(idx, botnetnick, from, NULL, MOD_NOFILESYSMOD);
      else {
	Function f = fs->funcs[FILESYS_REMOTE_REQ];

	f(idx, from, path);
      }
    } else {			/* pass it on */
      i = nextbot(tobot);
      if (i >= 0)
	botnet_send_filereq(i, from, tobot, path);
    }
  }
}

/* filesend <bot:path> <sock:nick@bot> <IP#> <port> <size> */
static void bot_filesend(int idx, char *par)
{
  char *botpath, *to, *tobot, *nick;
  int i;
  char *nfn;

  context;
  botpath = newsplit(&par);
  to = newsplit(&par);
  if ((tobot = strchr(to, '@'))) {
    *tobot = 0;
    tobot++;
  } else {
    tobot = to;
  }
  if (!strcasecmp(tobot, botnetnick)) {		/* for me! */
    nfn = strrchr(botpath, '/');
    if (nfn == NULL) {
      nfn = strrchr(botpath, ':');
      if (nfn == NULL)
	nfn = botpath;		/* that's odd. */
      else
	nfn++;
    } else
      nfn++;
    if ((nick = strchr(to, ':')))
      nick++;
    else
      nick = to;
    /* send it to 'nick' as if it's from me */
    dprintf(DP_SERVER, "PRIVMSG %s :\001DCC SEND %s %s\001\n", nick, nfn, par);
  } else {
    i = nextbot(tobot);
    if (i >= 0) {
      *--tobot = '@';
      botnet_send_filesend(i, botpath, to, par);
    }
  }
}

static void bot_error(int idx, char *par)
{
  context;
  putlog(LOG_MISC | LOG_BOTS, "*", "%s: %s", dcc[idx].nick, par);
}

/* nc <bot> <sock> <newnick> */
static void bot_nickchange(int idx, char *par)
{
  char *bot, *ssock, *newnick;
  int sock, i;

  context;
  if (bot_flags(dcc[idx].user) & BOT_ISOLATE)
    return;
  bot = newsplit(&par);
#ifndef NO_OLD_BOTNET
  if (b_numver(idx) < NEAT_BOTNET) {
    fake_alert(idx, "botversion", "NEAT_BOTNET");
    return;
  }
#endif
  i = nextbot(bot);
  if (i != idx) {
    fake_alert(idx, "direction", bot);
    return;
  }
  ssock = newsplit(&par);
  sock = base64_to_int(ssock);
  newnick = newsplit(&par);
  i = partynick(bot, sock, newnick);
  if (i < 0) {
    fake_alert(idx, "sock#", ssock);
    return;
  }
  chanout_but(-1, party[i].chan, "*** (%s) Nick change: %s -> %s\n",
	      bot, newnick, party[i].nick);
  botnet_send_nkch_part(idx, i, newnick);
}

/* join <bot> <nick> <chan> <flag><sock> <from> */
static void bot_join(int idx, char *par)
{
  char *bot, *nick, *x, *y;
  struct userrec *u;
  int i, sock, chan, i2, linking = 0;

  context;
  if (bot_flags(dcc[idx].user) & BOT_ISOLATE)
    return;
  bot = newsplit(&par);
#ifndef NO_OLD_BOTNET
  if (b_numver(idx) >= NEAT_BOTNET)
#endif
    if (bot[0] == '!') {
      linking = 1;
      bot++;
    }
  if (b_status(idx) & STAT_LINKING) {
    linking = 1;
  }
  nick = newsplit(&par);
  x = newsplit(&par);
#ifndef NO_OLD_BOTNET
  if (b_numver(idx) < NEAT_BOTNET)
    chan = atoi(x);
  else
#endif
    chan = base64_to_int(x);
  y = newsplit(&par);
  if ((chan < 0) || !y[0])
    return;			/* woops! pre 1.2.1's send .chat off'ers
				 * too!! */
  if (!y[0]) {
    y[0] = '-';
    sock = 0;
  } else {
#ifndef NO_OLD_BOTNET
    if (b_numver(idx) < NEAT_BOTNET)
      sock = atoi(y + 1);
    else
#endif
      sock = base64_to_int(y + 1);
  }
  /* 1.1 bots always send a sock#, even on a channel change
   * so if sock# is 0, this is from an old bot and we must tread softly
   * grab old sock# if there is one, otherwise make up one */
  if (sock == 0)
    sock = partysock(bot, nick);
  if (sock == 0)
    sock = fakesock++;
  i = nextbot(bot);
  if (i != idx) {		/* ok, garbage from a 1.0g (who uses that
				 * now?) OR raistlin being evil :) */
    fake_alert(idx, "direction", bot);
    return;
  }
  u = get_user_by_handle(userlist, nick);
  if (u) {
    sprintf(TBUF, "@%s", bot);
    touch_laston(u, TBUF, now);
  }
  i = addparty(bot, nick, chan, y[0], sock, par, &i2);
  context;
  botnet_send_join_party(idx, linking, i2, i);
  context;
  if (i != chan) {
    if (i >= 0) {
      if (b_numver(idx) >= NEAT_BOTNET)
	chanout_but(-1, i, "*** (%s) %s %s %s.\n", bot, nick, NET_LEFTTHE,
		    i ? "channel" : "party line");
      check_tcl_chpt(bot, nick, sock, i);
    }
    if ((b_numver(idx) >= NEAT_BOTNET) && !linking)
      chanout_but(-1, chan, "*** (%s) %s %s %s.\n", bot, nick, NET_JOINEDTHE,
		  chan ? "channel" : "party line");
    check_tcl_chjn(bot, nick, chan, y[0], sock, par);
  }
  context;
}

/* part <bot> <nick> <sock> [etc..] */
static void bot_part(int idx, char *par)
{
  char *bot, *nick, *etc;
  struct userrec *u;
  int sock, partyidx;
  int silent = 0;

  context;
  if (bot_flags(dcc[idx].user) & BOT_ISOLATE)
    return;
  bot = newsplit(&par);
  if (bot[0] == '!') {
    silent = 1;
    bot++;
  }
  nick = newsplit(&par);
  etc = newsplit(&par);
#ifndef NO_OLD_BOTNET
  if (b_numver(idx) < NEAT_BOTNET) {
    sock = atoi(etc);
    silent = 1;
  } else
#endif
    sock = base64_to_int(etc);
  if (sock == 0)
    sock = partysock(bot, nick);
  u = get_user_by_handle(userlist, nick);
  if (u) {
    sprintf(TBUF, "@%s", bot);
    touch_laston(u, TBUF, now);
  }
  if ((partyidx = getparty(bot, sock)) != -1) {
    check_tcl_chpt(bot, nick, sock, party[partyidx].chan);
    if ((b_numver(idx) >= NEAT_BOTNET) && !silent) {
      register int chan = party[partyidx].chan;

      if (par[0])
	chanout_but(-1, chan, "*** (%s) %s %s %s (%s).\n", bot, nick,
		    NET_LEFTTHE,
		    chan ? "channel" : "party line", par);
      else
	chanout_but(-1, chan, "*** (%s) %s %s %s.\n", bot, nick,
		    NET_LEFTTHE,
		    chan ? "channel" : "party line");
    }
    botnet_send_part_party(idx, partyidx, par, silent);
    remparty(bot, sock);
  }
  context;
}

/* away <bot> <sock> <message> */
/* null message = unaway */
static void bot_away(int idx, char *par)
{
  char *bot, *etc;
  int sock, partyidx, linking = 0;

  context;
  if (bot_flags(dcc[idx].user) & BOT_ISOLATE)
    return;
  bot = newsplit(&par);
#ifndef NO_OLD_BOTNET
  if (b_numver(idx) >= NEAT_BOTNET)
#endif
    if (bot[0] == '!') {
      linking = 1;
      bot++;
    }
  if (b_status(idx) & STAT_LINKING) {
    linking = 1;
  }
  etc = newsplit(&par);
#ifndef NO_OLD_BOTNET
  if (b_numver(idx) < NEAT_BOTNET)
    sock = atoi(etc);
  else
#endif
    sock = base64_to_int(etc);
  if (sock == 0)
    sock = partysock(bot, etc);
  check_tcl_away(bot, sock, par);
  if (par[0]) {
    partystat(bot, sock, PLSTAT_AWAY, 0);
    partyaway(bot, sock, par);
  } else {
    partystat(bot, sock, 0, PLSTAT_AWAY);
  }
  partyidx = getparty(bot, sock);
  if ((b_numver(idx) >= NEAT_BOTNET) && !linking) {
    if (par[0])
      chanout_but(-1, party[partyidx].chan,
		  "*** (%s) %s %s: %s.\n", bot,
		  party[partyidx].nick, NET_AWAY, par);
    else
      chanout_but(-1, party[partyidx].chan,
		  "*** (%s) %s %s.\n", bot,
		  party[partyidx].nick, NET_UNAWAY);
  }
  botnet_send_away(idx, bot, sock, par, linking);
}

/* (a courtesy info to help during connect bursts) */
/* idle <bot> <sock> <#secs> [away msg] */
static void bot_idle(int idx, char *par)
{
  char *bot, *work;
  int sock, idle;

  context;
  if (bot_flags(dcc[idx].user) & BOT_ISOLATE)
    return;
  bot = newsplit(&par);
  work = newsplit(&par);
#ifndef NO_OLD_BOTNET
  if (b_numver(idx) < NEAT_BOTNET)
    sock = atoi(work);
  else
#endif
    sock = base64_to_int(work);
  if (sock == 0)
    sock = partysock(bot, work);
  work = newsplit(&par);
#ifndef NO_OLD_BOTNET
  if (b_numver(idx) < NEAT_BOTNET)
    idle = atoi(work);
  else
#endif
    idle = base64_to_int(work);
  partysetidle(bot, sock, idle);
  if (par[0]) {
    partystat(bot, sock, PLSTAT_AWAY, 0);
    partyaway(bot, sock, par);
  }
  botnet_send_idle(idx, bot, sock, idle, par);
}

#ifndef NO_OLD_BOTNET

static void bot_ufno(int idx, char *par)
{
  context;
  putlog(LOG_BOTS, "*", "%s %s: %s", USERF_REJECTED, dcc[idx].nick, par);
  dcc[idx].status &= ~STAT_OFFERED;
  if (!(dcc[idx].status & STAT_GETTING))
    dcc[idx].status &= ~STAT_SHARE;
}

static void bot_old_userfile(int idx, char *par)
{
  context;
  putlog(LOG_BOTS, "*", "%s %s", USERF_OLDSHARE, dcc[idx].nick);
  dprintf(idx, "uf-no %s\n", USERF_ANTIQUESHARE);
}

#endif

void bot_share(int idx, char *par)
{
  context;
  sharein(idx, par);
}

/* v <frombot> <tobot> <idx:nick> */
static void bot_versions(int sock, char *par)
{
  char *frombot = newsplit(&par), *tobot, *from;
  module_entry *me;

  if (nextbot(frombot) != sock)
    fake_alert(sock, "versions-direction", frombot);
  else if (strcasecmp(tobot = newsplit(&par), botnetnick)) {
    if ((sock = nextbot(tobot)))
      dprintf(sock, "v %s %s %s\n", frombot, tobot, par);
  } else {
    from = newsplit(&par);
    botnet_send_priv(sock, botnetnick, from, frombot, "Modules loaded:\n");
    for (me = module_list; me; me = me->next)
      botnet_send_priv(sock, botnetnick, from, frombot, "  Module: %s (v%d.%d)\n",
		       me->name, me->major, me->minor);
    botnet_send_priv(sock, botnetnick, from, frombot, "End of module list.\n");
  }
}

/* BOT COMMANDS */
/* function call should be:
 * int bot_whatever(idx,"parameters");
 *
 * SORT these, dcc_bot uses a shortcut which requires them sorted
 * 
 * yup, those are tokens there to allow a more efficient botnet as
 * time goes on (death to slowly upgrading llama's)
 */
botcmd_t C_bot[] =
{
  {"a", (Function) bot_actchan},
#ifndef NO_OLD_BOTNET
  {"actchan", (Function) bot_actchan},
#endif
  {"aw", (Function) bot_away},
  {"away", (Function) bot_away},
  {"bye", (Function) bot_bye},
  {"c", (Function) bot_chan2},
#ifndef NO_OLD_BOTNET
  {"chan", (Function) bot_chan2},
  {"chat", (Function) bot_chat},
#endif
  {"ct", (Function) bot_chat},
  {"e", (Function) bot_error},
  {"el", (Function) bot_endlink},
#ifndef NO_OLD_BOTNET
  {"error", (Function) bot_error},
#endif
  {"f!", (Function) bot_filereject},
#ifndef NO_OLD_BOTNET
  {"filereject", (Function) bot_filereject},
  {"filereq", (Function) bot_filereq},
  {"filesend", (Function) bot_filesend},
#endif
  {"fr", (Function) bot_filereq},
  {"fs", (Function) bot_filesend},
  {"h", (Function) bot_handshake},
#ifndef NO_OLD_BOTNET
  {"handshake", (Function) bot_handshake},
#endif
  {"i", (Function) bot_idle},
  {"i?", (Function) bot_infoq},
#ifndef NO_OLD_BOTNET
  {"idle", (Function) bot_idle},
  {"info?", (Function) bot_infoq},
#endif
  {"j", (Function) bot_join},
#ifndef NO_OLD_BOTNET
  {"join", (Function) bot_join},
#endif
  {"l", (Function) bot_link},
#ifndef NO_OLD_BOTNET
  {"link", (Function) bot_link},
  {"linked", (Function) bot_linked},
#endif
  {"m", (Function) bot_motd},
#ifndef NO_OLD_BOTNET
  {"motd", (Function) bot_motd},
#endif
  {"n", (Function) bot_nlinked},
  {"nc", (Function) bot_nickchange},
#ifndef NO_OLD_BOTNET
  {"nlinked", (Function) bot_nlinked},
#endif
  {"p", (Function) bot_priv},
#ifndef NO_OLD_BOTNET
  {"part", (Function) bot_part},
#endif
  {"pi", (Function) bot_ping},
#ifndef NO_OLD_BOTNET
  {"ping", (Function) bot_ping},
#endif
  {"po", (Function) bot_pong},
#ifndef NO_OLD_BOTNET
  {"pong", (Function) bot_pong},
  {"priv", (Function) bot_priv},
#endif
  {"pt", (Function) bot_part},
  {"r", (Function) bot_reject},
#ifndef NO_OLD_BOTNET
  {"reject", (Function) bot_reject},
#endif
  {"s", (Function) bot_share},
  {"t", (Function) bot_trace},
  {"tb", (Function) bot_thisbot},
  {"td", (Function) bot_traced},
#ifndef NO_OLD_BOTNET
  {"thisbot", (Function) bot_thisbot},
  {"trace", (Function) bot_trace},
  {"traced", (Function) bot_traced},
#endif
  {"u", (Function) bot_update},
#ifndef NO_OLD_BOTNET
  {"uf-no", (Function) bot_ufno},
#endif
  {"ul", (Function) bot_unlink},
  {"un", (Function) bot_unlinked},
#ifndef NO_OLD_BOTNET
  {"unaway", (Function) bot_away},
  {"unlink", (Function) bot_unlink},
  {"unlinked", (Function) bot_unlinked},
  {"update", (Function) bot_update},
  {"userfile?", (Function) bot_old_userfile},
#endif
  {"v", (Function) bot_versions},
  {"w", (Function) bot_who},
#ifndef NO_OLD_BOTNET
  {"who", (Function) bot_who},
#endif
  {"z", (Function) bot_zapf},
#ifndef NO_OLD_BOTNET
  {"zapf", (Function) bot_zapf},
  {"zapf-broad", (Function) bot_zapfbroad},
#endif
  {"zb", (Function) bot_zapfbroad},
  {0, 0}
};