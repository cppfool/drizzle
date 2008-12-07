#include <iostream>
#include <fstream>
#include <string>
#include "replication_event.pb.h"
using namespace std;

/*
  Example reader application for master.info data.
*/

void printRecord(const drizzle::EventList *list)
{
  using namespace drizzle;
  uint32_t x;

  for (x= 0; x < list->event_size(); x++)
  {
    const drizzle::Event event= list->event(x);

    if (x != 0)
      cout << endl << "##########################################################################################" << endl << endl;

    switch (event.type())
    {
    case Event::DDL:
      cout << "DDL ";
      break;
    case Event::INSERT:
      {
        uint32_t x;

        cout << "INSERT INTO " << event.table() << " (";

        for (x= 0; x < event.field_names_size() ; x++)
        {
          if (x != 0)
            cout << ", ";

          cout << event.field_names(x);
        }

        cout << ") VALUES " << endl;

        for (x= 0; x < event.values_size(); x++)
        {
          uint32_t y;
          Event_Value values= event.values(x);

          if (x != 0)
            cout << ", ";

          cout << "(";
          for (y= 0; y < values.value_size() ; y++)
          {
            if (y != 0)
              cout << ", ";

            cout << "\"" << values.value(y) << "\"";
          }
          cout << ")";
        }

        cout << ";" << endl;
        break;
      }
    case Event::DELETE:
      {
        uint32_t x;
        Event_Value values= event.values(0);

        cout << "DELETE FROM " << event.table() << " WHERE " << event.primary_key() << " IN (";

        for (x= 0; x < values.value_size() ; x++)
        {
          if (x != 0)
            cout << ", ";

          cout << "\"" << values.value(x) << "\"";
        }

        cout << ")" << endl;
        break;
      }
    case Event::UPDATE:
      {
        uint32_t count;

        for (count= 0; count < event.values_size() ; count++)
        {
          uint32_t x;
          Event_Value values= event.values(count);

          cout << "UPDATE "  << event.table() << " SET ";

          for (x= 1; x < values.value_size() ; x++)
          {
            if (x != 1)
              cout << ", ";

            cout << event.field_names(x - 1) << " = \"" << values.value(x) << "\"";
          }

          cout << " WHERE " << event.primary_key() << " = " << values.value(0) << endl;
        }

        break;
      }
    case Event::COMMIT:
      cout << "COMMIT" << endl;
      break;
    }
    cout << endl;

    if (event.has_sql())
      cout << "Original SQL: " << event.sql() << endl;

    cout << "AUTOCOMMIT: " << event.autocommit() << endl;
    cout << "Server id: " << event.server_id() << endl;
    cout << "Query id: " << event.query_id() << endl;
    cout << "Transaction id: " << event.transaction_id() << endl;
    cout << "Schema: " << event.schema() << endl;
    if (event.type() != Event::DDL)
      cout << "Table Name: " << event.table() << endl;
  }
}

int main(int argc, char* argv[])
{
  GOOGLE_PROTOBUF_VERIFY_VERSION;

  if (argc != 2)
  {
    cerr << "Usage:  " << argv[0] << " replication event log " << endl;
    return -1;
  }

  drizzle::EventList list;

  {
    // Read the existing master.info file
    fstream input(argv[1], ios::in | ios::binary);
    if (!list.ParseFromIstream(&input))
    {
      cerr << "Failed to parse master.info." << endl;
      return -1;
    }
  }

  printRecord(&list);

  return 0;
}
