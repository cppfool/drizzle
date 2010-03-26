/* - mode: c; c-basic-offset: 2; indent-tabs-mode: nil; -*-
 *  vim:expandtab:shiftwidth=2:tabstop=2:smarttab:
 *
 *  Copyright (C) 2008-2009 Sun Microsystems
 *  Copyright (c) 2010 Jay Pipes <jaypipes@gmail.com>
 *
 *  Authors:
 *
 *  Jay Pipes <jaypipes@gmail.com.com>
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

/**
 * @file
 *
 * Transaction log module initialization and plugin
 * registration.
 */

#include "config.h"

#include "transaction_log.h"
#include "transaction_log_applier.h"
#include "transaction_log_index.h"
#include "data_dictionary_schema.h"
#include "print_transaction_message.h"
#include "hexdump_transaction_message.h"
#include "background_worker.h"

#include <drizzled/plugin/plugin.h>
#include <drizzled/session.h>
#include <drizzled/set_var.h>
#include <drizzled/gettext.h>

using namespace std;
using namespace drizzled;

/** 
 * Transaction Log plugin system variable - Is the log enabled? Only used on init().  
 * The enable() and disable() methods of the TransactionLog class control online
 * disabling.
 */
static bool sysvar_transaction_log_enabled= false;
/** Transaction Log plugin system variable - The path to the log file used */
static char* sysvar_transaction_log_file= NULL;
/** 
 * Transaction Log plugin system variable - A debugging variable to assist 
 * in truncating the log file. 
 */
static bool sysvar_transaction_log_truncate_debug= false;
static const char DEFAULT_LOG_FILE_PATH[]= "transaction.log"; /* In datadir... */
/** 
 * Transaction Log plugin system variable - Should we write a CRC32 checksum for 
 * each written Transaction message?
 */
static bool sysvar_transaction_log_checksum_enabled= false;
/**
 * Numeric option controlling the sync/flush behaviour of the transaction
 * log.  Options are:
 *
 * TransactionLog::SYNC_METHOD_OS == 0            ... let OS do sync'ing
 * TransactionLog::SYNC_METHOD_EVERY_WRITE == 1   ... sync on every write
 * TransactionLog::SYNC_METHOD_EVERY_SECOND == 2  ... sync at most once a second
 */
static uint32_t sysvar_transaction_log_sync_method= 0;

/** DATA_DICTIONARY views */
static TransactionLogTool *transaction_log_tool;
static TransactionLogEntriesTool *transaction_log_entries_tool;
static TransactionLogTransactionsTool *transaction_log_transactions_tool;

/** Index defined in transaction_log_index.cc */
extern TransactionLogIndex *transaction_log_index;
/** Transaction Log descriptor defined in transaction_log.cc */
extern TransactionLog *transaction_log;
/** Transaction Log descriptor defined in transaction_log.cc */
extern TransactionLogApplier *transaction_log_applier;

/** Defined in print_transaction_message.cc */
extern plugin::Create_function<PrintTransactionMessageFunction> *print_transaction_message_func_factory;
extern plugin::Create_function<HexdumpTransactionMessageFunction> *hexdump_transaction_message_func_factory;

