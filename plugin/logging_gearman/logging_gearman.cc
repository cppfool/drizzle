/* -*- mode: c++; c-basic-offset: 2; indent-tabs-mode: nil; -*-
 *  vim:expandtab:shiftwidth=2:tabstop=2:smarttab:
 *
 *  Copyright (C) 2008, 2009 Sun Microsystems, Inc.
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; version 2 of the License.
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

#include <config.h>

#include <boost/scoped_array.hpp>

#include <drizzled/item.h>
#include <drizzled/plugin.h>
#include <drizzled/plugin/logging.h>
#include <drizzled/gettext.h>
#include <drizzled/session.h>
#include <drizzled/session/times.h>
#include <drizzled/sql_parse.h>
#include <drizzled/errmsg_print.h>
#include <boost/date_time.hpp>
#include <boost/program_options.hpp>
#include <drizzled/module/option_map.h>
#include <libgearman/gearman.h>
#include <limits.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <cstdio>
#include <cerrno>
#include <memory>

using namespace drizzled;

namespace drizzle_plugin {
namespace logging_gearman {

namespace po= boost::program_options;

bool updateHost(Session *, set_var*);
bool updateFunction(Session *, set_var*);
/* TODO make this dynamic as needed */
static const int MAX_MSG_LEN= 32*1024;

/* quote a string to be safe to include in a CSV line
   that means backslash quoting all commas, doublequotes, backslashes,
   and all the ASCII unprintable characters
   as long as we pass the high-bit bytes unchanged
   this is safe to do to a UTF8 string
   we dont allow overrunning the targetbuffer
   to avoid having a very long query overwrite memory

   TODO consider remapping the unprintables instead to "Printable
   Representation", the Unicode characters from the area U+2400 to
   U+2421 reserved for representing control characters when it is
   necessary to print or display them rather than have them perform
   their intended function.

*/
static unsigned char *quotify (const unsigned char *src, size_t srclen,
                               unsigned char *dst, size_t dstlen)
{
  static const char hexit[]= { '0', '1', '2', '3', '4', '5', '6', '7',
                               '8', '9', 'a', 'b', 'c', 'd', 'e', 'f' };
  size_t dst_ndx;  /* ndx down the dst */
  size_t src_ndx;  /* ndx down the src */

  assert(dst);
  assert(dstlen > 0);

  for (dst_ndx= 0,src_ndx= 0; src_ndx < srclen; src_ndx++)
    {

      /* Worst case, need 5 dst bytes for the next src byte.
         backslash x hexit hexit null
         so if not enough room, just terminate the string and return
      */
      if ((dstlen - dst_ndx) < 5)
        {
          dst[dst_ndx]= (unsigned char)0x00;
          return dst;
        }

      if (src[src_ndx] > 0x7f)
        {
          // pass thru high bit characters, they are non-ASCII UTF8 Unicode
          dst[dst_ndx++]= src[src_ndx];
        }
      else if (src[src_ndx] == 0x00)  // null
        {
          dst[dst_ndx++]= 0x5C; dst[dst_ndx++]= (unsigned char) '0';
        }
      else if (src[src_ndx] == 0x07)  // bell
        {
          dst[dst_ndx++]= 0x5C; dst[dst_ndx++]= (unsigned char) 'a';
        }
      else if (src[src_ndx] == 0x08)  // backspace
        {
          dst[dst_ndx++]= 0x5C; dst[dst_ndx++]= (unsigned char) 'b';
        }
      else if (src[src_ndx] == 0x09)  // horiz tab
        {
          dst[dst_ndx++]= 0x5C; dst[dst_ndx++]= (unsigned char) 't';
        }
      else if (src[src_ndx] == 0x0a)  // line feed
        {
          dst[dst_ndx++]= 0x5C; dst[dst_ndx++]= (unsigned char) 'n';
        }
      else if (src[src_ndx] == 0x0b)  // vert tab
        {
          dst[dst_ndx++]= 0x5C; dst[dst_ndx++]= (unsigned char) 'v';
        }
      else if (src[src_ndx] == 0x0c)  // formfeed
        {
          dst[dst_ndx++]= 0x5C; dst[dst_ndx++]= (unsigned char) 'f';
        }
      else if (src[src_ndx] == 0x0d)  // carrage return
        {
          dst[dst_ndx++]= 0x5C; dst[dst_ndx++]= (unsigned char) 'r';
        }
      else if (src[src_ndx] == 0x1b)  // escape
        {
          dst[dst_ndx++]= 0x5C; dst[dst_ndx++]= (unsigned char) 'e';
        }
      else if (src[src_ndx] == 0x22)  // quotation mark
        {
          dst[dst_ndx++]= 0x5C; dst[dst_ndx++]= 0x22;
        }
      else if (src[src_ndx] == 0x2C)  // comma
        {
          dst[dst_ndx++]= 0x5C; dst[dst_ndx++]= 0x2C;
        }
      else if (src[src_ndx] == 0x5C)  // backslash
        {
          dst[dst_ndx++]= 0x5C; dst[dst_ndx++]= 0x5C;
        }
      else if ((src[src_ndx] < 0x20) || (src[src_ndx] == 0x7F))  // other unprintable ASCII
        {
          dst[dst_ndx++]= 0x5C;
          dst[dst_ndx++]= (unsigned char) 'x';
          dst[dst_ndx++]= hexit[(src[src_ndx] >> 4) & 0x0f];
          dst[dst_ndx++]= hexit[src[src_ndx] & 0x0f];
        }
      else  // everything else
        {
          dst[dst_ndx++]= src[src_ndx];
        }
      dst[dst_ndx]= '\0';
    }
  return dst;
}

