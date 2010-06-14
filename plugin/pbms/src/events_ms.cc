/*
 *  Copyright (C) 2010 PrimeBase Technologies GmbH, Germany
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
 *
 * Barry Leslie
 *
 * 2010-06-01
 */

#include "config.h"
#include <string>
#include <inttypes.h>

#include <drizzled/session.h>
#include <drizzled/field/blob.h>

#include "cslib/CSConfig.h"
#include "cslib/CSGlobal.h"
#include "cslib/CSStrUtil.h"
#include "cslib/CSThread.h"

#include "events_ms.h"
#include "parameters_ms.h"
#include "Engine_ms.h"

using namespace drizzled;
using namespace plugin;
using namespace std;


//==================================
// My table event observers: 
static bool insertRecord(TableEventData &data, unsigned char *new_row)
{
	Field_blob *field;
	unsigned char *blob_rec;
	char *blob_url, *possible_blob_url;
	char safe_url[PBMS_BLOB_URL_SIZE+1];
	PBMSBlobURLRec blob_url_buffer;
	size_t packlength, i, length, org_length;
	int32_t err;
	PBMSResultRec result;
	
	for (i= 0; i < data.table.sizeBlobFields(); i++) {
		field = data.table.getBlobFieldAt(i);

		// Get the blob record:
		blob_rec = new_row + field->offset(data.table.record[0]);
		packlength = field->pack_length() - data.table.getBlobPtrSize();

		length = field->get_length(blob_rec);
		memcpy(&possible_blob_url, blob_rec +packlength, sizeof(char*));
		org_length = field->get_length(blob_rec);
		
		// Signal PBMS to record a new reference to the BLOB.
		// If 'blob' is not a BLOB URL then it will be stored in the repositor as a new BLOB
		// and a reference to it will be created.
		if (MSEngine::couldBeURL(possible_blob_url, length) == false) {
			err = MSEngine::createBlob(data.table.getSchemaName(), data.table.getTableName(), possible_blob_url, length, &blob_url_buffer, &result);
			if (err) {
				// If it fails log the error and continue to try and release any other BLOBs in the row.
				fprintf(stderr, "PBMSEvents: createBlob(\"%s.%s\") error (%d):'%s'\n", 
					data.table.getSchemaName(), data.table.getTableName(), result.mr_code,  result.mr_message);
					
				return true;
			}				
			blob_url = blob_url_buffer.bu_data;
		} else {
			// The BLOB URL may not be null terminate, if so
			// then copy it to a safe buffer and terminate it.
			if (possible_blob_url[length]) {
				memcpy(safe_url, possible_blob_url, length);
				safe_url[length] = 0;
				blob_url = safe_url;
			} else
				blob_url = possible_blob_url;
		}
		
		// Signal PBMS to delete the reference to the BLOB.
		err = MSEngine::referenceBlob(data.table.getSchemaName(), data.table.getTableName(), &blob_url_buffer, blob_url, field->field_index, &result);
		if (err) {
			// If it fails log the error and continue to try and release any other BLOBs in the row.
			fprintf(stderr, "PBMSEvents: referenceBlob(\"%s.%s\", \"%s\" ) error (%d):'%s'\n", 
				data.table.getSchemaName(), data.table.getTableName(), blob_url, result.mr_code,  result.mr_message);
				
			return true;
		}
		
		// The URL is modified on insert so if the BLOB length changed reset it. 
		// This will happen if the BLOB data was replaced with a BLOB reference. 
		length = strlen(blob_url_buffer.bu_data)  +1;
		if ((length != org_length) || memcmp(blob_url_buffer.bu_data, possible_blob_url, length)) {
			char *blob = possible_blob_url; // This is the BLOB as the server currently sees it.
			
			if (length != org_length) {
				field->store_length(blob_rec, packlength, length);
			}
			
			if (length > org_length) {
				// This can only happen if the BLOB URL is actually larger than the BLOB itself.
				blob = (char *) data.session.alloc(length);
				memcpy(blob_rec+packlength, &blob, sizeof(char*));
			}			
			memcpy(blob, blob_url_buffer.bu_data, length);
		} 
	}

	return false;
}

