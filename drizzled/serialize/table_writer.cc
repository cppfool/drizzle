#include <iostream>
#include <fstream>
#include <string>
#include <drizzled/serialize/table.pb.h>

using namespace std;
using namespace drizzle;

/*
  Written from Google proto example
*/

void fill_engine(::drizzle::Table::StorageEngine *engine)
{
  int16_t x;

  engine->set_name("InnoDB");
  Table::StorageEngine::EngineOption *option;

  string option_names[2]= {
    "INDEX_DIRECTORY"
    , "DATA_DIRECTORY"
  };

  string option_values[2]= {
    "/var/drizzle/indexdir"
    , "/var/drizzle/datadir"
  };

  /* Add some engine options */
  for (x= 0; x < 2; x++)
  {
    option= engine->add_option();
    option->set_option_name(option_names[x]);
    option->set_option_value(option_values[x]);
    option->set_option_type(Table::StorageEngine::EngineOption::STRING);
  }
}

void new_index_to_table(::drizzle::Table *table,
                        const string name,
                        uint16_t num_index_parts,
                        uint32_t field_indexes[],
                        uint32_t compare_lengths[],
                        bool is_primary,
                        bool is_unique)
{
  uint16_t x= 0;

  Table::Index *index;
  Table::Index::IndexPart *index_part;

  index= table->add_indexes();

  index->set_name(name);
  index->set_type(Table::Index::BTREE);
  index->set_is_primary(is_primary);
  index->set_is_unique(is_unique);

  while (x < num_index_parts)
  {
    index_part= index->add_index_part();

    index_part->set_fieldnr(field_indexes[x]);

    if (compare_lengths[x] > 0)
      index_part->set_compare_length(compare_lengths[x]);

    x++;
  }
}

void fill_table(::drizzle::Table *table, const char *name)
{
  uint16_t x;

  Table::Field *field;
  Table::Field::FieldConstraints *field_constraints;
  Table::Field::StringFieldOptions *string_field_options;
  Table::Field::NumericFieldOptions *numeric_field_options;
  Table::Field::SetFieldOptions *set_field_options;

  table->set_name(name);
  table->set_type(Table::STANDARD);

  /* Write out some random varchar */
  for (x= 0; x < 3; x++)
  {
    char buffer[1024];
    field= table->add_field();
    field_constraints= field->mutable_constraints();
    string_field_options= field->mutable_string_options();

    sprintf(buffer, "sample%u", x);

    field->set_name(buffer);
    field->set_type(Table::Field::VARCHAR);

    field_constraints->set_is_nullable((x % 2));

    string_field_options->set_length(rand() % 100);

    if (x % 3)
    {
      string_field_options->set_collation("utf8_swedish_ci");
    }
  }

  /* Write out an INTEGER */
  {
    field= table->add_field();
    field->set_name("number");
    field->set_type(Table::Field::INTEGER);
  }
  /* Write out a ENUM */
  {
    field= table->add_field();
    field->set_type(Table::Field::ENUM);
    field->set_name("colors");

    set_field_options= field->mutable_set_options();
    set_field_options->add_field_value("red");
    set_field_options->add_field_value("blue");
    set_field_options->add_field_value("green");
    set_field_options->set_count_elements(set_field_options->field_value_size());
  }
  /* Write out a BLOB */
  {
    field= table->add_field();
    field->set_name("some_btye_string");
    field->set_type(Table::Field::BLOB);
  }

  /* Write out a DECIMAL */
  {
    field= table->add_field();
    field->set_name("important_number");
    field->set_type(Table::Field::DECIMAL);

    field_constraints= field->mutable_constraints();
    field_constraints->set_is_nullable(true);

    numeric_field_options= field->mutable_numeric_options();
    numeric_field_options->set_precision(8);
    numeric_field_options->set_scale(3);
  }

  {
    uint32_t fields_in_index[1]= {6};
    uint32_t compare_lengths_in_index[1]= {0};
    bool is_unique= true;
    bool is_primary= false;
    /* Add a single-column index on important_number field */
    new_index_to_table(table, "idx_important_decimal", 1, fields_in_index, compare_lengths_in_index, is_primary, is_unique);
  }

  {
    /* Add a double-column index on first two varchar fields */
    uint32_t fields_in_index[2]= {0,1};
    uint32_t compare_lengths_in_index[2]= {20,35};
    bool is_unique= true;
    bool is_primary= true;
    new_index_to_table(table, "idx_varchar1_2", 2, fields_in_index, compare_lengths_in_index, is_primary, is_unique);
  }

  /* Do engine-specific stuff */
  Table::StorageEngine *engine= table->mutable_engine();
  fill_engine(engine);

}

int main(int argc, char* argv[])
{
  GOOGLE_PROTOBUF_VERIFY_VERSION;

  if (argc != 2)
  {
    cerr << "Usage:  " << argv[0] << " SCHEMA" << endl;
    return -1;
  }

  Table table;

  fill_table(&table, "example_table");

  fstream output(argv[1], ios::out | ios::trunc | ios::binary);
  if (!table.SerializeToOstream(&output))
  {
    cerr << "Failed to write schema." << endl;
    return -1;
  }

  return 0;
}