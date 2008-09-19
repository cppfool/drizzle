/* - mode: c; c-basic-offset: 2; indent-tabs-mode: nil; -*-
 *  vim:expandtab:shiftwidth=2:tabstop=2:smarttab:
 *
 *  Copyright (C) 2008 MySQL
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA  02110-1301  USA
 */

/* maintaince of drizzle databases */

#include "client_priv.h"
#include <signal.h>
#include <mysys/my_pthread.h>				/* because of signal()	*/
#include <sys/stat.h>

/* Added this for string translation. */
#include <libdrizzle/gettext.h>

#define ADMIN_VERSION "8.42"
#define SHUTDOWN_DEF_TIMEOUT 3600		/* Wait for shutdown */

char *host= NULL, *user= NULL, *opt_password= NULL;
static bool interrupted= false, opt_verbose= false,tty_password= false; 
static uint32_t tcp_port= 0, option_wait= 0, option_silent= 0;
static uint32_t my_end_arg;
static uint32_t opt_connect_timeout, opt_shutdown_timeout;
static myf error_flags; /* flags to pass to my_printf_error, like ME_BELL */

/*
  Forward declarations 
*/
static void usage(void);
static void print_version(void);
extern "C" sig_handler endprog(int signal_number);
extern "C" bool get_one_option(int optid, const struct my_option *opt,
                               char *argument);
static int execute_commands(DRIZZLE *drizzle,int argc, char **argv);
static bool sql_connect(DRIZZLE *drizzle, uint wait);

/*
  The order of commands must be the same as command_names,
  except ADMIN_ERROR
*/
enum commands {
  ADMIN_ERROR,
  ADMIN_SHUTDOWN,
  ADMIN_PING
};

static const char *command_names[]= {
  "shutdown",
  "ping",
  NullS
};

static TYPELIB command_typelib=
{ array_elements(command_names)-1,"commands", command_names, NULL };

static struct my_option my_long_options[] =
{
  {"help", '?', N_("Display this help and exit."), 0, 0, 0, GET_NO_ARG,
   NO_ARG, 0, 0, 0, 0, 0, 0},
  {"host", 'h', N_("Connect to host."), (char**) &host, (char**) &host, 0, GET_STR,
   REQUIRED_ARG, 0, 0, 0, 0, 0, 0},
  {"password", 'p',
   N_("Password to use when connecting to server. If password is not given it's asked from the tty."),
   0, 0, 0, GET_STR, OPT_ARG, 0, 0, 0, 0, 0, 0},
  {"port", 'P', N_("Port number to use for connection or 0 for default to, in "
   "order of preference, my.cnf, $DRIZZLE_TCP_PORT, "
   "built-in default (" STRINGIFY_ARG(DRIZZLE_PORT) ")."),
   (char**) &tcp_port,
   (char**) &tcp_port, 0, GET_UINT, REQUIRED_ARG, 0, 0, 0, 0, 0, 0},
  {"silent", 's', N_("Silently exit if one can't connect to server."),
   0, 0, 0, GET_NO_ARG, NO_ARG, 0, 0, 0, 0, 0, 0},
#ifndef DONT_ALLOW_USER_CHANGE
  {"user", 'u', N_("User for login if not current user."), (char**) &user,
   (char**) &user, 0, GET_STR_ALLOC, REQUIRED_ARG, 0, 0, 0, 0, 0, 0},
#endif
  {"verbose", 'v', N_("Write more information."), (char**) &opt_verbose,
   (char**) &opt_verbose, 0, GET_BOOL, NO_ARG, 0, 0, 0, 0, 0, 0},
  {"version", 'V', N_("Output version information and exit."), 0, 0, 0, GET_NO_ARG,
   NO_ARG, 0, 0, 0, 0, 0, 0},
  {"wait", 'w', N_("Wait and retry if connection is down."), 0, 0, 0, GET_UINT,
   OPT_ARG, 0, 0, 0, 0, 0, 0},
  {"connect_timeout", OPT_CONNECT_TIMEOUT, "", (char**) &opt_connect_timeout,
   (char**) &opt_connect_timeout, 0, GET_ULONG, REQUIRED_ARG, 3600*12, 0,
   3600*12, 0, 1, 0},
  {"shutdown_timeout", OPT_SHUTDOWN_TIMEOUT, "", (char**) &opt_shutdown_timeout,
   (char**) &opt_shutdown_timeout, 0, GET_ULONG, REQUIRED_ARG,
   SHUTDOWN_DEF_TIMEOUT, 0, 3600*12, 0, 1, 0},
  { 0, 0, 0, 0, 0, 0, GET_NO_ARG, NO_ARG, 0, 0, 0, 0, 0, 0}
};


