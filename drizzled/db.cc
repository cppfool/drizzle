/* Copyright (C) 2000-2003 MySQL AB

   This program is free software; you can redistribute it and/or modify
   it under the terms of the GNU General Public License as published by
   the Free Software Foundation; version 2 of the License.

   This program is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
   GNU General Public License for more details.

   You should have received a copy of the GNU General Public License
   along with this program; if not, write to the Free Software
   Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA */


/* create and drop of databases */
#include <config.h>
#include CSTDINT_H
#include CINTTYPES_H
#include <string>
#include <fstream>
#include <drizzled/serialize/serialize.h>
using namespace std;
#include <drizzled/server_includes.h>
#include <mysys/mysys_err.h>
#include <mysys/my_dir.h>
#include "log.h"
#include <drizzled/error.h>
#include <drizzled/gettext.h>
#include <mysys/hash.h>
#include <drizzled/session.h>
#include <drizzled/db.h>
#include <drizzled/sql_base.h>

#define MAX_DROP_TABLE_Q_LEN      1024

const char *del_exts[]= {".frm", ".BAK", ".TMD",".opt", NULL};
static TYPELIB deletable_extentions=
{array_elements(del_exts)-1,"del_exts", del_exts, NULL};

static long mysql_rm_known_files(Session *session, MY_DIR *dirp,
                                 const char *db, const char *path,
                                 uint32_t level,
                                 TableList **dropped_tables);

static bool rm_dir_w_symlink(const char *org_path, bool send_error);
static void mysql_change_db_impl(Session *session,
                                 LEX_STRING *new_db_name,
                                 const CHARSET_INFO * const new_db_charset);


/* Database lock hash */
HASH lock_db_cache;
pthread_mutex_t LOCK_lock_db;
int creating_database= 0;  // how many database locks are made


/* Structure for database lock */
typedef struct my_dblock_st
{
  char *name;        /* Database name        */
  uint32_t name_length;  /* Database length name */
} my_dblock_t;


/*
  lock_db key.
*/

extern "C" unsigned char* lock_db_get_key(my_dblock_t *, size_t *, bool not_used);

unsigned char* lock_db_get_key(my_dblock_t *ptr, size_t *length,
                       bool not_used __attribute__((unused)))
{
  *length= ptr->name_length;
  return (unsigned char*) ptr->name;
}


/*
  Free lock_db hash element.
*/

extern "C" void lock_db_free_element(void *ptr);

void lock_db_free_element(void *ptr)
{
  free(ptr);
}


/*
  Delete a database lock entry from hash.
*/

void lock_db_delete(const char *name, uint32_t length)
{
  my_dblock_t *opt;
  safe_mutex_assert_owner(&LOCK_lock_db);
  if ((opt= (my_dblock_t *)hash_search(&lock_db_cache,
                                       (const unsigned char*) name, length)))
    hash_delete(&lock_db_cache, (unsigned char*) opt);
}


/* Database options hash */
static HASH dboptions;
static bool dboptions_init= 0;
static rw_lock_t LOCK_dboptions;

/* Structure for database options */
typedef struct my_dbopt_st
{
  char *name;			/* Database name                  */
  uint32_t name_length;		/* Database length name           */
  const CHARSET_INFO *charset;	/* Database default character set */
} my_dbopt_t;


/*
  Function we use in the creation of our hash to get key.
*/

extern "C" unsigned char* dboptions_get_key(my_dbopt_t *opt, size_t *length,
                                    bool not_used);

unsigned char* dboptions_get_key(my_dbopt_t *opt, size_t *length,
                         bool not_used __attribute__((unused)))
{
  *length= opt->name_length;
  return (unsigned char*) opt->name;
}


/*
  Helper function to write a query to binlog used by mysql_rm_db()
*/

static inline void write_to_binlog(Session *session, char *query, uint32_t q_len,
                                   char *db, uint32_t db_len)
{
  Query_log_event qinfo(session, query, q_len, 0, 0);
  qinfo.error_code= 0;
  qinfo.db= db;
  qinfo.db_len= db_len;
  drizzle_bin_log.write(&qinfo);
}  


/*
  Function to free dboptions hash element
*/

extern "C" void free_dbopt(void *dbopt);

void free_dbopt(void *dbopt)
{
  free((unsigned char*) dbopt);
}


/* 
  Initialize database option hash and locked database hash.

  SYNOPSIS
    my_database_names()

  NOTES
    Must be called before any other database function is called.

  RETURN
    0	ok
    1	Fatal error
*/

bool my_database_names_init(void)
{
  bool error= false;
  (void) my_rwlock_init(&LOCK_dboptions, NULL);
  if (!dboptions_init)
  {
    dboptions_init= 1;
    error= hash_init(&dboptions, lower_case_table_names ? 
                     &my_charset_bin : system_charset_info,
                     32, 0, 0, (hash_get_key) dboptions_get_key,
                     free_dbopt,0) ||
           hash_init(&lock_db_cache, lower_case_table_names ? 
                     &my_charset_bin : system_charset_info,
                     32, 0, 0, (hash_get_key) lock_db_get_key,
                     lock_db_free_element,0);

  }
  return error;
}



/* 
  Free database option hash and locked databases hash.
*/

