/* -*- mode: c++; c-basic-offset: 2; indent-tabs-mode: nil; -*-
 *  vim:expandtab:shiftwidth=2:tabstop=2:smarttab:
 *
 *  Copyright (C) 2008 Sun Microsystems
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


#ifndef DRIZZLED_OPEN_TABLES_STATE_H
#define DRIZZLED_OPEN_TABLES_STATE_H

#include "drizzled/lock.h"

namespace drizzled
{

/**
  Class that holds information about tables which were opened and locked
  by the thread. It is also used to save/restore this information in
  push_open_tables_state()/pop_open_tables_state().
*/

class Open_tables_state
{
public:
  /**
    List of regular tables in use by this thread. Contains temporary and
    base tables that were opened with @see open_tables().
  */
  Table *open_tables;

  /**
    List of temporary tables used by this thread. Contains user-level
    temporary tables, created with CREATE TEMPORARY TABLE, and
    internal temporary tables, created, e.g., to resolve a SELECT,
    or for an intermediate table used in ALTER.
    XXX Why are internal temporary tables added to this list?
  */
  Table *temporary_tables;

  /**
    Mark all temporary tables which were used by the current statement or
    substatement as free for reuse, but only if the query_id can be cleared.

    @param session thread context

    @remark For temp tables associated with a open SQL HANDLER the query_id
            is not reset until the HANDLER is closed.
  */
  void mark_temp_tables_as_free_for_reuse();

protected:
  void close_temporary_tables();
public:
  void close_temporary_table(Table *table);
  // The method below just handles the de-allocation of the table. In
  // a better memory type world, this would not be needed.
private:
  void nukeTable(Table *table);
public:

  /* Work with temporary tables */
  Table *find_temporary_table(const TableIdentifier &identifier);

  void dumpTemporaryTableNames(const char *id);
  int drop_temporary_table(const drizzled::TableIdentifier &identifier);
  bool rm_temporary_table(plugin::StorageEngine *base, TableIdentifier &identifier);
  bool rm_temporary_table(TableIdentifier &identifier, bool best_effort= false);
  Table *open_temporary_table(TableIdentifier &identifier,
                              bool link_in_list= true);

  virtual query_id_t getQueryId()  const= 0;

  Table *derived_tables;
  /*
    During a MySQL session, one can lock tables in two modes: automatic
    or manual. In automatic mode all necessary tables are locked just before
    statement execution, and all acquired locks are stored in 'lock'
    member. Unlocking takes place automatically as well, when the
    statement ends.
    Manual mode comes into play when a user issues a 'LOCK TABLES'
    statement. In this mode the user can only use the locked tables.
    Trying to use any other tables will give an error. The locked tables are
    stored in 'locked_tables' member.  Manual locking is described in
    the 'LOCK_TABLES' chapter of the MySQL manual.
    See also lock_tables() for details.
  */
  DrizzleLock *lock;

  /*
    CREATE-SELECT keeps an extra lock for the table being
    created. This field is used to keep the extra lock available for
    lower level routines, which would otherwise miss that lock.
   */
  DrizzleLock *extra_lock;

  uint64_t version;
  uint32_t current_tablenr;

  /*
    This constructor serves for creation of Open_tables_state instances
    which are used as backup storage.
  */
  Open_tables_state() :
    open_tables(0),
    temporary_tables(0),
    derived_tables(0),
    lock(0),
    extra_lock(0),
    version(0),
    current_tablenr(0)
  { }
  virtual ~Open_tables_state() {}

  Open_tables_state(uint64_t version_arg);
};

} /* namespace drizzled */

#endif /* DRIZZLED_OPEN_TABLES_STATE_H */
