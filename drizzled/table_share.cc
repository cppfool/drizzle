/* -*- mode: c++; c-basic-offset: 2; indent-tabs-mode: nil; -*-
 *  vim:expandtab:shiftwidth=2:tabstop=2:smarttab:
 *
 *  Copyright (C) 2010 Brian Aker
 *  Copyright (C) 2009 Sun Microsystems
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

/*
  This class is shared between different table objects. There is one
  instance of table share per one table in the database.
*/

/* Basic functions needed by many modules */
#include "config.h"

#include <pthread.h>
#include <float.h>

#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>


#include <cassert>

#include "drizzled/error.h"
#include "drizzled/gettext.h"
#include "drizzled/sql_base.h"
#include "drizzled/pthread_globals.h"
#include "drizzled/internal/my_pthread.h"

#include "drizzled/session.h"

#include "drizzled/charset.h"
#include "drizzled/internal/m_string.h"
#include "drizzled/internal/my_sys.h"

#include "drizzled/item/string.h"
#include "drizzled/item/int.h"
#include "drizzled/item/decimal.h"
#include "drizzled/item/float.h"
#include "drizzled/item/null.h"
#include "drizzled/temporal.h"

#include "drizzled/field.h"
#include "drizzled/field/str.h"
#include "drizzled/field/num.h"
#include "drizzled/field/blob.h"
#include "drizzled/field/enum.h"
#include "drizzled/field/null.h"
#include "drizzled/field/date.h"
#include "drizzled/field/decimal.h"
#include "drizzled/field/real.h"
#include "drizzled/field/double.h"
#include "drizzled/field/long.h"
#include "drizzled/field/int64_t.h"
#include "drizzled/field/num.h"
#include "drizzled/field/timestamp.h"
#include "drizzled/field/datetime.h"
#include "drizzled/field/varstring.h"

using namespace std;