void my_database_names_free(void)
{
  if (dboptions_init)
  {
    dboptions_init= 0;
    hash_free(&dboptions);
    (void) rwlock_destroy(&LOCK_dboptions);
    hash_free(&lock_db_cache);
  }
}


/*
  Cleanup cached options
*/

void my_dbopt_cleanup(void)
{
  rw_wrlock(&LOCK_dboptions);
  hash_free(&dboptions);
  hash_init(&dboptions, lower_case_table_names ? 
            &my_charset_bin : system_charset_info,
            32, 0, 0, (hash_get_key) dboptions_get_key,
            free_dbopt,0);
  rw_unlock(&LOCK_dboptions);
}


/*
  Find database options in the hash.
  
  DESCRIPTION
    Search a database options in the hash, usings its path.
    Fills "create" on success.
  
  RETURN VALUES
    0 on success.
    1 on error.
*/

static bool get_dbopt(const char *dbname, HA_CREATE_INFO *create)
{
  my_dbopt_t *opt;
  uint32_t length;
  bool error= true;
  
  length= (uint) strlen(dbname);
  
  rw_rdlock(&LOCK_dboptions);
  if ((opt= (my_dbopt_t*) hash_search(&dboptions, (unsigned char*) dbname, length)))
  {
    create->default_table_charset= opt->charset;
    error= true;
  }
  rw_unlock(&LOCK_dboptions);
  return error;
}


/*
  Writes database options into the hash.
  
  DESCRIPTION
    Inserts database options into the hash, or updates
    options if they are already in the hash.
  
  RETURN VALUES
    0 on success.
    1 on error.
*/

static bool put_dbopt(const char *dbname, HA_CREATE_INFO *create)
{
  my_dbopt_t *opt;
  uint32_t length;
  bool error= false;

  length= (uint) strlen(dbname);
  
  rw_wrlock(&LOCK_dboptions);
  if (!(opt= (my_dbopt_t*) hash_search(&dboptions, (unsigned char*) dbname, length)))
  { 
    /* Options are not in the hash, insert them */
    char *tmp_name;
    if (!my_multi_malloc(MYF(MY_WME | MY_ZEROFILL),
                         &opt, (uint) sizeof(*opt), &tmp_name, (uint) length+1,
                         NULL))
    {
      error= true;
      goto end;
    }
    
    opt->name= tmp_name;
    my_stpcpy(opt->name, dbname);
    opt->name_length= length;
    
    if ((error= my_hash_insert(&dboptions, (unsigned char*) opt)))
    {
      free(opt);
      goto end;
    }
  }

  /* Update / write options in hash */
  opt->charset= create->default_table_charset;

end:
  rw_unlock(&LOCK_dboptions);  
  return(error);
}


/*
  Deletes database options from the hash.
*/

void del_dbopt(const char *path)
{
  my_dbopt_t *opt;
  rw_wrlock(&LOCK_dboptions);
  if ((opt= (my_dbopt_t *)hash_search(&dboptions, (const unsigned char*) path,
                                      strlen(path))))
    hash_delete(&dboptions, (unsigned char*) opt);
  rw_unlock(&LOCK_dboptions);
}


/*
  Create database options file:

  DESCRIPTION
    Currently database default charset is only stored there.

  RETURN VALUES
  0	ok
  1	Could not create file or write to it.  Error sent through my_error()
*/

static bool write_db_opt(Session *session, const char *path, const char *name, HA_CREATE_INFO *create)
{
  bool error= true;
  drizzle::Schema db;

  assert(name);

  if (!create->default_table_charset)
    create->default_table_charset= session->variables.collation_server;

  if (put_dbopt(path, create))
    return 1;

  db.set_name(name);
  db.set_characterset(create->default_table_charset->csname);
  db.set_collation(create->default_table_charset->name);

  fstream output(path, ios::out | ios::trunc | ios::binary);
  if (!db.SerializeToOstream(&output)) 
    error= false;

  return error;
}


/*
  Load database options file

  load_db_opt()
  path		Path for option file
  create	Where to store the read options

  DESCRIPTION

  RETURN VALUES
  0	File found
  1	No database file or could not open it

*/

bool load_db_opt(Session *session, const char *path, HA_CREATE_INFO *create)
{
  bool error=1;
  drizzle::Schema db;
  string buffer;

  memset(create, 0, sizeof(*create));
  create->default_table_charset= session->variables.collation_server;

  /* Check if options for this database are already in the hash */
  if (!get_dbopt(path, create))
    return(0);

  fstream input(path, ios::in | ios::binary);
  if (!input)
    goto err1;
  else if (!db.ParseFromIstream(&input))
    goto err1;

  buffer= db.characterset();
  if (!(create->default_table_charset= get_charset_by_csname(buffer.c_str(), MY_CS_PRIMARY, MYF(0))))
  {
    sql_print_error(_("Error while loading database options: '%s':"),path);
    sql_print_error(ER(ER_UNKNOWN_COLLATION), buffer.c_str());
    create->default_table_charset= default_charset_info;
  }

  buffer= db.collation();
  if (!(create->default_table_charset= get_charset_by_name(buffer.c_str(), MYF(0))))
  {
    sql_print_error(_("Error while loading database options: '%s':"),path);
    sql_print_error(ER(ER_UNKNOWN_COLLATION), buffer.c_str());
    create->default_table_charset= default_charset_info;
  }

  /*
    Put the loaded value into the hash.
    Note that another thread could've added the same
    entry to the hash after we called get_dbopt(),
    but it's not an error, as put_dbopt() takes this
    possibility into account.
  */
  error= put_dbopt(path, create);

err1:
  return(error);
}