//---
static bool deleteRecord(TableEventData &data, const unsigned char *old_row)
{
	Field_blob *field;
	const char *blob_rec;
	char *blob_url;
	size_t packlength, i, length;
	int32_t err;
	PBMSResultRec result;
	bool call_failed = false;
	
	for (i= 0; i < data.table.sizeBlobFields(); i++) {
		field = data.table.getBlobFieldAt(i);

		// Get the blob record:
		blob_rec = (char *)old_row + field->offset(data.table.record[0]);
		packlength = field->pack_length() - data.table.getBlobPtrSize();

		length = field->get_length((unsigned char *)blob_rec);
		memcpy(&blob_url, blob_rec +packlength, sizeof(char*));
		
		// Check to see if this is a valid URL.
		if (MSEngine::couldBeURL(blob_url, length)) {
		
			// The BLOB URL may not be null terminate, if so
			// then copy it to a safe buffer and terminate it.
			char safe_url[PBMS_BLOB_URL_SIZE+1];
			if (blob_url[length]) {
				memcpy(safe_url, blob_url, length);
				safe_url[length] = 0;
				blob_url = safe_url;
			}
			
			// Signal PBMS to delete the reference to the BLOB.
			err = MSEngine::dereferenceBlob(data.table.getSchemaName(), data.table.getTableName(), blob_url, &result);
			if (err) {
				// If it fails log the error and continue to try and release any other BLOBs in the row.
				fprintf(stderr, "PBMSEvents: dereferenceBlob(\"%s.%s\") error (%d):'%s'\n", 
					data.table.getSchemaName(), data.table.getTableName(), result.mr_code,  result.mr_message);
					
				call_failed = true;
			}
		}
	}

	return call_failed;
}

//---
static bool observeBeforeInsertRecord(BeforeInsertRecordEventData &data)
{
	if  (data.table.sizeBlobFields() == 0)
		return false;

	return insertRecord(data, data.row);
}

//---
static bool observeAfterInsertRecord(AfterInsertRecordEventData &data)
{
	if  (data.table.sizeBlobFields() > 0)
		MSEngine::callCompleted(data.err == 0);
	
	return false;
}

//---
static bool observeBeforeUpdateRecord(BeforeUpdateRecordEventData &data)
{
	if (data.table.sizeBlobFields() == 0)
		return false;

	// NOTE: The simple way to do this is to just do a delete followed by an insert.
	// But in the majority of cases this is probably a waste of time since the BLOB
	// will most likely NOT be the target of the update. What I need to do is check if
	// the BLOB was updated and then only delete/insert those BLOBs.
	//
	// But for now I will be lazy.
	if (deleteRecord(data, data.old_row))
		return true;
		
	return insertRecord(data, data.new_row);
}

//---
static bool observeAfterUpdateRecord(AfterUpdateRecordEventData &data)
{
	if  (data.table.sizeBlobFields() > 0)
		MSEngine::callCompleted(data.err == 0);
	
  return false;
}

//---
static bool observeAfterDeleteRecord(AfterDeleteRecordEventData &data)
{
	if ((data.err != 0) || (data.table.sizeBlobFields() == 0))
		return false;

	bool call_failed = deleteRecord(data, data.row);
	
	MSEngine::callCompleted(call_failed == false);

	return call_failed;
}

//==================================
// My session event observers: 
static bool observeAfterDropDatabase(AfterDropDatabaseEventData &data)
{
	PBMSResultRec result;
	if (data.err != 0)
		return false;

	if (MSEngine::dropDatabase(data.db.c_str(), &result) != 0) {
		fprintf(stderr, "PBMSEvents: dropDatabase(\"%s\") error (%d):'%s'\n", 
			data.db.c_str(), result.mr_code,  result.mr_message);
	}
	
	// Always return no error for after drop database. What could the server do about it?
	return false;
}

//==================================
// My schema event observers: 
static bool observeAfterDropTable(AfterDropTableEventData &data)
{
	PBMSResultRec result;
	if (data.err != 0)
		return false;

	if (MSEngine::dropTable(data.table.getSchemaName().c_str(), data.table.getTableName().c_str(), &result) != 0) {
		fprintf(stderr, "PBMSEvents: dropTable(\"%s.%s\") error (%d):'%s'\n", 
			data.table.getSchemaName().c_str(), data.table.getTableName().c_str(), result.mr_code,  result.mr_message);
		return true;
	}
	MSEngine::callCompleted(true);
	
	return false;
}