class LoggingGearman :
  public drizzled::plugin::Logging
{

  std::string sysvar_host;
  std::string sysvar_function;

  int _gearman_client_ok;
  gearman_client_st _gearman_client;

  LoggingGearman();
  LoggingGearman(const LoggingGearman&);

public:

  LoggingGearman(const std::string &host,
                 const std::string &function) :
    drizzled::plugin::Logging("gearman_query_log"),
    sysvar_host(host),
    sysvar_function(function),
    _gearman_client_ok(0),
    _gearman_client()
  {
    gearman_return_t ret;


    if (gearman_client_create(&_gearman_client) == NULL)
    {
      drizzled::sql_perror(_("fail gearman_client_create()"));
      return;
    }

    /* TODO, be able to override the port */
    /* TODO, be able send to multiple servers */
    ret= gearman_client_add_server(&_gearman_client,
                                   host.c_str(), 0);
    if (ret != GEARMAN_SUCCESS)
    {
      drizzled::errmsg_printf(drizzled::error::ERROR, _("fail gearman_client_add_server(): %s"),
                              gearman_client_error(&_gearman_client));
      return;
    }

    _gearman_client_ok= 1;

  }

  ~LoggingGearman()
  {
    if (_gearman_client_ok)
    {
      gearman_client_free(&_gearman_client);
    }
  }

  virtual bool post(drizzled::Session *session)
  {
    boost::scoped_array<char> msgbuf(new char[MAX_MSG_LEN]);
    int msgbuf_len= 0;
  
    assert(session != NULL);

    /* in theory, we should return "true", meaning that the plugin isn't happy,
       but that crashes the server, so for now, we just lie a little bit
    */

    if (not _gearman_client_ok)
        return false;
  
    /* 
      TODO, the session object should have a "utime command completed"
      inside itself, so be more accurate, and so this doesnt have to
      keep calling current_utime, which can be slow.
    */
    uint64_t t_mark= session->times.getCurrentTimestamp(false);
  

    // buffer to quotify the query
    unsigned char qs[255];
  
    // to avoid trying to printf %s something that is potentially NULL
    drizzled::util::string::ptr dbs(session->schema());
  
    msgbuf_len=
      snprintf(msgbuf.get(), MAX_MSG_LEN,
               "%"PRIu64",%"PRIu64",%"PRIu64",\"%.*s\",\"%s\",\"%.*s\","
               "%"PRIu64",%"PRIu64",%"PRIu64",%"PRIu64",%"PRIu64","
               "%"PRIu32",%"PRIu32",%"PRIu32",\"%s\"",
               t_mark,
               session->thread_id,
               session->getQueryId(),
               // dont need to quote the db name, always CSV safe
               (int)dbs->size(), dbs->c_str(),
               // do need to quote the query
               quotify((const unsigned char *)session->getQueryString()->c_str(), session->getQueryString()->length(), qs, sizeof(qs)),
               // getCommandName is defined in drizzled/sql_parse.h dont
               // need to quote the command name, always CSV safe
               (int)drizzled::getCommandName(session->command).size(),
               drizzled::getCommandName(session->command).c_str(),
               // counters are at end, to make it easier to add more
               (t_mark - session->times.getConnectMicroseconds()),
               (session->times.getElapsedTime()),
               (t_mark - session->times.utime_after_lock),
               session->sent_row_count,
               session->examined_row_count,
               session->tmp_table,
               session->total_warn_count,
               session->getServerId(),
               drizzled::getServerHostname().c_str()
               );
  
    char job_handle[GEARMAN_JOB_HANDLE_SIZE];
  
    (void) gearman_client_do_background(&_gearman_client,
                                        sysvar_function.c_str(),
                                        NULL,
                                        (void *) msgbuf.get(),
                                        (size_t) msgbuf_len,
                                        job_handle);
  
    return false;
  }
  
  /**
   * This function changes the current gearman host to the parameter passed to the function.
   *
   * @return True on success, False on error.
   */
  bool setHost(std::string &new_host)
  {
    gearman_return_t tmp_ret;
    
    /*
      New server is added to the list of servers using gearman_client_add_server.
      If the call does not result in error, then all the servers are removed from the list and
      new server only is added. This is done to ensure that a bad server url does not replace
      the existing server url.
      TODO Create a new instance of gearman_client_st and add the new server in it. If success, release the
      old gearman_client_st and use new instance of gearman_client_st everywhere.
    */
    tmp_ret= gearman_client_add_server(&_gearman_client,
                                   new_host.c_str(), 0);
    if (tmp_ret != GEARMAN_SUCCESS)
    {
      drizzled::errmsg_printf(drizzled::error::ERROR, _("fail gearman_client_add_server(): %s"),
                              gearman_client_error(&_gearman_client));
      return false;
    }

    gearman_client_remove_servers(&_gearman_client);
    gearman_client_add_server(&_gearman_client, new_host.c_str(), 0);
    sysvar_host= new_host;
    return true;
  }

  /**
   * This function changes the gearman function with the new one.
   *
   * @return True on success (as we dont have to do anything except updating the function string, this always return true.)
   */
  bool setFunction(std::string &new_function)
  {
    sysvar_function= new_function;
    return true;
  }
  
  /**
   * Getter for host
   *
   * @return sysvar_host
   */
  std::string& getHost()
  {
    return sysvar_host;
  }
  
  /**
   * Getter for function
   *
   * @return sysvar_function
   */
  std::string& getFunction()
  {
    return sysvar_function;
  }
};