/*
  Retrieve database options by name. Load database options file or fetch from
  cache.

  SYNOPSIS
    load_db_opt_by_name()
    db_name         Database name
    db_create_info  Where to store the database options

  DESCRIPTION
    load_db_opt_by_name() is a shortcut for load_db_opt().

  NOTE
    Although load_db_opt_by_name() (and load_db_opt()) returns status of
    the operation, it is useless usually and should be ignored. The problem
    is that there are 1) system databases ("mysql") and 2) virtual
    databases ("information_schema"), which do not contain options file.
    So, load_db_opt[_by_name]() returns false for these databases, but this
    is not an error.

    load_db_opt[_by_name]() clears db_create_info structure in any case, so
    even on failure it contains valid data. So, common use case is just
    call load_db_opt[_by_name]() without checking return value and use
    db_create_info right after that.

  RETURN VALUES (read NOTE!)
    false   Success
    true    Failed to retrieve options
*/

bool load_db_opt_by_name(Session *session, const char *db_name,
                         HA_CREATE_INFO *db_create_info)
{
  char db_opt_path[FN_REFLEN];

  /*
    Pass an empty file name, and the database options file name as extension
    to avoid table name to file name encoding.
  */
  (void) build_table_filename(db_opt_path, sizeof(db_opt_path),
                              db_name, "", MY_DB_OPT_FILE, 0);

  return load_db_opt(session, db_opt_path, db_create_info);
}


/**
  Return default database collation.

  @param session     Thread context.
  @param db_name Database name.

  @return CHARSET_INFO object. The operation always return valid character
    set, even if the database does not exist.
*/

const CHARSET_INFO *get_default_db_collation(Session *session, const char *db_name)
{
  HA_CREATE_INFO db_info;

  if (session->db != NULL && strcmp(db_name, session->db) == 0)
    return session->db_charset;

  load_db_opt_by_name(session, db_name, &db_info);

  /*
    NOTE: even if load_db_opt_by_name() fails,
    db_info.default_table_charset contains valid character set
    (collation_server). We should not fail if load_db_opt_by_name() fails,
    because it is valid case. If a database has been created just by
    "mkdir", it does not contain db.opt file, but it is valid database.
  */

  return db_info.default_table_charset;
}


/*
  Create a database

  SYNOPSIS
  mysql_create_db()
  session		Thread handler
  db		Name of database to create
		Function assumes that this is already validated.
  create_info	Database create options (like character set)
  silent	Used by replication when internally creating a database.
		In this case the entry should not be logged.

  SIDE-EFFECTS
   1. Report back to client that command succeeded (my_ok)
   2. Report errors to client
   3. Log event to binary log
   (The 'silent' flags turns off 1 and 3.)

  RETURN VALUES
  false ok
  true  Error

*/

