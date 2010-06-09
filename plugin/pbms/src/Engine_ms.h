/* Copyright (c) 2008 PrimeBase Technologies GmbH, Germany
 *
 * PrimeBase Media Stream for MySQL
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA 02111-1307 USA
 *
 * Original author: Paul McCullagh
 * Continued development: Barry Leslie
 *
 * 2007-07-20
 *
 * H&G2JCtL
 *
 * Engine interface
 *
 */

#ifndef __ENGINE_MS_H__
#define __ENGINE_MS_H__

#include "Defs_ms.h"

#include "pbms.h"
class MSOpenTable;

class MSEngine : public CSObject {
public:
	
#ifdef DRIZZLED
	static int startUp(PBMSResultPtr ) { return 0;}
	static void shutDown() {}
#else
	static int startUp(PBMSResultPtr result);

	static void shutDown();

	static const PBMSEnginePtr getEngineInfoAt(int indx);
#endif
	
	static int32_t	dropDatabase(const char *db_name, PBMSResultPtr result);
	static int32_t	createBlob(const char *db_name, const char *tab_name, char *blob, size_t blob_len, PBMSBlobURLPtr blob_url, PBMSResultPtr result);
	static int32_t	referenceBlob(const char *db_name, const char *tab_name, PBMSBlobURLPtr  ret_blob_url, char *blob_url, uint16_t col_index, PBMSResultPtr result);
	static int32_t	dereferenceBlob(const char *db_name, const char *tab_name, char *blob_url, PBMSResultPtr result);
	static int32_t	dropTable(const char *db_name, const char *tab_name, PBMSResultPtr result);
	static int32_t	renameTable(const char *from_db_name, const char *from_table, const char *to_db_name, const char *to_table, PBMSResultPtr result);
	static void		callCompleted(bool ok);
	
	static bool couldBeURL(const char *url, size_t length);
	
	private:
	static MSOpenTable *openTable(const char *db_name, const char *tab_name, bool create);
	static bool renameTable(const char *db_name, const char *from_table, const char *to_db_name, const char *to_table);
	static void completeRenameTable(struct UnDoInfo *info, bool ok);
};

#endif