static const char *load_default_groups[]= { "drizzleadmin","client",0 };

bool
get_one_option(int optid, const struct my_option *opt __attribute__((unused)),
               char *argument)
{
  int error = 0;

  switch(optid) {
  case 'p':
    if (argument)
    {
      char *start=argument;
      my_free(opt_password,MYF(MY_ALLOW_ZERO_PTR));
      opt_password= my_strdup(argument,MYF(MY_FAE));
      while (*argument) *argument++= 'x';   /* Destroy argument */
      if (*start)
        start[1]=0; /* Cut length of argument */
      tty_password= 0;
    }
    else 
      tty_password= 1; 
    break;
  case 's':
    option_silent++;
    break;
  case 'V':
    print_version();
    exit(0);
    break;
  case 'w':
    if (argument)
    {
      if ((option_wait=atoi(argument)) <= 0)
        option_wait=1;
    }
    else
      option_wait= ~(uint)0;
    break;
  case '?':
  case 'I':					/* Info */
    error++;
    break;
  }

  if (error)
  {
    usage();
    exit(1);
  }
  return 0;
}

int main(int argc,char *argv[])
{
  int error= 0, ho_error;
  DRIZZLE drizzle;
  char **commands, **save_argv;

  MY_INIT(argv[0]);
  drizzle_create(&drizzle);
  load_defaults("my",load_default_groups,&argc,&argv);
  save_argv = argv;				/* Save for free_defaults */
  if ((ho_error=handle_options(&argc, &argv, my_long_options, get_one_option)))
  {
    free_defaults(save_argv);
    exit(ho_error);
  }

  if (argc == 0)
  {
    usage();
    exit(1);
  }

  commands = argv;
  if (tty_password)
    opt_password = get_tty_password(NullS);

  VOID(signal(SIGINT,endprog));			/* Here if abort */
  VOID(signal(SIGTERM,endprog));		/* Here if abort */

  if (opt_connect_timeout)
  {
    uint tmp=opt_connect_timeout;
    drizzle_options(&drizzle,DRIZZLE_OPT_CONNECT_TIMEOUT, (char*) &tmp);
  }

  error_flags= (myf)0;

  if (sql_connect(&drizzle, option_wait))
  {
    unsigned int err= drizzle_errno(&drizzle);
    if (err >= CR_MIN_ERROR && err <= CR_MAX_ERROR)
      error= 1;
    else
    {
      /* Return 0 if all commands are PING */
      for (; argc > 0; argv++, argc--)
      {
        if (find_type(argv[0], &command_typelib, 2) != ADMIN_PING)
        {
          error= 1;
          break;
        }
      }
    }
  }
  else
  {
    error=execute_commands(&drizzle,argc,commands);
    drizzle_close(&drizzle);
  }
  my_free(opt_password,MYF(MY_ALLOW_ZERO_PTR));
  my_free(user,MYF(MY_ALLOW_ZERO_PTR));
  free_defaults(save_argv);
  my_end(my_end_arg);
  exit(error ? 1 : 0);
}

sig_handler endprog(int signal_number __attribute__((unused)))
{
  interrupted=1;
}