int mysql_create_db(Session *session, char *db, HA_CREATE_INFO *create_info, bool silent)
{
  char	 path[FN_REFLEN+16];
  char	 tmp_query[FN_REFLEN+16];
  long result= 1;
  int error= 0;
  struct stat stat_info;
  uint32_t create_options= create_info ? create_info->options : 0;
  uint32_t path_len;

  /* do not create 'information_schema' db */
  if (!my_strcasecmp(system_charset_info, db, INFORMATION_SCHEMA_NAME.c_str()))
  {
    my_error(ER_DB_CREATE_EXISTS, MYF(0), db);
    return(-1);
  }

  /*
    Do not create database if another thread is holding read lock.
    Wait for global read lock before acquiring LOCK_drizzle_create_db.
    After wait_if_global_read_lock() we have protection against another
    global read lock. If we would acquire LOCK_drizzle_create_db first,
    another thread could step in and get the global read lock before we
    reach wait_if_global_read_lock(). If this thread tries the same as we
    (admin a db), it would then go and wait on LOCK_drizzle_create_db...
    Furthermore wait_if_global_read_lock() checks if the current thread
    has the global read lock and refuses the operation with
    ER_CANT_UPDATE_WITH_READLOCK if applicable.
  */
  if (wait_if_global_read_lock(session, 0, 1))
  {
    error= -1;
    goto exit2;
  }

  pthread_mutex_lock(&LOCK_drizzle_create_db);

  /* Check directory */
  path_len= build_table_filename(path, sizeof(path), db, "", "", 0);
  path[path_len-1]= 0;                    // Remove last '/' from path

  if (!stat(path,&stat_info))
  {
    if (!(create_options & HA_LEX_CREATE_IF_NOT_EXISTS))
    {
      my_error(ER_DB_CREATE_EXISTS, MYF(0), db);
      error= -1;
      goto exit;
    }
    push_warning_printf(session, DRIZZLE_ERROR::WARN_LEVEL_NOTE,
			ER_DB_CREATE_EXISTS, ER(ER_DB_CREATE_EXISTS), db);
    if (!silent)
      my_ok(session);
    error= 0;
    goto exit;
  }
  else
  {
    if (errno != ENOENT)
    {
      my_error(EE_STAT, MYF(0), path, errno);
      goto exit;
    }
    if (my_mkdir(path,0777,MYF(0)) < 0)
    {
      my_error(ER_CANT_CREATE_DB, MYF(0), db, my_errno);
      error= -1;
      goto exit;
    }
  }

  path[path_len-1]= FN_LIBCHAR;
  strmake(path+path_len, MY_DB_OPT_FILE, sizeof(path)-path_len-1);
  if (write_db_opt(session, path, db, create_info))
  {
    /*
      Could not create options file.
      Restore things to beginning.
    */
    path[path_len]= 0;
    if (rmdir(path) >= 0)
    {
      error= -1;
      goto exit;
    }
    /*
      We come here when we managed to create the database, but not the option
      file.  In this case it's best to just continue as if nothing has
      happened.  (This is a very unlikely senario)
    */
  }
  
  if (!silent)
  {
    char *query;
    uint32_t query_length;

    if (!session->query)				// Only in replication
    {
      query= 	     tmp_query;
      query_length= (uint) (strxmov(tmp_query,"create database `",
                                    db, "`", NULL) - tmp_query);
    }
    else
    {
      query= 	    session->query;
      query_length= session->query_length;
    }

    if (drizzle_bin_log.is_open())
    {
      Query_log_event qinfo(session, query, query_length, 0, 
			    /* suppress_use */ true);

      /*
	Write should use the database being created as the "current
        database" and not the threads current database, which is the
        default. If we do not change the "current database" to the
        database being created, the CREATE statement will not be
        replicated when using --binlog-do-db to select databases to be
        replicated. 

	An example (--binlog-do-db=sisyfos):
       
          CREATE DATABASE bob;        # Not replicated
          USE bob;                    # 'bob' is the current database
          CREATE DATABASE sisyfos;    # Not replicated since 'bob' is
                                      # current database.
          USE sisyfos;                # Will give error on slave since
                                      # database does not exist.
      */
      qinfo.db     = db;
      qinfo.db_len = strlen(db);

      /* These DDL methods and logging protected with LOCK_drizzle_create_db */
      drizzle_bin_log.write(&qinfo);
    }
    my_ok(session, result);
  }

exit:
  pthread_mutex_unlock(&LOCK_drizzle_create_db);
  start_waiting_global_read_lock(session);
exit2:
  return(error);
}


/* db-name is already validated when we come here */

bool mysql_alter_db(Session *session, const char *db, HA_CREATE_INFO *create_info)
{
  char path[FN_REFLEN+16];
  long result=1;
  int error= false;

  /*
    Do not alter database if another thread is holding read lock.
    Wait for global read lock before acquiring LOCK_drizzle_create_db.
    After wait_if_global_read_lock() we have protection against another
    global read lock. If we would acquire LOCK_drizzle_create_db first,
    another thread could step in and get the global read lock before we
    reach wait_if_global_read_lock(). If this thread tries the same as we
    (admin a db), it would then go and wait on LOCK_drizzle_create_db...
    Furthermore wait_if_global_read_lock() checks if the current thread
    has the global read lock and refuses the operation with
    ER_CANT_UPDATE_WITH_READLOCK if applicable.
  */
  if ((error=wait_if_global_read_lock(session,0,1)))
    goto exit2;

  pthread_mutex_lock(&LOCK_drizzle_create_db);

  /* 
     Recreate db options file: /dbpath/.db.opt
     We pass MY_DB_OPT_FILE as "extension" to avoid
     "table name to file name" encoding.
  */
  build_table_filename(path, sizeof(path), db, "", MY_DB_OPT_FILE, 0);
  if ((error=write_db_opt(session, path, db, create_info)))
    goto exit;

  /* Change options if current database is being altered. */

  if (session->db && !strcmp(session->db,db))
  {
    session->db_charset= create_info->default_table_charset ?
		     create_info->default_table_charset :
		     session->variables.collation_server;
    session->variables.collation_database= session->db_charset;
  }

  if (drizzle_bin_log.is_open())
  {
    Query_log_event qinfo(session, session->query, session->query_length, 0,
			  /* suppress_use */ true);

    /*
      Write should use the database being created as the "current
      database" and not the threads current database, which is the
      default.
    */
    qinfo.db     = db;
    qinfo.db_len = strlen(db);

    session->clear_error();
    /* These DDL methods and logging protected with LOCK_drizzle_create_db */
    drizzle_bin_log.write(&qinfo);
  }
  my_ok(session, result);

exit:
  pthread_mutex_unlock(&LOCK_drizzle_create_db);
  start_waiting_global_read_lock(session);
exit2:
  return(error);
}


/*
  Drop all tables in a database and the database itself

  SYNOPSIS
    mysql_rm_db()
    session			Thread handle
    db			Database name in the case given by user
		        It's already validated and set to lower case
                        (if needed) when we come here
    if_exists		Don't give error if database doesn't exists
    silent		Don't generate errors

  RETURN
    false ok (Database dropped)
    ERROR Error
*/