static LoggingGearman *handler= NULL;

/**
 * This function is called when the value of gearman host is updated dynamically from the drizzle server
 *
 * @return False on success, True on error
 */
bool updateHost(Session *, set_var* var)
{
  if (not var->value->str_value.empty())
  {
    std::string newHost(var->value->str_value.data());
    if (handler->setHost(newHost))
      return false; //success
    else
      return true; // error
  }
  errmsg_printf(error::ERROR, _("logging_gearman_host cannot be NULL"));
  return true; // error
}

/**
 * This function is called when the value of gearman function is updated dynamically
 *
 * @return False on error, True on success
 */
bool updateFunction(Session *, set_var* var)
{
  if (not var->value->str_value.empty())
  {
    std::string newFunction(var->value->str_value.data());
    if (handler->setFunction(newFunction))
      return false; //success
    else
      return true; // error
  }
  errmsg_printf(error::ERROR, _("logging_gearman_function cannot be NULL"));
  return true; // error
}


static int init(drizzled::module::Context &context)
{
  const drizzled::module::option_map &vm= context.getOptions();

  handler= new LoggingGearman(vm["host"].as<std::string>(),
                              vm["function"].as<std::string>());
  context.add(handler);
  context.registerVariable(new sys_var_std_string("host", handler->getHost(), NULL, &updateHost));
  context.registerVariable(new sys_var_std_string("function", handler->getFunction(), NULL, &updateFunction));

  return 0;
}

static void init_options(drizzled::module::option_context &context)
{
  context("host",
          po::value<std::string>()->default_value("localhost"),
          _("Hostname for logging to a Gearman server"));
  context("function",
          po::value<std::string>()->default_value("drizzlelog"),
          _("Gearman Function to send logging to"));
}

} /* namespace logging_gearman */
} /* namespace drizzle_plugin */

DRIZZLE_DECLARE_PLUGIN
{
  DRIZZLE_VERSION_ID,
  "logging_gearman",
  "0.1",
  "Mark Atwood",
  N_("Logs queries to a Gearman server"),
  drizzled::PLUGIN_LICENSE_GPL,
  drizzle_plugin::logging_gearman::init,
  NULL,
  drizzle_plugin::logging_gearman::init_options
}
DRIZZLE_DECLARE_PLUGIN_END;