namespace drizzled
{

extern size_t table_def_size;
TableDefinitionCache table_def_cache;
static pthread_mutex_t LOCK_table_share;
bool table_def_inited= false;

/*****************************************************************************
  Functions to handle table definition cach (TableShare)
 *****************************************************************************/


void TableShare::cacheStart(void)
{
  pthread_mutex_init(&LOCK_table_share, MY_MUTEX_INIT_FAST);
  table_def_inited= true;
  /* 
   * This is going to overalloc a bit - as rehash sets the number of
   * buckets, not the number of elements. BUT, it'll allow us to not need
   * to rehash later on as the unordered_map grows.
 */
  table_def_cache.rehash(table_def_size);
}


void TableShare::cacheStop(void)
{
  if (table_def_inited)
  {
    table_def_inited= false;
    pthread_mutex_destroy(&LOCK_table_share);
  }
}


/**
 * @TODO: This should return size_t
 */
uint32_t cached_table_definitions(void)
{
  return static_cast<uint32_t>(table_def_cache.size());
}


/*
  Mark that we are not using table share anymore.

  SYNOPSIS
  release()
  share		Table share

  IMPLEMENTATION
  If ref_count goes to zero and (we have done a refresh or if we have
  already too many open table shares) then delete the definition.
*/

void TableShare::release(TableShare *share)
{
  bool to_be_deleted= false;
  safe_mutex_assert_owner(&LOCK_open);

  pthread_mutex_lock(&share->mutex);
  if (!--share->ref_count)
  {
    to_be_deleted= true;
  }

  if (to_be_deleted)
  {
    const string key_string(share->getCacheKey(),
                            share->getCacheKeySize());
    TableDefinitionCache::iterator iter= table_def_cache.find(key_string);
    if (iter != table_def_cache.end())
    {
      table_def_cache.erase(iter);
      delete share;
    }
    return;
  }
  pthread_mutex_unlock(&share->mutex);
}

void TableShare::release(const char *key, uint32_t key_length)
{
  const string key_string(key, key_length);

  TableDefinitionCache::iterator iter= table_def_cache.find(key_string);
  if (iter != table_def_cache.end())
  {
    TableShare *share= (*iter).second;
    share->version= 0;                          // Mark for delete
    if (share->ref_count == 0)
    {
      pthread_mutex_lock(&share->mutex);
      table_def_cache.erase(key_string);
      delete share;
    }
  }
}


static TableShare *foundTableShare(TableShare *share)
{
  /*
    We found an existing table definition. Return it if we didn't get
    an error when reading the table definition from file.
  */

  /* We must do a lock to ensure that the structure is initialized */
  (void) pthread_mutex_lock(&share->mutex);
  if (share->error)
  {
    /* Table definition contained an error */
    share->open_table_error(share->error, share->open_errno, share->errarg);
    (void) pthread_mutex_unlock(&share->mutex);

    return NULL;
  }

  share->ref_count++;
  (void) pthread_mutex_unlock(&share->mutex);

  return share;
}

/*
  Get TableShare for a table.

  get_table_share()
  session			Thread handle
  table_list		Table that should be opened
  key			Table cache key
  key_length		Length of key
  error			out: Error code from open_table_def()

  IMPLEMENTATION
  Get a table definition from the table definition cache.
  If it doesn't exist, create a new from the table definition file.

  NOTES
  We must have wrlock on LOCK_open when we come here
  (To be changed later)

  RETURN
  0  Error
#  Share for table
*/

TableShare *TableShare::getShare(Session *session, 
                                 char *key,
                                 uint32_t key_length, int *error)
{
  const string key_string(key, key_length);
  TableShare *share= NULL;

  *error= 0;

  /* Read table definition from cache */
  TableDefinitionCache::iterator iter= table_def_cache.find(key_string);
  if (iter != table_def_cache.end())
  {
    share= (*iter).second;
    return foundTableShare(share);
  }

  if (not (share= new TableShare(key, key_length)))
  {
    return NULL;
  }

  /*
    Lock mutex to be able to read table definition from file without
    conflicts
  */
  (void) pthread_mutex_lock(&share->mutex);

  /**
   * @TODO: we need to eject something if we exceed table_def_size
 */
  pair<TableDefinitionCache::iterator, bool> ret=
    table_def_cache.insert(make_pair(key_string, share));
  if (ret.second == false)
  {
    delete share;

    return NULL;
  }

  TableIdentifier identifier(share->getSchemaName(), share->getTableName());
  if (share->open_table_def(*session, identifier))
  {
    *error= share->error;
    table_def_cache.erase(key_string);
    delete share;

    return NULL;
  }
  share->ref_count++;				// Mark in use
  (void) pthread_mutex_unlock(&share->mutex);

  return share;
}


/*
  Check if table definition exits in cache

  SYNOPSIS
  get_cached_table_share()
  db			Database name
  table_name		Table name

  RETURN
  0  Not cached
#  TableShare for table
*/
TableShare *TableShare::getShare(TableIdentifier &identifier)
{
  char key[MAX_DBKEY_LENGTH];
  uint32_t key_length;
  safe_mutex_assert_owner(&LOCK_open);

  key_length= TableShare::createKey(key, identifier);

  const string key_string(key, key_length);

  TableDefinitionCache::iterator iter= table_def_cache.find(key_string);
  if (iter != table_def_cache.end())
  {
    return (*iter).second;
  }
  else
  {
    return NULL;
  }
}

/* Get column name from column hash */

static unsigned char *get_field_name(Field **buff, size_t *length, bool)
{
  *length= (uint32_t) strlen((*buff)->field_name);
  return (unsigned char*) (*buff)->field_name;
}

static enum_field_types proto_field_type_to_drizzle_type(uint32_t proto_field_type)
{
  enum_field_types field_type;

  switch(proto_field_type)
  {
  case message::Table::Field::INTEGER:
    field_type= DRIZZLE_TYPE_LONG;
    break;
  case message::Table::Field::DOUBLE:
    field_type= DRIZZLE_TYPE_DOUBLE;
    break;
  case message::Table::Field::TIMESTAMP:
    field_type= DRIZZLE_TYPE_TIMESTAMP;
    break;
  case message::Table::Field::BIGINT:
    field_type= DRIZZLE_TYPE_LONGLONG;
    break;
  case message::Table::Field::DATETIME:
    field_type= DRIZZLE_TYPE_DATETIME;
    break;
  case message::Table::Field::DATE:
    field_type= DRIZZLE_TYPE_DATE;
    break;
  case message::Table::Field::VARCHAR:
    field_type= DRIZZLE_TYPE_VARCHAR;
    break;
  case message::Table::Field::DECIMAL:
    field_type= DRIZZLE_TYPE_DECIMAL;
    break;
  case message::Table::Field::ENUM:
    field_type= DRIZZLE_TYPE_ENUM;
    break;
  case message::Table::Field::BLOB:
    field_type= DRIZZLE_TYPE_BLOB;
    break;
  default:
    field_type= DRIZZLE_TYPE_LONG; /* Set value to kill GCC warning */
    assert(1);
  }

  return field_type;
}

static Item *default_value_item(enum_field_types field_type,
                                const CHARSET_INFO *charset,
                                bool default_null, const string *default_value,
                                const string *default_bin_value)
{
  Item *default_item= NULL;
  int error= 0;

  if (default_null)
  {
    return new Item_null();
  }

  switch(field_type)
  {
  case DRIZZLE_TYPE_LONG:
  case DRIZZLE_TYPE_LONGLONG:
    default_item= new Item_int(default_value->c_str(),
                               (int64_t) internal::my_strtoll10(default_value->c_str(),
                                                                NULL,
                                                                &error),
                               default_value->length());
    break;
  case DRIZZLE_TYPE_DOUBLE:
    default_item= new Item_float(default_value->c_str(),
                                 default_value->length());
    break;
  case DRIZZLE_TYPE_NULL:
    assert(false);
  case DRIZZLE_TYPE_TIMESTAMP:
  case DRIZZLE_TYPE_DATETIME:
  case DRIZZLE_TYPE_DATE:
    if (default_value->compare("NOW()") == 0)
      break;
  case DRIZZLE_TYPE_ENUM:
    default_item= new Item_string(default_value->c_str(),
                                  default_value->length(),
                                  system_charset_info);
    break;
  case DRIZZLE_TYPE_VARCHAR:
  case DRIZZLE_TYPE_BLOB: /* Blob is here due to TINYTEXT. Feel the hate. */
    if (charset==&my_charset_bin)
    {
      default_item= new Item_string(default_bin_value->c_str(),
                                    default_bin_value->length(),
                                    &my_charset_bin);
    }
    else
    {
      default_item= new Item_string(default_value->c_str(),
                                    default_value->length(),
                                    system_charset_info);
    }
    break;
  case DRIZZLE_TYPE_DECIMAL:
    default_item= new Item_decimal(default_value->c_str(),
                                   default_value->length(),
                                   system_charset_info);
    break;
  }

  return default_item;
}



/**
 * @todo
 *
 * Precache this stuff....
 */
bool TableShare::fieldInPrimaryKey(Field *in_field) const
{
  assert(table_proto != NULL);

  size_t num_indexes= table_proto->indexes_size();

  for (size_t x= 0; x < num_indexes; ++x)
  {
    const message::Table::Index &index= table_proto->indexes(x);
    if (index.is_primary())
    {
      size_t num_parts= index.index_part_size();
      for (size_t y= 0; y < num_parts; ++y)
      {
        if (index.index_part(y).fieldnr() == in_field->field_index)
          return true;
      }
    }
  }
  return false;
}

TableDefinitionCache &TableShare::getCache()
{
  return table_def_cache;
}

TableShare::TableShare(char *key, uint32_t key_length, char *path_arg, uint32_t path_length_arg) :
  table_category(TABLE_UNKNOWN_CATEGORY),
  open_count(0),
  field(NULL),
  found_next_number_field(NULL),
  timestamp_field(NULL),
  key_info(NULL),
  blob_field(NULL),
  intervals(NULL),
  default_values(NULL),
  block_size(0),
  version(0),
  timestamp_offset(0),
  reclength(0),
  stored_rec_length(0),
  row_type(ROW_TYPE_DEFAULT),
  max_rows(0),
  table_proto(NULL),
  storage_engine(NULL),
  tmp_table(message::Table::STANDARD),
  ref_count(0),
  null_bytes(0),
  last_null_bit_pos(0),
  fields(0),
  rec_buff_length(0),
  keys(0),
  key_parts(0),
  max_key_length(0),
  max_unique_length(0),
  total_key_length(0),
  uniques(0),
  null_fields(0),
  blob_fields(0),
  timestamp_field_offset(0),
  varchar_fields(0),
  db_create_options(0),
  db_options_in_use(0),
  db_record_offset(0),
  rowid_field_offset(0),
  primary_key(0),
  next_number_index(0),
  next_number_key_offset(0),
  next_number_keypart(0),
  error(0),
  open_errno(0),
  errarg(0),
  column_bitmap_size(0),
  blob_ptr_size(0),
  db_low_byte_first(false),
  name_lock(false),
  replace_with_name_lock(false),
  waiting_on_cond(false),
  keys_in_use(0),
  keys_for_keyread(0),
  newed(true)
{
  memset(&name_hash, 0, sizeof(HASH));

  table_charset= 0;
  memset(&all_set, 0, sizeof (MyBitmap));
  memset(&table_cache_key, 0, sizeof(LEX_STRING));
  memset(&db, 0, sizeof(LEX_STRING));
  memset(&table_name, 0, sizeof(LEX_STRING));
  memset(&path, 0, sizeof(LEX_STRING));
  memset(&normalized_path, 0, sizeof(LEX_STRING));

  mem_root.init_alloc_root(TABLE_ALLOC_BLOCK_SIZE);
  char *key_buff, *path_buff;
  std::string _path;

  db.str= key;
  db.length= strlen(db.str);
  table_name.str= db.str + db.length + 1;
  table_name.length= strlen(table_name.str);

  if (path_arg)
  {
    _path.append(path_arg, path_length_arg);
  }
  else
  {
    build_table_filename(_path, db.str, table_name.str, false);
  }

  if (multi_alloc_root(&mem_root,
                       &key_buff, key_length,
                       &path_buff, _path.length() + 1,
                       NULL))
  {
    set_table_cache_key(key_buff, key, key_length, db.length, table_name.length);

    setPath(path_buff, _path.length());
    strcpy(path_buff, _path.c_str());
    setNormalizedPath(path_buff, _path.length());

    version=       refresh_version;

    pthread_mutex_init(&mutex, MY_MUTEX_INIT_FAST);
    pthread_cond_init(&cond, NULL);
  }
  else
  {
    assert(0); // We should throw here.
  }

  newed= true;
}

int TableShare::inner_parse_table_proto(Session& session, message::Table &table)
{
  TableShare *share= this;
  int local_error= 0;

  if (! table.IsInitialized())
  {
    my_error(ER_CORRUPT_TABLE_DEFINITION, MYF(0), table.InitializationErrorString().c_str());
    return ER_CORRUPT_TABLE_DEFINITION;
  }

  share->setTableProto(new(nothrow) message::Table(table));

  share->storage_engine= plugin::StorageEngine::findByName(session, table.engine().name());
  assert(share->storage_engine); // We use an assert() here because we should never get this far and still have no suitable engine.

  message::Table::TableOptions table_options;

  if (table.has_options())
    table_options= table.options();

  uint32_t local_db_create_options= 0;

  if (table_options.pack_record())
    local_db_create_options|= HA_OPTION_PACK_RECORD;

  /* local_db_create_options was stored as 2 bytes in FRM
    Any HA_OPTION_ that doesn't fit into 2 bytes was silently truncated away.
  */
  share->db_create_options= (local_db_create_options & 0x0000FFFF);
  share->db_options_in_use= share->db_create_options;

  share->row_type= table_options.has_row_type() ?
    (enum row_type) table_options.row_type() : ROW_TYPE_DEFAULT;

  share->block_size= table_options.has_block_size() ?
    table_options.block_size() : 0;

  share->table_charset= get_charset(table_options.has_collation_id()?
                                    table_options.collation_id() : 0);

  if (!share->table_charset)
  {
    /* unknown charset in head[38] or pre-3.23 frm */
    if (use_mb(default_charset_info))
    {
      /* Warn that we may be changing the size of character columns */
      errmsg_printf(ERRMSG_LVL_WARN,
                    _("'%s' had no or invalid character set, "
                      "and default character set is multi-byte, "
                      "so character column sizes may have changed"),
                    share->getPath());
    }
    share->table_charset= default_charset_info;
  }

  share->db_record_offset= 1;

  share->blob_ptr_size= portable_sizeof_char_ptr; // more bonghits.

  share->keys= table.indexes_size();

  share->key_parts= 0;
  for (int indx= 0; indx < table.indexes_size(); indx++)
    share->key_parts+= table.indexes(indx).index_part_size();

  share->key_info= (KEY*) share->alloc_root( table.indexes_size() * sizeof(KEY) +share->key_parts*sizeof(KeyPartInfo));

  KeyPartInfo *key_part;

  key_part= reinterpret_cast<KeyPartInfo*>
    (share->key_info+table.indexes_size());


  ulong *rec_per_key= (ulong*) share->alloc_root(sizeof(ulong*)*share->key_parts);

  KEY* keyinfo= share->key_info;
  for (int keynr= 0; keynr < table.indexes_size(); keynr++, keyinfo++)
  {
    message::Table::Index indx= table.indexes(keynr);

    keyinfo->table= 0;
    keyinfo->flags= 0;

    if (indx.is_unique())
      keyinfo->flags|= HA_NOSAME;

    if (indx.has_options())
    {
      message::Table::Index::IndexOptions indx_options= indx.options();
      if (indx_options.pack_key())
        keyinfo->flags|= HA_PACK_KEY;

      if (indx_options.var_length_key())
        keyinfo->flags|= HA_VAR_LENGTH_PART;

      if (indx_options.null_part_key())
        keyinfo->flags|= HA_NULL_PART_KEY;

      if (indx_options.binary_pack_key())
        keyinfo->flags|= HA_BINARY_PACK_KEY;

      if (indx_options.has_partial_segments())
        keyinfo->flags|= HA_KEY_HAS_PART_KEY_SEG;

      if (indx_options.auto_generated_key())
        keyinfo->flags|= HA_GENERATED_KEY;

      if (indx_options.has_key_block_size())
      {
        keyinfo->flags|= HA_USES_BLOCK_SIZE;
        keyinfo->block_size= indx_options.key_block_size();
      }
      else
      {
        keyinfo->block_size= 0;
      }
    }

    switch (indx.type())
    {
    case message::Table::Index::UNKNOWN_INDEX:
      keyinfo->algorithm= HA_KEY_ALG_UNDEF;
      break;
    case message::Table::Index::BTREE:
      keyinfo->algorithm= HA_KEY_ALG_BTREE;
      break;
    case message::Table::Index::HASH:
      keyinfo->algorithm= HA_KEY_ALG_HASH;
      break;

    default:
      /* TODO: suitable warning ? */
      keyinfo->algorithm= HA_KEY_ALG_UNDEF;
      break;
    }

    keyinfo->key_length= indx.key_length();

    keyinfo->key_parts= indx.index_part_size();

    keyinfo->key_part= key_part;
    keyinfo->rec_per_key= rec_per_key;

    for (unsigned int partnr= 0;
         partnr < keyinfo->key_parts;
         partnr++, key_part++)
    {
      message::Table::Index::IndexPart part;
      part= indx.index_part(partnr);

      *rec_per_key++= 0;

      key_part->field= NULL;
      key_part->fieldnr= part.fieldnr() + 1; // start from 1.
      key_part->null_bit= 0;
      /* key_part->null_offset is only set if null_bit (see later) */
      /* key_part->key_type= */ /* I *THINK* this may be okay.... */
      /* key_part->type ???? */
      key_part->key_part_flag= 0;
      if (part.has_in_reverse_order())
        key_part->key_part_flag= part.in_reverse_order()? HA_REVERSE_SORT : 0;

      key_part->length= part.compare_length();

      key_part->store_length= key_part->length;

      /* key_part->offset is set later */
      key_part->key_type= part.key_type();
    }

    if (! indx.has_comment())
    {
      keyinfo->comment.length= 0;
      keyinfo->comment.str= NULL;
    }
    else
    {
      keyinfo->flags|= HA_USES_COMMENT;
      keyinfo->comment.length= indx.comment().length();
      keyinfo->comment.str= share->strmake_root(indx.comment().c_str(), keyinfo->comment.length);
    }

    keyinfo->name= share->strmake_root(indx.name().c_str(), indx.name().length());

    addKeyName(string(keyinfo->name, indx.name().length()));
  }

  share->keys_for_keyread.reset();
  set_prefix(share->keys_in_use, share->keys);

  share->fields= table.field_size();

  share->field= (Field**) share->alloc_root(((share->fields+1) * sizeof(Field*)));
  share->field[share->fields]= NULL;

  uint32_t local_null_fields= 0;
  share->reclength= 0;

  vector<uint32_t> field_offsets;
  vector<uint32_t> field_pack_length;

  field_offsets.resize(share->fields);
  field_pack_length.resize(share->fields);

  uint32_t interval_count= 0;
  uint32_t interval_parts= 0;

  uint32_t stored_columns_reclength= 0;

  for (unsigned int fieldnr= 0; fieldnr < share->fields; fieldnr++)
  {
    message::Table::Field pfield= table.field(fieldnr);
    if (pfield.constraints().is_nullable())
      local_null_fields++;

    enum_field_types drizzle_field_type=
      proto_field_type_to_drizzle_type(pfield.type());

    field_offsets[fieldnr]= stored_columns_reclength;

    /* the below switch is very similar to
      CreateField::create_length_to_internal_length in field.cc
      (which should one day be replace by just this code)
    */
    switch(drizzle_field_type)
    {
    case DRIZZLE_TYPE_BLOB:
    case DRIZZLE_TYPE_VARCHAR:
      {
        message::Table::Field::StringFieldOptions field_options= pfield.string_options();

        const CHARSET_INFO *cs= get_charset(field_options.has_collation_id() ?
                                            field_options.collation_id() : 0);

        if (! cs)
          cs= default_charset_info;

        field_pack_length[fieldnr]= calc_pack_length(drizzle_field_type,
                                                     field_options.length() * cs->mbmaxlen);
      }
      break;
    case DRIZZLE_TYPE_ENUM:
      {
        message::Table::Field::EnumerationValues field_options= pfield.enumeration_values();

        field_pack_length[fieldnr]=
          get_enum_pack_length(field_options.field_value_size());

        interval_count++;
        interval_parts+= field_options.field_value_size();
      }
      break;
    case DRIZZLE_TYPE_DECIMAL:
      {
        message::Table::Field::NumericFieldOptions fo= pfield.numeric_options();

        field_pack_length[fieldnr]= my_decimal_get_binary_size(fo.precision(), fo.scale());
      }
      break;
    default:
      /* Zero is okay here as length is fixed for other types. */
      field_pack_length[fieldnr]= calc_pack_length(drizzle_field_type, 0);
    }

    share->reclength+= field_pack_length[fieldnr];
    stored_columns_reclength+= field_pack_length[fieldnr];
  }

  /* data_offset added to stored_rec_length later */
  share->stored_rec_length= stored_columns_reclength;

  share->null_fields= local_null_fields;

  ulong null_bits= local_null_fields;
  if (! table_options.pack_record())
    null_bits++;
  ulong data_offset= (null_bits + 7)/8;


  share->reclength+= data_offset;
  share->stored_rec_length+= data_offset;

  ulong local_rec_buff_length;

  local_rec_buff_length= ALIGN_SIZE(share->reclength + 1);
  share->rec_buff_length= local_rec_buff_length;

  unsigned char* record= NULL;

  if (! (record= (unsigned char *) share->alloc_root(local_rec_buff_length)))
    abort();

  memset(record, 0, local_rec_buff_length);

  int null_count= 0;

  if (! table_options.pack_record())
  {
    null_count++; // one bit for delete mark.
    *record|= 1;
  }

  share->default_values= record;

  if (interval_count)
  {
    share->intervals= (TYPELIB *) share->alloc_root(interval_count*sizeof(TYPELIB));
  }
  else
  {
    share->intervals= NULL;
  }

  /* Now fix the TYPELIBs for the intervals (enum values)
    and field names.
  */

  uint32_t interval_nr= 0;

  for (unsigned int fieldnr= 0; fieldnr < share->fields; fieldnr++)
  {
    message::Table::Field pfield= table.field(fieldnr);

    /* enum typelibs */
    if (pfield.type() != message::Table::Field::ENUM)
      continue;

    message::Table::Field::EnumerationValues field_options= pfield.enumeration_values();

    const CHARSET_INFO *charset= get_charset(field_options.has_collation_id() ?
                                             field_options.collation_id() : 0);

    if (! charset)
      charset= default_charset_info;

    TYPELIB *t= &(share->intervals[interval_nr]);

    t->type_names= (const char**)share->alloc_root((field_options.field_value_size() + 1) * sizeof(char*));

    t->type_lengths= (unsigned int*) share->alloc_root((field_options.field_value_size() + 1) * sizeof(unsigned int));

    t->type_names[field_options.field_value_size()]= NULL;
    t->type_lengths[field_options.field_value_size()]= 0;

    t->count= field_options.field_value_size();
    t->name= NULL;

    for (int n= 0; n < field_options.field_value_size(); n++)
    {
      t->type_names[n]= share->strmake_root(field_options.field_value(n).c_str(), field_options.field_value(n).length());

      /* 
       * Go ask the charset what the length is as for "" length=1
       * and there's stripping spaces or some other crack going on.
     */
      uint32_t lengthsp;
      lengthsp= charset->cset->lengthsp(charset,
                                        t->type_names[n],
                                        field_options.field_value(n).length());
      t->type_lengths[n]= lengthsp;
    }
    interval_nr++;
  }


  /* and read the fields */
  interval_nr= 0;

  bool use_hash= share->fields >= MAX_FIELDS_BEFORE_HASH;

  if (use_hash)
    use_hash= ! hash_init(&share->name_hash,
                          system_charset_info,
                          share->fields,
                          0,
                          0,
                          (hash_get_key) get_field_name,
                          0,
                          0);

  unsigned char* null_pos= record;;
  int null_bit_pos= (table_options.pack_record()) ? 0 : 1;

  for (unsigned int fieldnr= 0; fieldnr < share->fields; fieldnr++)
  {
    message::Table::Field pfield= table.field(fieldnr);

    enum column_format_type column_format= COLUMN_FORMAT_TYPE_DEFAULT;

    switch (pfield.format())
    {
    case message::Table::Field::DefaultFormat:
      column_format= COLUMN_FORMAT_TYPE_DEFAULT;
      break;
    case message::Table::Field::FixedFormat:
      column_format= COLUMN_FORMAT_TYPE_FIXED;
      break;
    case message::Table::Field::DynamicFormat:
      column_format= COLUMN_FORMAT_TYPE_DYNAMIC;
      break;
    default:
      assert(1);
    }

    Field::utype unireg_type= Field::NONE;

    if (pfield.has_numeric_options() &&
        pfield.numeric_options().is_autoincrement())
    {
      unireg_type= Field::NEXT_NUMBER;
    }

    if (pfield.has_options() &&
        pfield.options().has_default_value() &&
        pfield.options().default_value().compare("NOW()") == 0)
    {
      if (pfield.options().has_update_value() &&
          pfield.options().update_value().compare("NOW()") == 0)
      {
        unireg_type= Field::TIMESTAMP_DNUN_FIELD;
      }
      else if (! pfield.options().has_update_value())
      {
        unireg_type= Field::TIMESTAMP_DN_FIELD;
      }
      else
        assert(1); // Invalid update value.
    }
    else if (pfield.has_options() &&
             pfield.options().has_update_value() &&
             pfield.options().update_value().compare("NOW()") == 0)
    {
      unireg_type= Field::TIMESTAMP_UN_FIELD;
    }

    LEX_STRING comment;
    if (!pfield.has_comment())
    {
      comment.str= (char*)"";
      comment.length= 0;
    }
    else
    {
      size_t len= pfield.comment().length();
      const char* str= pfield.comment().c_str();

      comment.str= share->strmake_root(str, len);
      comment.length= len;
    }

    enum_field_types field_type;

    field_type= proto_field_type_to_drizzle_type(pfield.type());

    const CHARSET_INFO *charset= &my_charset_bin;

    if (field_type == DRIZZLE_TYPE_BLOB ||
        field_type == DRIZZLE_TYPE_VARCHAR)
    {
      message::Table::Field::StringFieldOptions field_options= pfield.string_options();

      charset= get_charset(field_options.has_collation_id() ?
                           field_options.collation_id() : 0);

      if (! charset)
        charset= default_charset_info;
    }

    if (field_type == DRIZZLE_TYPE_ENUM)
    {
      message::Table::Field::EnumerationValues field_options= pfield.enumeration_values();

      charset= get_charset(field_options.has_collation_id()?
                           field_options.collation_id() : 0);

      if (! charset)
        charset= default_charset_info;
    }

    uint8_t decimals= 0;
    if (field_type == DRIZZLE_TYPE_DECIMAL
        || field_type == DRIZZLE_TYPE_DOUBLE)
    {
      message::Table::Field::NumericFieldOptions fo= pfield.numeric_options();

      if (! pfield.has_numeric_options() || ! fo.has_scale())
      {
        /*
          We don't write the default to table proto so
          if no decimals specified for DOUBLE, we use the default.
        */
        decimals= NOT_FIXED_DEC;
      }
      else
      {
        if (fo.scale() > DECIMAL_MAX_SCALE)
        {
          local_error= 4;

          return local_error;
        }
        decimals= static_cast<uint8_t>(fo.scale());
      }
    }

    Item *default_value= NULL;

    if (pfield.options().has_default_value() ||
        pfield.options().has_default_null()  ||
        pfield.options().has_default_bin_value())
    {
      default_value= default_value_item(field_type,
                                        charset,
                                        pfield.options().default_null(),
                                        &pfield.options().default_value(),
                                        &pfield.options().default_bin_value());
    }


    Table temp_table; /* Use this so that BLOB DEFAULT '' works */
    memset(&temp_table, 0, sizeof(temp_table));
    temp_table.s= share;
    temp_table.in_use= &session;
    temp_table.s->db_low_byte_first= true; //Cursor->low_byte_first();
    temp_table.s->blob_ptr_size= portable_sizeof_char_ptr;

    uint32_t field_length= 0; //Assignment is for compiler complaint.

    switch (field_type)
    {
    case DRIZZLE_TYPE_BLOB:
    case DRIZZLE_TYPE_VARCHAR:
      {
        message::Table::Field::StringFieldOptions field_options= pfield.string_options();

        charset= get_charset(field_options.has_collation_id() ?
                             field_options.collation_id() : 0);

        if (! charset)
          charset= default_charset_info;

        field_length= field_options.length() * charset->mbmaxlen;
      }
      break;
    case DRIZZLE_TYPE_DOUBLE:
      {
        message::Table::Field::NumericFieldOptions fo= pfield.numeric_options();
        if (!fo.has_precision() && !fo.has_scale())
        {
          field_length= DBL_DIG+7;
        }
        else
        {
          field_length= fo.precision();
        }
        if (field_length < decimals &&
            decimals != NOT_FIXED_DEC)
        {
          my_error(ER_M_BIGGER_THAN_D, MYF(0), pfield.name().c_str());
          local_error= 1;

          return local_error;
        }
        break;
      }
    case DRIZZLE_TYPE_DECIMAL:
      {
        message::Table::Field::NumericFieldOptions fo= pfield.numeric_options();

        field_length= my_decimal_precision_to_length(fo.precision(), fo.scale(),
                                                     false);
        break;
      }
    case DRIZZLE_TYPE_TIMESTAMP:
    case DRIZZLE_TYPE_DATETIME:
      field_length= DateTime::MAX_STRING_LENGTH;
      break;
    case DRIZZLE_TYPE_DATE:
      field_length= Date::MAX_STRING_LENGTH;
      break;
    case DRIZZLE_TYPE_ENUM:
      {
        field_length= 0;

        message::Table::Field::EnumerationValues fo= pfield.enumeration_values();

        for (int valnr= 0; valnr < fo.field_value_size(); valnr++)
        {
          if (fo.field_value(valnr).length() > field_length)
          {
            field_length= charset->cset->numchars(charset,
                                                  fo.field_value(valnr).c_str(),
                                                  fo.field_value(valnr).c_str()
                                                  + fo.field_value(valnr).length())
              * charset->mbmaxlen;
          }
        }
      }
      break;
    case DRIZZLE_TYPE_LONG:
      {
        uint32_t sign_len= pfield.constraints().is_unsigned() ? 0 : 1;
        field_length= MAX_INT_WIDTH+sign_len;
      }
      break;
    case DRIZZLE_TYPE_LONGLONG:
      field_length= MAX_BIGINT_WIDTH;
      break;
    case DRIZZLE_TYPE_NULL:
      abort(); // Programming error
    }

    Field* f= share->make_field(record + field_offsets[fieldnr] + data_offset,
                                field_length,
                                pfield.constraints().is_nullable(),
                                null_pos,
                                null_bit_pos,
                                decimals,
                                field_type,
                                charset,
                                (Field::utype) MTYP_TYPENR(unireg_type),
                                ((field_type == DRIZZLE_TYPE_ENUM) ?
                                 share->intervals + (interval_nr++)
                                 : (TYPELIB*) 0),
                                getTableProto()->field(fieldnr).name().c_str());

    share->field[fieldnr]= f;

    f->init(&temp_table); /* blob default values need table obj */

    if (! (f->flags & NOT_NULL_FLAG))
    {
      *f->null_ptr|= f->null_bit;
      if (! (null_bit_pos= (null_bit_pos + 1) & 7)) /* @TODO Ugh. */
        null_pos++;
      null_count++;
    }

    if (default_value)
    {
      enum_check_fields old_count_cuted_fields= session.count_cuted_fields;
      session.count_cuted_fields= CHECK_FIELD_WARN;
      int res= default_value->save_in_field(f, 1);
      session.count_cuted_fields= old_count_cuted_fields;
      if (res != 0 && res != 3) /* @TODO Huh? */
      {
        my_error(ER_INVALID_DEFAULT, MYF(0), f->field_name);
        local_error= 1;

        return local_error;
      }
    }
    else if (f->real_type() == DRIZZLE_TYPE_ENUM &&
             (f->flags & NOT_NULL_FLAG))
    {
      f->set_notnull();
      f->store((int64_t) 1, true);
    }
    else
    {
      f->reset();
    }

    /* hack to undo f->init() */
    f->table= NULL;
    f->orig_table= NULL;

    f->field_index= fieldnr;
    f->comment= comment;
    if (! default_value &&
        ! (f->unireg_check==Field::NEXT_NUMBER) &&
        (f->flags & NOT_NULL_FLAG) &&
        (f->real_type() != DRIZZLE_TYPE_TIMESTAMP))
    {
      f->flags|= NO_DEFAULT_VALUE_FLAG;
    }

    if (f->unireg_check == Field::NEXT_NUMBER)
      share->found_next_number_field= &(share->field[fieldnr]);

    if (share->timestamp_field == f)
      share->timestamp_field_offset= fieldnr;

    if (use_hash) /* supposedly this never fails... but comments lie */
      (void) my_hash_insert(&share->name_hash,
                            (unsigned char*)&(share->field[fieldnr]));

  }

  keyinfo= share->key_info;
  for (unsigned int keynr= 0; keynr < share->keys; keynr++, keyinfo++)
  {
    key_part= keyinfo->key_part;

    for (unsigned int partnr= 0;
         partnr < keyinfo->key_parts;
         partnr++, key_part++)
    {
      /* 
       * Fix up key_part->offset by adding data_offset.
       * We really should compute offset as well.
       * But at least this way we are a little better.
     */
      key_part->offset= field_offsets[key_part->fieldnr-1] + data_offset;
    }
  }

  /*
    We need to set the unused bits to 1. If the number of bits is a multiple
    of 8 there are no unused bits.
  */

  if (null_count & 7)
    *(record + null_count / 8)|= ~(((unsigned char) 1 << (null_count & 7)) - 1);

  share->null_bytes= (null_pos - (unsigned char*) record + (null_bit_pos + 7) / 8);

  share->last_null_bit_pos= null_bit_pos;

  /* Fix key stuff */
  if (share->key_parts)
  {
    uint32_t local_primary_key= 0;
    doesKeyNameExist("PRIMARY", local_primary_key);

    keyinfo= share->key_info;
    key_part= keyinfo->key_part;

    for (uint32_t key= 0; key < share->keys; key++,keyinfo++)
    {
      uint32_t usable_parts= 0;

      if (local_primary_key >= MAX_KEY && (keyinfo->flags & HA_NOSAME))
      {
        /*
          If the UNIQUE key doesn't have NULL columns and is not a part key
          declare this as a primary key.
        */
        local_primary_key=key;
        for (uint32_t i= 0; i < keyinfo->key_parts; i++)
        {
          uint32_t fieldnr= key_part[i].fieldnr;
          if (! fieldnr ||
              share->field[fieldnr-1]->null_ptr ||
              share->field[fieldnr-1]->key_length() != key_part[i].length)
          {
            local_primary_key= MAX_KEY; // Can't be used
            break;
          }
        }
      }

      for (uint32_t i= 0 ; i < keyinfo->key_parts ; key_part++,i++)
      {
        Field *local_field;
        if (! key_part->fieldnr)
        {
          return ENOMEM;
        }
        local_field= key_part->field= share->field[key_part->fieldnr-1];
        key_part->type= local_field->key_type();
        if (local_field->null_ptr)
        {
          key_part->null_offset=(uint32_t) ((unsigned char*) local_field->null_ptr - share->default_values);
          key_part->null_bit= local_field->null_bit;
          key_part->store_length+=HA_KEY_NULL_LENGTH;
          keyinfo->flags|=HA_NULL_PART_KEY;
          keyinfo->extra_length+= HA_KEY_NULL_LENGTH;
          keyinfo->key_length+= HA_KEY_NULL_LENGTH;
        }
        if (local_field->type() == DRIZZLE_TYPE_BLOB ||
            local_field->real_type() == DRIZZLE_TYPE_VARCHAR)
        {
          if (local_field->type() == DRIZZLE_TYPE_BLOB)
            key_part->key_part_flag|= HA_BLOB_PART;
          else
            key_part->key_part_flag|= HA_VAR_LENGTH_PART;
          keyinfo->extra_length+=HA_KEY_BLOB_LENGTH;
          key_part->store_length+=HA_KEY_BLOB_LENGTH;
          keyinfo->key_length+= HA_KEY_BLOB_LENGTH;
        }
        if (i == 0 && key != local_primary_key)
          local_field->flags |= (((keyinfo->flags & HA_NOSAME) &&
                                  (keyinfo->key_parts == 1)) ?
                                 UNIQUE_KEY_FLAG : MULTIPLE_KEY_FLAG);
        if (i == 0)
          local_field->key_start.set(key);
        if (local_field->key_length() == key_part->length &&
            !(local_field->flags & BLOB_FLAG))
        {
          enum ha_key_alg algo= share->key_info[key].algorithm;
          if (share->db_type()->index_flags(algo) & HA_KEYREAD_ONLY)
          {
            share->keys_for_keyread.set(key);
            local_field->part_of_key.set(key);
            local_field->part_of_key_not_clustered.set(key);
          }
          if (share->db_type()->index_flags(algo) & HA_READ_ORDER)
            local_field->part_of_sortkey.set(key);
        }
        if (!(key_part->key_part_flag & HA_REVERSE_SORT) &&
            usable_parts == i)
          usable_parts++;			// For FILESORT
        local_field->flags|= PART_KEY_FLAG;
        if (key == local_primary_key)
        {
          local_field->flags|= PRI_KEY_FLAG;
          /*
            If this field is part of the primary key and all keys contains
            the primary key, then we can use any key to find this column
          */
          if (share->storage_engine->check_flag(HTON_BIT_PRIMARY_KEY_IN_READ_INDEX))
          {
            local_field->part_of_key= share->keys_in_use;
            if (local_field->part_of_sortkey.test(key))
              local_field->part_of_sortkey= share->keys_in_use;
          }
        }
        if (local_field->key_length() != key_part->length)
        {
          key_part->key_part_flag|= HA_PART_KEY_SEG;
        }
      }
      keyinfo->usable_key_parts= usable_parts; // Filesort

      set_if_bigger(share->max_key_length,keyinfo->key_length+
                    keyinfo->key_parts);
      share->total_key_length+= keyinfo->key_length;

      if (keyinfo->flags & HA_NOSAME)
      {
        set_if_bigger(share->max_unique_length,keyinfo->key_length);
      }
    }
    if (local_primary_key < MAX_KEY &&
        (share->keys_in_use.test(local_primary_key)))
    {
      share->primary_key= local_primary_key;
      /*
        If we are using an integer as the primary key then allow the user to
        refer to it as '_rowid'
      */
      if (share->key_info[local_primary_key].key_parts == 1)
      {
        Field *local_field= share->key_info[local_primary_key].key_part[0].field;
        if (local_field && local_field->result_type() == INT_RESULT)
        {
          /* note that fieldnr here (and rowid_field_offset) starts from 1 */
          share->rowid_field_offset= (share->key_info[local_primary_key].key_part[0].
                                      fieldnr);
        }
      }
    }
    else
    {
      share->primary_key = MAX_KEY; // we do not have a primary key
    }
  }
  else
  {
    share->primary_key= MAX_KEY;
  }

  if (share->found_next_number_field)
  {
    Field *reg_field= *share->found_next_number_field;
    if ((int) (share->next_number_index= (uint32_t)
               find_ref_key(share->key_info, share->keys,
                            share->default_values, reg_field,
                            &share->next_number_key_offset,
                            &share->next_number_keypart)) < 0)
    {
      /* Wrong field definition */
      local_error= 4;

      return local_error;
    }
    else
    {
      reg_field->flags |= AUTO_INCREMENT_FLAG;
    }
  }

  if (share->blob_fields)
  {
    Field **ptr;
    uint32_t k, *save;

    /* Store offsets to blob fields to find them fast */
    if (!(share->blob_field= save=
          (uint*) share->alloc_root((uint32_t) (share->blob_fields* sizeof(uint32_t)))))
    {
      return local_error;
    }
    for (k= 0, ptr= share->field ; *ptr ; ptr++, k++)
    {
      if ((*ptr)->flags & BLOB_FLAG)
        (*save++)= k;
    }
  }

  share->db_low_byte_first= true; // @todo Question this.
  share->column_bitmap_size= bitmap_buffer_size(share->fields);

  my_bitmap_map *bitmaps;

  if (!(bitmaps= (my_bitmap_map*) share->alloc_root(share->column_bitmap_size)))
  { }
  else
  {
    share->all_set.init(bitmaps, share->fields);
    share->all_set.setAll();

    return (0);
  }

  return local_error;
}

int TableShare::parse_table_proto(Session& session, message::Table &table)
{
  int local_error= inner_parse_table_proto(session, table);

  if (not local_error)
    return 0;

  error= local_error;
  open_errno= errno;
  errarg= 0;
  hash_free(&name_hash);
  open_table_error(local_error, open_errno, 0);

  return local_error;
}


/*
  Read table definition from a binary / text based .frm cursor

  SYNOPSIS
  open_table_def()
  session		Thread Cursor
  share		Fill this with table definition

  NOTES
  This function is called when the table definition is not cached in
  table_def_cache
  The data is returned in 'share', which is alloced by
  alloc_table_share().. The code assumes that share is initialized.

  RETURN VALUES
  0	ok
  1	Error (see open_table_error)
  2    Error (see open_table_error)
  3    Wrong data in .frm cursor
  4    Error (see open_table_error)
  5    Error (see open_table_error: charset unavailable)
  6    Unknown .frm version
*/

int TableShare::open_table_def(Session& session, TableIdentifier &identifier)
{
  int local_error;
  bool error_given;

  local_error= 1;
  error_given= 0;

  message::Table table;

  local_error= plugin::StorageEngine::getTableDefinition(session, identifier, table);

  if (local_error != EEXIST)
  {
    if (local_error > 0)
    {
      errno= local_error;
      local_error= 1;
    }
    else
    {
      if (not table.IsInitialized())
      {
        local_error= 4;
      }
    }
    goto err_not_open;
  }

  local_error= parse_table_proto(session, table);

  setTableCategory(TABLE_CATEGORY_USER);

err_not_open:
  if (local_error && !error_given)
  {
    error= local_error;
    open_table_error(error, (open_errno= errno), 0);
  }

  return(error);
}


/*
  Open a table based on a TableShare

  SYNOPSIS
  open_table_from_share()
  session			Thread Cursor
  share		Table definition
  alias       	Alias for table
  db_stat		open flags (for example HA_OPEN_KEYFILE|
  HA_OPEN_RNDFILE..) can be 0 (example in
  ha_example_table)
  ha_open_flags	HA_OPEN_ABORT_IF_LOCKED etc..
  outparam       	result table

  RETURN VALUES
  0	ok
  1	Error (see open_table_error)
  2    Error (see open_table_error)
  3    Wrong data in .frm cursor
  4    Error (see open_table_error)
  5    Error (see open_table_error: charset unavailable)
  7    Table definition has changed in engine
*/

int TableShare::open_table_from_share(Session *session, const char *alias,
                                      uint32_t db_stat, uint32_t ha_open_flags,
                                      Table &outparam)
{
  int local_error;
  uint32_t records, bitmap_size;
  bool error_reported= false;
  unsigned char *record, *bitmaps;
  Field **field_ptr;

  /* Parsing of partitioning information from .frm needs session->lex set up. */
  assert(session->lex->is_lex_started);

  local_error= 1;
  outparam.resetTable(session, this, db_stat);


  if (not (outparam.alias= strdup(alias)))
    goto err;

  /* Allocate Cursor */
  if (not (outparam.cursor= db_type()->getCursor(*this, &outparam.mem_root)))
    goto err;

  local_error= 4;
  records= 0;
  if ((db_stat & HA_OPEN_KEYFILE))
    records=1;

  records++;

  if (!(record= (unsigned char*) outparam.mem_root.alloc_root(rec_buff_length * records)))
    goto err;

  if (records == 0)
  {
    /* We are probably in hard repair, and the buffers should not be used */
    outparam.record[0]= outparam.record[1]= default_values;
  }
  else
  {
    outparam.record[0]= record;
    if (records > 1)
      outparam.record[1]= record+ rec_buff_length;
    else
      outparam.record[1]= outparam.record[0];   // Safety
  }

#ifdef HAVE_purify
  /*
    We need this because when we read var-length rows, we are not updating
    bytes after end of varchar
  */
  if (records > 1)
  {
    memcpy(outparam.record[0], default_values, rec_buff_length);
    memcpy(outparam.record[1], default_values, null_bytes);
    if (records > 2)
      memcpy(outparam.record[1], default_values, rec_buff_length);
  }
#endif
  if (records > 1)
  {
    memcpy(outparam.record[1], default_values, null_bytes);
  }

  if (!(field_ptr = (Field **) outparam.mem_root.alloc_root( (uint32_t) ((fields+1)* sizeof(Field*)))))
  {
    goto err;
  }

  outparam.field= field_ptr;

  record= (unsigned char*) outparam.record[0]-1;	/* Fieldstart = 1 */

  outparam.null_flags= (unsigned char*) record+1;

  /* Setup copy of fields from share, but use the right alias and record */
  for (uint32_t i= 0 ; i < fields; i++, field_ptr++)
  {
    if (!((*field_ptr)= field[i]->clone(&outparam.mem_root, &outparam)))
      goto err;
  }
  (*field_ptr)= 0;                              // End marker

  if (found_next_number_field)
    outparam.found_next_number_field=
      outparam.field[(uint32_t) (found_next_number_field - field)];
  if (timestamp_field)
    outparam.timestamp_field= (Field_timestamp*) outparam.field[timestamp_field_offset];


  /* Fix key->name and key_part->field */
  if (key_parts)
  {
    KEY	*local_key_info, *key_info_end;
    KeyPartInfo *key_part;
    uint32_t n_length;
    n_length= keys*sizeof(KEY) + key_parts*sizeof(KeyPartInfo);
    if (!(local_key_info= (KEY*) outparam.mem_root.alloc_root(n_length)))
      goto err;
    outparam.key_info= local_key_info;
    key_part= (reinterpret_cast<KeyPartInfo*> (local_key_info+keys));

    memcpy(local_key_info, key_info, sizeof(*local_key_info)*keys);
    memcpy(key_part, key_info[0].key_part, (sizeof(*key_part) *
                                            key_parts));

    for (key_info_end= local_key_info + keys ;
         local_key_info < key_info_end ;
         local_key_info++)
    {
      KeyPartInfo *key_part_end;

      local_key_info->table= &outparam;
      local_key_info->key_part= key_part;

      for (key_part_end= key_part+ local_key_info->key_parts ;
           key_part < key_part_end ;
           key_part++)
      {
        Field *local_field= key_part->field= outparam.field[key_part->fieldnr-1];

        if (local_field->key_length() != key_part->length &&
            !(local_field->flags & BLOB_FLAG))
        {
          /*
            We are using only a prefix of the column as a key:
            Create a new field for the key part that matches the index
          */
          local_field= key_part->field= local_field->new_field(&outparam.mem_root, &outparam, 0);
          local_field->field_length= key_part->length;
        }
      }
    }
  }

  /* Allocate bitmaps */

  bitmap_size= column_bitmap_size;
  if (!(bitmaps= (unsigned char*) outparam.mem_root.alloc_root(bitmap_size*3)))
  {
    goto err;
  }
  outparam.def_read_set.init((my_bitmap_map*) bitmaps, fields);
  outparam.def_write_set.init((my_bitmap_map*) (bitmaps+bitmap_size), fields);
  outparam.tmp_set.init((my_bitmap_map*) (bitmaps+bitmap_size*2), fields);
  outparam.default_column_bitmaps();

  /* The table struct is now initialized;  Open the table */
  local_error= 2;
  if (db_stat)
  {
    int ha_err;
    if ((ha_err= (outparam.cursor->
                  ha_open(&outparam, getNormalizedPath(),
                          (db_stat & HA_READ_ONLY ? O_RDONLY : O_RDWR),
                          (db_stat & HA_OPEN_TEMPORARY ? HA_OPEN_TMP_TABLE :
                           (db_stat & HA_WAIT_IF_LOCKED) ?  HA_OPEN_WAIT_IF_LOCKED :
                           (db_stat & (HA_ABORT_IF_LOCKED | HA_GET_INFO)) ?
                           HA_OPEN_ABORT_IF_LOCKED :
                           HA_OPEN_IGNORE_IF_LOCKED) | ha_open_flags))))
    {
      switch (ha_err)
      {
      case HA_ERR_NO_SUCH_TABLE:
        /*
          The table did not exists in storage engine, use same error message
          as if the .frm cursor didn't exist
        */
        local_error= 1;
        errno= ENOENT;
        break;
      case EMFILE:
        /*
          Too many files opened, use same error message as if the .frm
          cursor can't open
        */
        local_error= 1;
        errno= EMFILE;
        break;
      default:
        outparam.print_error(ha_err, MYF(0));
        error_reported= true;
        if (ha_err == HA_ERR_TABLE_DEF_CHANGED)
          local_error= 7;
        break;
      }
      goto err;
    }
  }

#if defined(HAVE_purify)
  memset(bitmaps, 0, bitmap_size*3);
#endif

  return 0;

err:
  if (!error_reported)
    open_table_error(local_error, errno, 0);

  delete outparam.cursor;
  outparam.cursor= 0;				// For easier error checking
  outparam.db_stat= 0;
  outparam.mem_root.free_root(MYF(0));       // Safe to call on zeroed root
  free((char*) outparam.alias);
  return (local_error);
}

/* error message when opening a form cursor */
void TableShare::open_table_error(int pass_error, int db_errno, int pass_errarg)
{
  int err_no;
  char buff[FN_REFLEN];
  myf errortype= ME_ERROR+ME_WAITTANG;

  switch (pass_error) {
  case 7:
  case 1:
    if (db_errno == ENOENT)
    {
      my_error(ER_NO_SUCH_TABLE, MYF(0), db.str, table_name.str);
    }
    else
    {
      snprintf(buff, sizeof(buff), "%s",normalized_path.str);
      my_error((db_errno == EMFILE) ? ER_CANT_OPEN_FILE : ER_FILE_NOT_FOUND,
               errortype, buff, db_errno);
    }
    break;
  case 2:
    {
      Cursor *cursor= 0;
      const char *datext= "";

      if (db_type() != NULL)
      {
        if ((cursor= db_type()->getCursor(*this, current_session->mem_root)))
        {
          if (!(datext= *db_type()->bas_ext()))
            datext= "";
        }
      }
      err_no= (db_errno == ENOENT) ? ER_FILE_NOT_FOUND : (db_errno == EAGAIN) ?
        ER_FILE_USED : ER_CANT_OPEN_FILE;
      snprintf(buff, sizeof(buff), "%s%s", normalized_path.str,datext);
      my_error(err_no,errortype, buff, db_errno);
      delete cursor;
      break;
    }
  case 5:
    {
      const char *csname= get_charset_name((uint32_t) pass_errarg);
      char tmp[10];
      if (!csname || csname[0] =='?')
      {
        snprintf(tmp, sizeof(tmp), "#%d", pass_errarg);
        csname= tmp;
      }
      my_printf_error(ER_UNKNOWN_COLLATION,
                      _("Unknown collation '%s' in table '%-.64s' definition"),
                      MYF(0), csname, table_name.str);
      break;
    }
  case 6:
    snprintf(buff, sizeof(buff), "%s", normalized_path.str);
    my_printf_error(ER_NOT_FORM_FILE,
                    _("Table '%-.64s' was created with a different version "
                      "of Drizzle and cannot be read"),
                    MYF(0), buff);
    break;
  case 8:
    break;
  default:				/* Better wrong error than none */
  case 4:
    snprintf(buff, sizeof(buff), "%s", normalized_path.str);
    my_error(ER_NOT_FORM_FILE, errortype, buff, 0);
    break;
  }
  return;
} /* open_table_error */

Field *TableShare::make_field(memory::Root *root,
                              unsigned char *ptr,
                              uint32_t field_length,
                              bool is_nullable,
                              unsigned char *null_pos,
                              unsigned char null_bit,
                              uint8_t decimals,
                              enum_field_types field_type,
                              const CHARSET_INFO * field_charset,
                              Field::utype unireg_check,
                              TYPELIB *interval,
                              const char *field_name)
{
  TableShare *share= this;
  assert(root);

  if (! is_nullable)
  {
    null_pos=0;
    null_bit=0;
  }
  else
  {
    null_bit= ((unsigned char) 1) << null_bit;
  }

  switch (field_type) 
  {
  case DRIZZLE_TYPE_DATE:
  case DRIZZLE_TYPE_DATETIME:
  case DRIZZLE_TYPE_TIMESTAMP:
    field_charset= &my_charset_bin;
  default: break;
  }

  switch (field_type)
  {
  case DRIZZLE_TYPE_ENUM:
    return new (root) Field_enum(ptr,
                                 field_length,
                                 null_pos,
                                 null_bit,
                                 field_name,
                                 get_enum_pack_length(interval->count),
                                 interval,
                                 field_charset);
  case DRIZZLE_TYPE_VARCHAR:
    return new (root) Field_varstring(ptr,field_length,
                                      HA_VARCHAR_PACKLENGTH(field_length),
                                      null_pos,null_bit,
                                      field_name,
                                      share,
                                      field_charset);
  case DRIZZLE_TYPE_BLOB:
    return new (root) Field_blob(ptr,
                                 null_pos,
                                 null_bit,
                                 field_name,
                                 share,
                                 calc_pack_length(DRIZZLE_TYPE_LONG, 0),
                                 field_charset);
  case DRIZZLE_TYPE_DECIMAL:
    return new (root) Field_decimal(ptr,
                                    field_length,
                                    null_pos,
                                    null_bit,
                                    unireg_check,
                                    field_name,
                                    decimals,
                                    false,
                                    false /* is_unsigned */);
  case DRIZZLE_TYPE_DOUBLE:
    return new (root) Field_double(ptr,
                                   field_length,
                                   null_pos,
                                   null_bit,
                                   unireg_check,
                                   field_name,
                                   decimals,
                                   false,
                                   false /* is_unsigned */);
  case DRIZZLE_TYPE_LONG:
    return new (root) Field_long(ptr,
                                 field_length,
                                 null_pos,
                                 null_bit,
                                 unireg_check,
                                 field_name,
                                 false,
                                 false /* is_unsigned */);
  case DRIZZLE_TYPE_LONGLONG:
    return new (root) Field_int64_t(ptr,
                                    field_length,
                                    null_pos,
                                    null_bit,
                                    unireg_check,
                                    field_name,
                                    false,
                                    false /* is_unsigned */);
  case DRIZZLE_TYPE_TIMESTAMP:
    return new (root) Field_timestamp(ptr,
                                      field_length,
                                      null_pos,
                                      null_bit,
                                      unireg_check,
                                      field_name,
                                      share,
                                      field_charset);
  case DRIZZLE_TYPE_DATE:
    return new (root) Field_date(ptr,
                                 null_pos,
                                 null_bit,
                                 field_name,
                                 field_charset);
  case DRIZZLE_TYPE_DATETIME:
    return new (root) Field_datetime(ptr,
                                     null_pos,
                                     null_bit,
                                     field_name,
                                     field_charset);
  case DRIZZLE_TYPE_NULL:
    return new (root) Field_null(ptr,
                                 field_length,
                                 field_name,
                                 field_charset);
  default: // Impossible (Wrong version)
    break;
  }
  return 0;
}


} /* namespace drizzled */