bool mysql_rm_db(Session *session,char *db,bool if_exists, bool silent)
{
  long deleted=0;
  int error= false;
  char	path[FN_REFLEN+16];
  MY_DIR *dirp;
  uint32_t length;
  TableList* dropped_tables= 0;

  if (db && (strcmp(db, "information_schema") == 0))
  {
    my_error(ER_DBACCESS_DENIED_ERROR, MYF(0), "", "", INFORMATION_SCHEMA_NAME.c_str());
    return(true);
  }

  /*
    Do not drop database if another thread is holding read lock.
    Wait for global read lock before acquiring LOCK_drizzle_create_db.
    After wait_if_global_read_lock() we have protection against another
    global read lock. If we would acquire LOCK_drizzle_create_db first,
    another thread could step in and get the global read lock before we
    reach wait_if_global_read_lock(). If this thread tries the same as we
    (admin a db), it would then go and wait on LOCK_drizzle_create_db...
    Furthermore wait_if_global_read_lock() checks if the current thread
    has the global read lock and refuses the operation with
    ER_CANT_UPDATE_WITH_READLOCK if applicable.
  */
  if (wait_if_global_read_lock(session, 0, 1))
  {
    error= -1;
    goto exit2;
  }

  pthread_mutex_lock(&LOCK_drizzle_create_db);

  /*
    This statement will be replicated as a statement, even when using
    row-based replication. The flag will be reset at the end of the
    statement.
  */
  session->clear_current_stmt_binlog_row_based();

  length= build_table_filename(path, sizeof(path), db, "", "", 0);
  my_stpcpy(path+length, MY_DB_OPT_FILE);		// Append db option file name
  del_dbopt(path);				// Remove dboption hash entry
  path[length]= '\0';				// Remove file name

  /* See if the directory exists */
  if (!(dirp= my_dir(path,MYF(MY_DONT_SORT))))
  {
    if (!if_exists)
    {
      error= -1;
      my_error(ER_DB_DROP_EXISTS, MYF(0), db);
      goto exit;
    }
    else
      push_warning_printf(session, DRIZZLE_ERROR::WARN_LEVEL_NOTE,
			  ER_DB_DROP_EXISTS, ER(ER_DB_DROP_EXISTS), db);
  }
  else
  {
    pthread_mutex_lock(&LOCK_open);
    remove_db_from_cache(db);
    pthread_mutex_unlock(&LOCK_open);

    
    error= -1;
    if ((deleted= mysql_rm_known_files(session, dirp, db, path, 0,
                                       &dropped_tables)) >= 0)
    {
      ha_drop_database(path);
      error = 0;
    }
  }
  if (!silent && deleted>=0)
  {
    const char *query;
    uint32_t query_length;
    if (!session->query)
    {
      /* The client used the old obsolete mysql_drop_db() call */
      query= path;
      query_length= (uint) (strxmov(path, "drop database `", db, "`",
                                     NULL) - path);
    }
    else
    {
      query =session->query;
      query_length= session->query_length;
    }
    if (drizzle_bin_log.is_open())
    {
      Query_log_event qinfo(session, query, query_length, 0, 
			    /* suppress_use */ true);
      /*
        Write should use the database being created as the "current
        database" and not the threads current database, which is the
        default.
      */
      qinfo.db     = db;
      qinfo.db_len = strlen(db);

      session->clear_error();
      /* These DDL methods and logging protected with LOCK_drizzle_create_db */
      drizzle_bin_log.write(&qinfo);
    }
    session->clear_error();
    session->server_status|= SERVER_STATUS_DB_DROPPED;
    my_ok(session, (uint32_t) deleted);
    session->server_status&= ~SERVER_STATUS_DB_DROPPED;
  }
  else if (drizzle_bin_log.is_open())
  {
    char *query, *query_pos, *query_end, *query_data_start;
    TableList *tbl;
    uint32_t db_len;

    if (!(query= (char*) session->alloc(MAX_DROP_TABLE_Q_LEN)))
      goto exit; /* not much else we can do */
    query_pos= query_data_start= my_stpcpy(query,"drop table ");
    query_end= query + MAX_DROP_TABLE_Q_LEN;
    db_len= strlen(db);

    for (tbl= dropped_tables; tbl; tbl= tbl->next_local)
    {
      uint32_t tbl_name_len;

      /* 3 for the quotes and the comma*/
      tbl_name_len= strlen(tbl->table_name) + 3;
      if (query_pos + tbl_name_len + 1 >= query_end)
      {
        /* These DDL methods and logging protected with LOCK_drizzle_create_db */
        write_to_binlog(session, query, query_pos -1 - query, db, db_len);
        query_pos= query_data_start;
      }

      *query_pos++ = '`';
      query_pos= my_stpcpy(query_pos,tbl->table_name);
      *query_pos++ = '`';
      *query_pos++ = ',';
    }

    if (query_pos != query_data_start)
    {
      /* These DDL methods and logging protected with LOCK_drizzle_create_db */
      write_to_binlog(session, query, query_pos -1 - query, db, db_len);
    }
  }

exit:
  /*
    If this database was the client's selected database, we silently
    change the client's selected database to nothing (to have an empty
    SELECT DATABASE() in the future). For this we free() session->db and set
    it to 0.
  */
  if (session->db && !strcmp(session->db, db))
    mysql_change_db_impl(session, NULL, session->variables.collation_server);
  pthread_mutex_unlock(&LOCK_drizzle_create_db);
  start_waiting_global_read_lock(session);
exit2:
  return(error);
}

