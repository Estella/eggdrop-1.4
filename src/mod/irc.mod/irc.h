#define check_tcl_join(a,b,c,d) check_tcl_joinpart(a,b,c,d,H_join)
#define check_tcl_part(a,b,c,d) check_tcl_joinpart(a,b,c,d,H_part)
#define check_tcl_splt(a,b,c,d) check_tcl_joinpart(a,b,c,d,H_splt)
#define check_tcl_rejn(a,b,c,d) check_tcl_joinpart(a,b,c,d,H_rejn)
#define check_tcl_sign(a,b,c,d,e) check_tcl_signtopcnick(a,b,c,d,e,H_sign)
#define check_tcl_topc(a,b,c,d,e) check_tcl_signtopcnick(a,b,c,d,e,H_topc)
#define check_tcl_nick(a,b,c,d,e) check_tcl_signtopcnick(a,b,c,d,e,H_nick)
#define check_tcl_mode(a,b,c,d,e,f) check_tcl_kickmode(a,b,c,d,e,f,H_mode)
#define check_tcl_kick(a,b,c,d,e,f) check_tcl_kickmode(a,b,c,d,e,f,H_kick)

#ifdef MAKING_IRC
static void check_tcl_kickmode(char *, char *, struct userrec *, char *, char *, char *,
			       p_tcl_bind_list);
static void check_tcl_joinpart(char *, char *, struct userrec *, char *, p_tcl_bind_list);
static void check_tcl_signtopcnick(char *, char *, struct userrec *u, char *,
				   char *, p_tcl_bind_list);
static void check_tcl_pubm(char *, char *, char *, char *);
static int check_tcl_pub(char *, char *, char *, char *);
static int me_op(struct chanset_t *);
static int any_ops(struct chanset_t *);
static int hand_on_chan(struct chanset_t *, struct userrec *);
static char *getchanmode(struct chanset_t *);
static void flush_mode(struct chanset_t *, int);
static void resetbans(struct chanset_t *);
static void resetexempts(struct chanset_t *);
static void resetinvites(struct chanset_t *);
static void reset_chan_info(struct chanset_t *);
static void recheck_channel(struct chanset_t *, int);
static void set_key(struct chanset_t *, char *);
static void take_revenge(struct chanset_t *, char *, char *);
static int detect_chan_flood(char *, char *, char *, struct chanset_t *, int, char *);
static void newban(struct chanset_t *, char *, char *);
static void newexempt(struct chanset_t *, char *, char *);
static void newinvite(struct chanset_t *, char *, char *);
static char *quickban(struct chanset_t *, char *);
static void got_op(struct chanset_t *chan, char *nick, char *from,
		   char *who, struct flag_record *opper);
static int killmember(struct chanset_t *chan, char *nick);
static void check_lonely_channel(struct chanset_t *chan);
static void gotmode(char *, char *);

#else
/* 4 - 7 */
#define H_splt (*(p_tcl_bind_list*)(irc_funcs[4]))
#define H_rejn (*(p_tcl_bind_list*)(irc_funcs[5]))
#define H_nick (*(p_tcl_bind_list*)(irc_funcs[6]))
#define H_sign (*(p_tcl_bind_list*)(irc_funcs[7]))
/* 8 - 11 */
#define H_join (*(p_tcl_bind_list*)(irc_funcs[8]))
#define H_part (*(p_tcl_bind_list*)(irc_funcs[9]))
#define H_mode (*(p_tcl_bind_list*)(irc_funcs[10]))
#define H_kick (*(p_tcl_bind_list*)(irc_funcs[11]))
/* 12 - 15 */
#define H_pubm (*(p_tcl_bind_list*)(irc_funcs[12]))
#define H_pub (*(p_tcl_bind_list*)(irc_funcs[13]))
#define H_topc (*(p_tcl_bind_list*)(irc_funcs[14]))
/* recheck_channel is here */
/* 16 - 19 */
#define me_op ((int(*)(irc_funcs[16]))(struct chanset_t *))
#endif