static bool sql_connect(DRIZZLE *drizzle, uint wait)
{
  bool info=0;

  for (;;)
  {
    if (drizzle_connect(drizzle,host,user,opt_password,NullS,tcp_port,NULL,0))
    {
      drizzle->reconnect= 1;
      if (info)
      {
        fputs("\n",stderr);
        (void) fflush(stderr);
      }
      return 0;
    }

    if (!wait)
    {
      if (!option_silent)
      {
        if (!host)
          host= (char*) LOCAL_HOST;

        my_printf_error(0,_("connect to server at '%s' failed\nerror: '%s'"),
        error_flags, host, drizzle_error(drizzle));

        if (drizzle_errno(drizzle) == CR_CONN_HOST_ERROR ||
          drizzle_errno(drizzle) == CR_UNKNOWN_HOST)
        {
          fprintf(stderr,_("Check that drizzled is running on %s"),host);
          fprintf(stderr,_(" and that the port is %d.\n"),
          tcp_port ? tcp_port: drizzle_get_default_port());
          fprintf(stderr,_("You can check this by doing 'telnet %s %d'\n"),
                  host, tcp_port ? tcp_port: drizzle_get_default_port());
        }
      }
      return 1;
    }
    if (wait != UINT32_MAX)
      wait--;				/* One less retry */
    if ((drizzle_errno(drizzle) != CR_CONN_HOST_ERROR) &&
        (drizzle_errno(drizzle) != CR_CONNECTION_ERROR))
    {
      fprintf(stderr,_("Got error: %s\n"), drizzle_error(drizzle));
    }
    else if (!option_silent)
    {
      if (!info)
      {
        info=1;
        fputs(_("Waiting for Drizzle server to answer"),stderr);
        (void) fflush(stderr);
      }
      else
      {
        putc('.',stderr);
        (void) fflush(stderr);
      }
    }
    sleep(5);
  }
}

/*
  Execute a command.
  Return 0 on ok
	 -1 on retryable error
	 1 on fatal error
*/
static int execute_commands(DRIZZLE *drizzle,int argc, char **argv)
{

  /*
    DRIZZLE documentation relies on the fact that drizzleadmin will
    execute commands in the order specified.
    If this behaviour is ever changed, Docs should be notified.
  */
  for (; argc > 0 ; argv++,argc--)
  {
    switch (find_type(argv[0],&command_typelib,2)) {
    case ADMIN_SHUTDOWN:
    {
      if (opt_verbose)
        printf(_("shutting down drizzled...\n"));

      if (drizzle_shutdown(drizzle, SHUTDOWN_DEFAULT))
      {
        my_printf_error(0, _("shutdown failed; error: '%s'"), error_flags,
                        drizzle_error(drizzle));
        return -1;
      }
      drizzle_close(drizzle);	/* Close connection to avoid error messages */

      if (opt_verbose)
        printf(_("done\n"));

      argc=1;             /* Force SHUTDOWN to be the last command */
      break;
    }
    case ADMIN_PING:
      drizzle->reconnect=0;	/* We want to know of reconnects */
      if (!drizzle_ping(drizzle))
      {
        if (option_silent < 2)
          puts(_("drizzled is alive"));
      }
      else
      {
        if (drizzle_errno(drizzle) == CR_SERVER_GONE_ERROR)
        {
          drizzle->reconnect=1;
          if (!drizzle_ping(drizzle))
            puts(_("connection was down, but drizzled is now alive"));
        }
        else
	      {
          my_printf_error(0,_("drizzled doesn't answer to ping, error: '%s'"),
          error_flags, drizzle_error(drizzle));
          return -1;
        }
      }
      drizzle->reconnect=1;	/* Automatic reconnect is default */
      break;

    default:
      my_printf_error(0, _("Unknown command: '%-.60s'"), error_flags, argv[0]);
      return 1;
    }
  }
  return 0;
}

static void print_version(void)
{
  printf(_("%s  Ver %s Distrib %s, for %s on %s\n"),my_progname,ADMIN_VERSION,
	 drizzle_get_client_info(),SYSTEM_TYPE,MACHINE_TYPE);
}

static void usage(void)
{
  print_version();
  puts(_("Copyright (C) 2000-2008 MySQL AB"));
  puts(_("This software comes with ABSOLUTELY NO WARRANTY. This is free software,\nand you are welcome to modify and redistribute it under the GPL license\n"));
  puts(_("Administration program for the drizzled daemon."));
  printf(_("Usage: %s [OPTIONS] command command....\n"), my_progname);
  my_print_help(my_long_options);
  puts(_("\
  ping         Check if server is down\n\
  shutdown     Take server down\n"));
}