/*
  Removes files with known extensions plus.
  session MUST be set when calling this function!
*/

static long mysql_rm_known_files(Session *session, MY_DIR *dirp, const char *db,
				 const char *org_path, uint32_t level,
                                 TableList **dropped_tables)
{
  long deleted=0;
  uint32_t found_other_files=0;
  char filePath[FN_REFLEN];
  TableList *tot_list=0, **tot_list_next;

  tot_list_next= &tot_list;

  for (uint32_t idx=0 ;
       idx < (uint) dirp->number_off_files && !session->killed ;
       idx++)
  {
    FILEINFO *file=dirp->dir_entry+idx;
    char *extension;

    /* skiping . and .. */
    if (file->name[0] == '.' && (!file->name[1] ||
       (file->name[1] == '.' &&  !file->name[2])))
      continue;

    if (!(extension= strrchr(file->name, '.')))
      extension= strchr(file->name, '\0');
    if (find_type(extension, &deletable_extentions,1+2) <= 0)
    {
      /*
        ass ass ass.

        strange checking for magic extensions that are then deleted if
        not reg_ext (i.e. .frm).

        and (previously) we'd err out on drop database if files not matching
        engine ha_known_exts() or deletable_extensions were present.

        presumably this was to avoid deleting other user data... except if that
        data happened to be in files ending in .BAK, .opt or .TMD. *fun*
       */
      find_type(extension, ha_known_exts(),1+2);
      continue;
    }
    /* just for safety we use files_charset_info */
    if (db && !my_strcasecmp(files_charset_info,
                             extension, reg_ext))
    {
      /* Drop the table nicely */
      *extension= 0;			// Remove extension
      TableList *table_list=(TableList*)
                              session->calloc(sizeof(*table_list) + 
                                          strlen(db) + 1 +
                                          MYSQL50_TABLE_NAME_PREFIX_LENGTH + 
                                          strlen(file->name) + 1);

      if (!table_list)
        goto err;
      table_list->db= (char*) (table_list+1);
      table_list->table_name= my_stpcpy(table_list->db, db) + 1;
      filename_to_tablename(file->name, table_list->table_name,
                            MYSQL50_TABLE_NAME_PREFIX_LENGTH +
                            strlen(file->name) + 1);
      table_list->alias= table_list->table_name;	// If lower_case_table_names=2
      table_list->internal_tmp_table= is_prefix(file->name, TMP_FILE_PREFIX);
      /* Link into list */
      (*tot_list_next)= table_list;
      tot_list_next= &table_list->next_local;
      deleted++;
    }
    else
    {
      strxmov(filePath, org_path, "/", file->name, NULL);
      if (my_delete_with_symlink(filePath,MYF(MY_WME)))
      {
	goto err;
      }
    }
  }
  if (session->killed ||
      (tot_list && mysql_rm_table_part2(session, tot_list, 1, 0, 1)))
    goto err;

  my_dirend(dirp);  
  
  if (dropped_tables)
    *dropped_tables= tot_list;
  
  /*
    If the directory is a symbolic link, remove the link first, then
    remove the directory the symbolic link pointed at
  */
  if (found_other_files)
  {
    my_error(ER_DB_DROP_RMDIR, MYF(0), org_path, EEXIST);
    return(-1);
  }
  else
  {
    /* Don't give errors if we can't delete 'RAID' directory */
    if (rm_dir_w_symlink(org_path, level == 0))
      return(-1);
  }

  return(deleted);

err:
  my_dirend(dirp);
  return(-1);
}


/*
  Remove directory with symlink

  SYNOPSIS
    rm_dir_w_symlink()
    org_path    path of derictory
    send_error  send errors
  RETURN
    0 OK
    1 ERROR
*/

static bool rm_dir_w_symlink(const char *org_path, bool send_error)
{
  char tmp_path[FN_REFLEN], *pos;
  char *path= tmp_path;
  unpack_filename(tmp_path, org_path);
#ifdef HAVE_READLINK
  int error;
  char tmp2_path[FN_REFLEN];

  /* Remove end FN_LIBCHAR as this causes problem on Linux in readlink */
  pos= strchr(path, '\0');
  if (pos > path && pos[-1] == FN_LIBCHAR)
    *--pos=0;

  if ((error= my_readlink(tmp2_path, path, MYF(MY_WME))) < 0)
    return(1);
  if (!error)
  {
    if (my_delete(path, MYF(send_error ? MY_WME : 0)))
    {
      return(send_error);
    }
    /* Delete directory symbolic link pointed at */
    path= tmp2_path;
  }
#endif
  /* Remove last FN_LIBCHAR to not cause a problem on OS/2 */
  pos= strchr(path, '\0');

  if (pos > path && pos[-1] == FN_LIBCHAR)
    *--pos=0;
  if (rmdir(path) < 0 && send_error)
  {
    my_error(ER_DB_DROP_RMDIR, MYF(0), path, errno);
    return(1);
  }
  return(0);
}


