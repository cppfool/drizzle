/* -*- mode: c++; c-basic-offset: 2; indent-tabs-mode: nil; -*-
 *  vim:expandtab:shiftwidth=2:tabstop=2:smarttab:
 *
 *  Copyright (C) 2010 Brian Aker
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

#include "config.h"

#include "drizzled/session.h"

#include "drizzled/global_charset_info.h"
#include "drizzled/charset.h"

#include "drizzled/plugin/storage_engine.h"
#include "drizzled/plugin/authorization.h"

using namespace std;

namespace drizzled
{

namespace plugin
{

class AddSchemaNames : 
  public unary_function<StorageEngine *, void>
{
  SchemaIdentifierList &schemas;

public:

  AddSchemaNames(SchemaIdentifierList &of_names) :
    schemas(of_names)
  {
  }

  result_type operator() (argument_type engine)
  {
    engine->doGetSchemaIdentifiers(schemas);
  }
};

void StorageEngine::getSchemaIdentifiers(Session &session, SchemaIdentifierList &schemas)
{
  // Add hook here for engines to register schema.
  for_each(StorageEngine::getSchemaEngines().begin(), StorageEngine::getSchemaEngines().end(),
           AddSchemaNames(schemas));

  plugin::Authorization::pruneSchemaNames(session.getSecurityContext(), schemas);
}

class StorageEngineGetSchemaDefinition: public unary_function<StorageEngine *, bool>
{
  SchemaIdentifier &identifier;
  message::Schema &schema_proto;

public:
  StorageEngineGetSchemaDefinition(SchemaIdentifier &identifier_arg,
                                   message::Schema &schema_proto_arg) :
    identifier(identifier_arg),
    schema_proto(schema_proto_arg) 
  {
  }

  result_type operator() (argument_type engine)
  {
    return engine->doGetSchemaDefinition(identifier, schema_proto);
  }
};

/*
  Return value is "if parsed"
*/
bool StorageEngine::getSchemaDefinition(TableIdentifier &identifier, message::Schema &proto)
{
  return StorageEngine::getSchemaDefinition(identifier, proto);
}

bool StorageEngine::getSchemaDefinition(SchemaIdentifier &identifier, message::Schema &proto)
{
  proto.Clear();

  EngineVector::iterator iter=
    find_if(StorageEngine::getSchemaEngines().begin(), StorageEngine::getSchemaEngines().end(),
            StorageEngineGetSchemaDefinition(identifier, proto));

  if (iter != StorageEngine::getSchemaEngines().end())
  {
    return true;
  }

  return false;
}

bool StorageEngine::doesSchemaExist(SchemaIdentifier &identifier)
{
  message::Schema proto;

  return StorageEngine::getSchemaDefinition(identifier, proto);
}


const CHARSET_INFO *StorageEngine::getSchemaCollation(SchemaIdentifier &identifier)
{
  message::Schema schmema_proto;
  bool found;

  found= StorageEngine::getSchemaDefinition(identifier, schmema_proto);

  if (found && schmema_proto.has_collation())
  {
    const string buffer= schmema_proto.collation();
    const CHARSET_INFO* cs= get_charset_by_name(buffer.c_str());

    if (not cs)
    {
      errmsg_printf(ERRMSG_LVL_ERROR,
                    _("Error while loading database options: '%s':"), identifier.getSQLPath().c_str());
      errmsg_printf(ERRMSG_LVL_ERROR, ER(ER_UNKNOWN_COLLATION), buffer.c_str());

      return default_charset_info;
    }

    return cs;
  }

  return default_charset_info;
}

class CreateSchema : 
  public unary_function<StorageEngine *, void>
{
  const drizzled::message::Schema &schema_message;

public:

  CreateSchema(const drizzled::message::Schema &arg) :
    schema_message(arg)
  {
  }

  result_type operator() (argument_type engine)
  {
    // @todo eomeday check that at least one engine said "true"
    (void)engine->doCreateSchema(schema_message);
  }
};

bool StorageEngine::createSchema(const drizzled::message::Schema &schema_message)
{
  // Add hook here for engines to register schema.
  for_each(StorageEngine::getSchemaEngines().begin(), StorageEngine::getSchemaEngines().end(),
           CreateSchema(schema_message));

  return true;
}

class DropSchema : 
  public unary_function<StorageEngine *, void>
{
  uint64_t &success_count;
  SchemaIdentifier &identifier;

public:

  DropSchema(SchemaIdentifier &arg, uint64_t &count_arg) :
    success_count(count_arg),
    identifier(arg)
  {
  }

  result_type operator() (argument_type engine)
  {
    // @todo someday check that at least one engine said "true"
    bool success= engine->doDropSchema(identifier);

    if (success)
      success_count++;
  }
};

bool StorageEngine::dropSchema(SchemaIdentifier &identifier)
{
  uint64_t counter= 0;
  // Add hook here for engines to register schema.
  for_each(StorageEngine::getSchemaEngines().begin(), StorageEngine::getSchemaEngines().end(),
           DropSchema(identifier, counter));

  return counter ? true : false;
}

class AlterSchema : 
  public unary_function<StorageEngine *, void>
{
  uint64_t &success_count;
  const drizzled::message::Schema &schema_message;

public:

  AlterSchema(const drizzled::message::Schema &arg, uint64_t &count_arg) :
    success_count(count_arg),
    schema_message(arg)
  {
  }

  result_type operator() (argument_type engine)
  {
    // @todo eomeday check that at least one engine said "true"
    bool success= engine->doAlterSchema(schema_message);

    if (success)
      success_count++;
  }
};

bool StorageEngine::alterSchema(const drizzled::message::Schema &schema_message)
{
  uint64_t success_count= 0;

  for_each(StorageEngine::getSchemaEngines().begin(), StorageEngine::getSchemaEngines().end(),
           AlterSchema(schema_message, success_count));

  return success_count ? true : false;
}

} /* namespace plugin */
} /* namespace drizzled */