//---
static bool observeAfterRenameTable(AfterRenameTableEventData &data)
{
	PBMSResultRec result;
	if (data.err != 0)
		return false;

	const char *from_db = data.from.getSchemaName().c_str();
	const char *from_table = data.from.getTableName().c_str();
	const char *to_db = data.to.getSchemaName().c_str();
	const char *to_table = data.to.getTableName().c_str();
	
	if (MSEngine::renameTable(from_db, from_table, to_db, to_table, &result) != 0) {
		fprintf(stderr, "PBMSEvents: renameTable(\"%s.%s\" To \"%s.%s\") error (%d):'%s'\n", 
			from_db, from_table, to_db, to_table, result.mr_code,  result.mr_message);
		return true;
	}
	MSEngine::callCompleted(true);
	
	return false;
}

//==================================
/* This is where I register which table events my pluggin is interested in.*/
void PBMSEvents::registerTableEventsDo(TableShare &table_share, EventObserverList &observers)
{
  if ((PBMSParameters::isPBMSEventsEnabled() == false) 
    || (PBMSParameters::isBLOBTable(table_share.getSchemaName(), table_share.getTableName()) == false))
    return;
    
  if (table_share.blob_fields > 0) {
	  registerEvent(observers, BEFORE_INSERT_RECORD, PBMSParameters::getBeforeInsertEventPosition()); // I want to be called first if passible
	  registerEvent(observers, AFTER_INSERT_RECORD); 
	  registerEvent(observers, BEFORE_UPDATE_RECORD, PBMSParameters::getBeforeUptateEventPosition());
	  registerEvent(observers, AFTER_UPDATE_RECORD); 
	  registerEvent(observers, AFTER_DELETE_RECORD);
 }
}

//==================================
/* This is where I register which schema events my pluggin is interested in.*/
void PBMSEvents::registerSchemaEventsDo(const std::string &db, EventObserverList &observers)
{
  if ((PBMSParameters::isPBMSEventsEnabled() == false) 
    || (PBMSParameters::isBLOBDatabase(db.c_str()) == false))
    return;
    
  registerEvent(observers, AFTER_DROP_TABLE);
  registerEvent(observers, AFTER_RENAME_TABLE);
}

//==================================
/* This is where I register which schema events my pluggin is interested in.*/
void PBMSEvents::registerSessionEventsDo(Session &, EventObserverList &observers)
{
  if (PBMSParameters::isPBMSEventsEnabled() == false) 
    return;
    
  registerEvent(observers, AFTER_DROP_DATABASE);
}

//==================================
/* The event observer.*/
bool PBMSEvents::observeEventDo(EventData &data)
{
  bool result= false;
  
  switch (data.event) {
  case AFTER_DROP_DATABASE:
    result = observeAfterDropDatabase((AfterDropDatabaseEventData &)data);
    break;
    
  case AFTER_DROP_TABLE:
    result = observeAfterDropTable((AfterDropTableEventData &)data);
    break;
    
  case AFTER_RENAME_TABLE:
    result = observeAfterRenameTable((AfterRenameTableEventData &)data);
    break;
    
  case BEFORE_INSERT_RECORD:
     result = observeBeforeInsertRecord((BeforeInsertRecordEventData &)data);
    break;
    
  case AFTER_INSERT_RECORD:
    result = observeAfterInsertRecord((AfterInsertRecordEventData &)data);
    break;
    
 case BEFORE_UPDATE_RECORD:
    result = observeBeforeUpdateRecord((BeforeUpdateRecordEventData &)data);
    break;
             
  case AFTER_UPDATE_RECORD:
    result = observeAfterUpdateRecord((AfterUpdateRecordEventData &)data);
    break;
    
  case AFTER_DELETE_RECORD:
    result = observeAfterDeleteRecord((AfterDeleteRecordEventData &)data);
    break;

  default:
    fprintf(stderr, "PBMSEvents: Unexpected event '%s'\n", EventObserver::eventName(data.event));
 
  }
  
  return result;
}