/**
  @brief Internal implementation: switch current database to a valid one.

  @param session            Thread context.
  @param new_db_name    Name of the database to switch to. The function will
                        take ownership of the name (the caller must not free
                        the allocated memory). If the name is NULL, we're
                        going to switch to NULL db.
  @param new_db_charset Character set of the new database.
*/

static void mysql_change_db_impl(Session *session,
                                 LEX_STRING *new_db_name,
                                 const CHARSET_INFO * const new_db_charset)
{
  /* 1. Change current database in Session. */

  if (new_db_name == NULL)
  {
    /*
      Session::set_db() does all the job -- it frees previous database name and
      sets the new one.
    */

    session->set_db(NULL, 0);
  }
  else if (my_strcasecmp(system_charset_info, new_db_name->str,
                         INFORMATION_SCHEMA_NAME.c_str()) == 0)
  {
    /*
      Here we must use Session::set_db(), because we want to copy
      INFORMATION_SCHEMA_NAME constant.
    */

    session->set_db(INFORMATION_SCHEMA_NAME.c_str(),
                    INFORMATION_SCHEMA_NAME.length());
  }
  else
  {
    /*
      Here we already have a copy of database name to be used in Session. So,
      we just call Session::reset_db(). Since Session::reset_db() does not releases
      the previous database name, we should do it explicitly.
    */

    if (session->db)
      free(session->db);

    session->reset_db(new_db_name->str, new_db_name->length);
  }

  /* 3. Update db-charset environment variables. */

  session->db_charset= new_db_charset;
  session->variables.collation_database= new_db_charset;
}



/**
  Backup the current database name before switch.

  @param[in]      session             thread handle
  @param[in, out] saved_db_name   IN: "str" points to a buffer where to store
                                  the old database name, "length" contains the
                                  buffer size
                                  OUT: if the current (default) database is
                                  not NULL, its name is copied to the
                                  buffer pointed at by "str"
                                  and "length" is updated accordingly.
                                  Otherwise "str" is set to NULL and
                                  "length" is set to 0.
*/

static void backup_current_db_name(Session *session,
                                   LEX_STRING *saved_db_name)
{
  if (!session->db)
  {
    /* No current (default) database selected. */

    saved_db_name->str= NULL;
    saved_db_name->length= 0;
  }
  else
  {
    strmake(saved_db_name->str, session->db, saved_db_name->length - 1);
    saved_db_name->length= session->db_length;
  }
}


/**
  Return true if db1_name is equal to db2_name, false otherwise.

  The function allows to compare database names according to the MySQL
  rules. The database names db1 and db2 are equal if:
     - db1 is NULL and db2 is NULL;
     or
     - db1 is not-NULL, db2 is not-NULL, db1 is equal (ignoring case) to
       db2 in system character set (UTF8).
*/

static inline bool
cmp_db_names(const char *db1_name,
             const char *db2_name)
{
  return
         /* db1 is NULL and db2 is NULL */
         (!db1_name && !db2_name) ||

         /* db1 is not-NULL, db2 is not-NULL, db1 == db2. */
         (db1_name && db2_name && my_strcasecmp(system_charset_info, db1_name, db2_name) == 0);
}


/**
  @brief Change the current database and its attributes unconditionally.

  @param session          thread handle
  @param new_db_name  database name
  @param force_switch if force_switch is false, then the operation will fail if

                        - new_db_name is NULL or empty;

                        - OR new database name is invalid
                          (check_db_name() failed);

                        - OR user has no privilege on the new database;

                        - OR new database does not exist;

                      if force_switch is true, then

                        - if new_db_name is NULL or empty, the current
                          database will be NULL, @@collation_database will
                          be set to @@collation_server, the operation will
                          succeed.

                        - if new database name is invalid
                          (check_db_name() failed), the current database
                          will be NULL, @@collation_database will be set to
                          @@collation_server, but the operation will fail;

                        - user privileges will not be checked
                          (Session::db_access however is updated);

                          TODO: is this really the intention?
                                (see sp-security.test).

                        - if new database does not exist,the current database
                          will be NULL, @@collation_database will be set to
                          @@collation_server, a warning will be thrown, the
                          operation will succeed.

  @details The function checks that the database name corresponds to a
  valid and existent database, checks access rights and changes the current
  database with database attributes (@@collation_database session variable,
  Session::db_access).

  This function is not the only way to switch the database that is
  currently employed. When the replication slave thread switches the
  database before executing a query, it calls session->set_db directly.
  However, if the query, in turn, uses a stored routine, the stored routine
  will use this function, even if it's run on the slave.

  This function allocates the name of the database on the system heap: this
  is necessary to be able to uniformly change the database from any module
  of the server. Up to 5.0 different modules were using different memory to
  store the name of the database, and this led to memory corruption:
  a stack pointer set by Stored Procedures was used by replication after
  the stack address was long gone.

  @return Operation status
    @retval false Success
    @retval true  Error
*/