static int init(drizzled::plugin::Context &context)
{
  /* Create and initialize the transaction log itself */
  if (sysvar_transaction_log_enabled)
  {
    transaction_log= new (nothrow) TransactionLog(string(sysvar_transaction_log_file),
                                                  sysvar_transaction_log_sync_method);

    if (transaction_log == NULL)
    {
      errmsg_printf(ERRMSG_LVL_ERROR, _("Failed to allocate the TransactionLog instance.  Got error: %s\n"), 
                    strerror(errno));
      return 1;
    }
    else
    {
      /* Check to see if the log was not created properly */
      if (transaction_log->hasError())
      {
        errmsg_printf(ERRMSG_LVL_ERROR, _("Failed to initialize the Transaction Log.  Got error: %s\n"), 
                      transaction_log->getErrorMessage().c_str());
        return 1;
      }
    }
    /* Create the applier plugin and register it */
    transaction_log_applier= new (nothrow) TransactionLogApplier("transaction_log_applier",
                                                                 *transaction_log, 
                                                                 sysvar_transaction_log_checksum_enabled);
    if (transaction_log_applier == NULL)
    {
      errmsg_printf(ERRMSG_LVL_ERROR, _("Failed to allocate the TransactionLogApplier instance.  Got error: %s\n"), 
                    strerror(errno));
      return 1;
    }
    context.add(transaction_log_applier);

    /* Setup DATA_DICTIONARY views */

    transaction_log_tool= new (nothrow) TransactionLogTool;
    context.add(transaction_log_tool);
    transaction_log_entries_tool= new (nothrow) TransactionLogEntriesTool;
    context.add(transaction_log_entries_tool);
    transaction_log_transactions_tool= new (nothrow) TransactionLogTransactionsTool;
    context.add(transaction_log_transactions_tool);

    /* Setup the module's UDFs */
    print_transaction_message_func_factory=
      new plugin::Create_function<PrintTransactionMessageFunction>("print_transaction_message");
    context.add(print_transaction_message_func_factory);

    hexdump_transaction_message_func_factory=
      new plugin::Create_function<HexdumpTransactionMessageFunction>("hexdump_transaction_message");
    context.add(hexdump_transaction_message_func_factory);

    /* Create and initialize the transaction log index */
    transaction_log_index= new (nothrow) TransactionLogIndex(*transaction_log);
    if (transaction_log_index == NULL)
    {
      errmsg_printf(ERRMSG_LVL_ERROR, _("Failed to allocate the TransactionLogIndex instance.  Got error: %s\n"), 
                    strerror(errno));
      return 1;
    }
    else
    {
      /* Check to see if the index was not created properly */
      if (transaction_log_index->hasError())
      {
        errmsg_printf(ERRMSG_LVL_ERROR, _("Failed to initialize the Transaction Log Index.  Got error: %s\n"), 
                      transaction_log_index->getErrorMessage().c_str());
        return 1;
      }
    }

    /* 
     * Setup the background worker thread which maintains
     * summary information about the transaction log.
     */
    if (initTransactionLogBackgroundWorker())
      return 1; /* Error message output handled in function above */
  }
  return 0;
}


static void set_truncate_debug(Session *,
                               drizzle_sys_var *, 
                               void *, 
                               const void *save)
{
  /* 
   * The const void * save comes directly from the check function, 
   * which should simply return the result from the set statement. 
   */
  if (transaction_log)
  {
    if (*(bool *)save != false)
    {
      transaction_log->truncate();
      transaction_log_index->clear();
    }
  }
}

static DRIZZLE_SYSVAR_BOOL(enable,
                           sysvar_transaction_log_enabled,
                           PLUGIN_VAR_NOCMDARG,
                           N_("Enable transaction log"),
                           NULL, /* check func */
                           NULL, /* update func */
                           false /* default */);

static DRIZZLE_SYSVAR_BOOL(truncate_debug,
                           sysvar_transaction_log_truncate_debug,
                           PLUGIN_VAR_NOCMDARG,
                           N_("DEBUGGING - Truncate transaction log"),
                           NULL, /* check func */
                           set_truncate_debug, /* update func */
                           false /* default */);

static DRIZZLE_SYSVAR_STR(log_file,
                          sysvar_transaction_log_file,
                          PLUGIN_VAR_READONLY,
                          N_("Path to the file to use for transaction log"),
                          NULL, /* check func */
                          NULL, /* update func*/
                          DEFAULT_LOG_FILE_PATH /* default */);

static DRIZZLE_SYSVAR_BOOL(enable_checksum,
                           sysvar_transaction_log_checksum_enabled,
                           PLUGIN_VAR_NOCMDARG,
                           N_("Enable CRC32 Checksumming of each written transaction log entry"),
                           NULL, /* check func */
                           NULL, /* update func */
                           false /* default */);

static DRIZZLE_SYSVAR_UINT(sync_method,
                           sysvar_transaction_log_sync_method,
                           PLUGIN_VAR_OPCMDARG,
                           N_("0 == rely on operating system to sync log file (default), "
                              "1 == sync file at each transaction write, "
                              "2 == sync log file once per second"),
                           NULL, /* check func */
                           NULL, /* update func */
                           0, /* default */
                           0,
                           2,
                           0);

static drizzle_sys_var* sys_variables[]= {
  DRIZZLE_SYSVAR(enable),
  DRIZZLE_SYSVAR(truncate_debug),
  DRIZZLE_SYSVAR(log_file),
  DRIZZLE_SYSVAR(enable_checksum),
  DRIZZLE_SYSVAR(sync_method),
  NULL
};

DRIZZLE_PLUGIN(init, sys_variables);