# action.fix.tcl
#
# Tothwolf  25May1999: cleanup
# Tothwolf  04Oct1999: changed proc names slightly
#
# $Id: action.fix.tcl,v 1.4 1999/12/15 02:32:57 guppy Exp $

# fix for mirc dcc chat /me's
bind filt - "\001ACTION *\001" filt:dcc_action
proc filt:dcc_action {idx text} {
  return ".me [string trim [join [lrange [split $text] 1 end]] \001]"
}

# fix for telnet session /me's
bind filt - "/me *" filt:telnet_action
proc filt:telnet_action {idx text} {
  return ".me [join [lrange [split $text] 1 end]]"
}