bool mysql_change_db(Session *session, const LEX_STRING *new_db_name, bool force_switch)
{
  LEX_STRING new_db_file_name;
  const CHARSET_INFO *db_default_cl;

  if (new_db_name == NULL ||
      new_db_name->length == 0)
  {
    if (force_switch)
    {
      /*
        This can happen only if we're switching the current database back
        after loading stored program. The thing is that loading of stored
        program can happen when there is no current database.

        TODO: actually, new_db_name and new_db_name->str seem to be always
        non-NULL. In case of stored program, new_db_name->str == "" and
        new_db_name->length == 0.
      */

      mysql_change_db_impl(session, NULL, session->variables.collation_server);

      return(false);
    }
    else
    {
      my_message(ER_NO_DB_ERROR, ER(ER_NO_DB_ERROR), MYF(0));

      return(true);
    }
  }

  if (my_strcasecmp(system_charset_info, new_db_name->str,
                    INFORMATION_SCHEMA_NAME.c_str()) == 0)
  {
    /* Switch the current database to INFORMATION_SCHEMA. */
    /* const_cast<> is safe here: mysql_change_db_impl does a copy */
    LEX_STRING is_name= { const_cast<char *>(INFORMATION_SCHEMA_NAME.c_str()),
                          INFORMATION_SCHEMA_NAME.length() };
    mysql_change_db_impl(session, &is_name, system_charset_info);

    return(false);
  }

  /*
    Now we need to make a copy because check_db_name requires a
    non-constant argument. Actually, it takes database file name.

    TODO: fix check_db_name().
  */

  new_db_file_name.str= my_strndup(new_db_name->str, new_db_name->length,
                                   MYF(MY_WME));
  new_db_file_name.length= new_db_name->length;

  if (new_db_file_name.str == NULL)
    return(true);                             /* the error is set */

  /*
    NOTE: if check_db_name() fails, we should throw an error in any case,
    even if we are called from sp_head::execute().

    It's next to impossible however to get this error when we are called
    from sp_head::execute(). But let's switch the current database to NULL
    in this case to be sure.
  */

  if (check_db_name(&new_db_file_name))
  {
    my_error(ER_WRONG_DB_NAME, MYF(0), new_db_file_name.str);
    free(new_db_file_name.str);

    if (force_switch)
      mysql_change_db_impl(session, NULL, session->variables.collation_server);

    return(true);
  }

  if (check_db_dir_existence(new_db_file_name.str))
  {
    if (force_switch)
    {
      /* Throw a warning and free new_db_file_name. */

      push_warning_printf(session, DRIZZLE_ERROR::WARN_LEVEL_NOTE,
                          ER_BAD_DB_ERROR, ER(ER_BAD_DB_ERROR),
                          new_db_file_name.str);

      free(new_db_file_name.str);

      /* Change db to NULL. */

      mysql_change_db_impl(session, NULL, session->variables.collation_server);

      /* The operation succeed. */

      return(false);
    }
    else
    {
      /* Report an error and free new_db_file_name. */

      my_error(ER_BAD_DB_ERROR, MYF(0), new_db_file_name.str);
      free(new_db_file_name.str);

      /* The operation failed. */

      return(true);
    }
  }

  /*
    NOTE: in mysql_change_db_impl() new_db_file_name is assigned to Session
    attributes and will be freed in Session::~Session().
  */

  db_default_cl= get_default_db_collation(session, new_db_file_name.str);

  mysql_change_db_impl(session, &new_db_file_name, db_default_cl);

  return(false);
}


/**
  Change the current database and its attributes if needed.

  @param          session             thread handle
  @param          new_db_name     database name
  @param[in, out] saved_db_name   IN: "str" points to a buffer where to store
                                  the old database name, "length" contains the
                                  buffer size
                                  OUT: if the current (default) database is
                                  not NULL, its name is copied to the
                                  buffer pointed at by "str"
                                  and "length" is updated accordingly.
                                  Otherwise "str" is set to NULL and
                                  "length" is set to 0.
  @param          force_switch    @see mysql_change_db()
  @param[out]     cur_db_changed  out-flag to indicate whether the current
                                  database has been changed (valid only if
                                  the function suceeded)
*/

bool mysql_opt_change_db(Session *session,
                         const LEX_STRING *new_db_name,
                         LEX_STRING *saved_db_name,
                         bool force_switch,
                         bool *cur_db_changed)
{
  *cur_db_changed= !cmp_db_names(session->db, new_db_name->str);

  if (!*cur_db_changed)
    return false;

  backup_current_db_name(session, saved_db_name);

  return mysql_change_db(session, new_db_name, force_switch);
}


/*
  Check if there is directory for the database name.

  SYNOPSIS
    check_db_dir_existence()
    db_name   database name

  RETURN VALUES
    false   There is directory for the specified database name.
    true    The directory does not exist.
*/

bool check_db_dir_existence(const char *db_name)
{
  char db_dir_path[FN_REFLEN];
  uint32_t db_dir_path_len;

  db_dir_path_len= build_table_filename(db_dir_path, sizeof(db_dir_path),
                                        db_name, "", "", 0);

  if (db_dir_path_len && db_dir_path[db_dir_path_len - 1] == FN_LIBCHAR)
    db_dir_path[db_dir_path_len - 1]= 0;

  /* Check access. */

  return my_access(db_dir_path, F_OK);
}