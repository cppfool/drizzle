#
# The goal of this grammar is to stress test the operation of the HEAP storage engine by:
# 
# * Creating numerous tables, populating them rapidly and then dropping them
#
# * Using various DDL statements that cause HEAP tables to be created or manipulated
#
# * Have concurrent operation by using mostly TEMPORARY or connection-specific tables
#

query_init:
	create_definition SELECT short_value  AS f1 , short_value AS f2 , short_value AS f3 , short_value AS f4 , short_value AS f5 FROM DUAL ;

query:
	create | create | create |
	create | create | create |
	create | create | create |
	create | create | create |
	insert | insert | insert | insert |
	insert | insert | insert | insert |
	insert | insert | insert | insert |
	insert | insert | insert | insert |
	update | update | update | update |
	delete | delete | delete | delete |
	alter | drop ;

create:
	create_definition |
	create_definition select_all ;

alter:
	ALTER TABLE table_name ENGINE = HEAP ;

select_all:
	SELECT * FROM table_name ;

create_definition:
	CREATE temporary TABLE IF NOT EXISTS table_name (
		f1 column_def ,
		f2 column_def ,
		f3 column_def ,
		f4 column_def ,
		f5 column_def ,
		index_definition_list
	) /*executor1 ENGINE=HEAP KEY_BLOCK_SIZE = key_block_size */ ;

temporary:
	| | | | | | TEMPORARY ;

insert:
	insert_multi | 	insert_select ;

insert_multi:
	INSERT IGNORE INTO table_name VALUES row_list ;

insert_select:
	INSERT IGNORE INTO table_name select_all;

row_list:
	row , row , row , row |
	row_list , row ;

row:
	( value , value , value , value , value ) ;

index_definition_list:
	index_definition |
	index_definition , index_definition ;

index_definition:
	index_type ( index_column_list ) ;

index_type:
	KEY | KEY | UNIQUE | PRIMARY KEY ;

index_column_list:
	index_column /*  ( index_column_size ) */ |
	index_column /* ( index_column_size ) */ , index_column /* ( index_column_size ) */ ;	# bug 783366

index_column:
	f1 | f2 ;

index_column_size:
	1 | 2 | 32 ;

key_block_size:
	64 | 128 | 256 | 512 | 1024 | 2048 | 3072 ;

column_def:
	varchar ( size ) not_null default unique ;
#|
#	blob not_null unique ;

unique:
	| UNIQUE ;

size:
	32 | 128 | 512 | 1024 | 2048 | 3072 ;

varchar:
	VARCHAR | VARBINARY ;

blob:
	BLOB | MEDIUMBLOB | TINYBLOB | LONGBLOB ;

not_null:
	| NOT NULL ;

default:
	| DEFAULT _varchar(32) ;

unique:
	| UNIQUE ;

drop:
	DROP TABLE IF EXISTS table_name ;

table_name:
	connection_specific_table |
	connection_specific_table |
	connection_specific_table |
	connection_specific_table |
	connection_specific_table |
	connection_specific_table |
	connection_specific_table |
	connection_specific_table |
	global_table ;

connection_specific_table:
	{ 'local_'.$generator->threadId().'_'.$prng->int(1,3) } ;

global_table:
	global_1 | global_2 | global_3 | global_4 | global_5 ;

value_list:
	value , value |
	value , value_list ;

value:
	short_value | long_value ;

short_value:
	_digit | _varchar(1) | NULL | _english ;

long_value:
	REPEAT( _varchar(128) , _digit ) | NULL | _data ;

update:
	UPDATE table_name SET field_name = value WHERE where ;

delete:
	DELETE FROM table_name WHERE where ;

field_name:
	f1 | f2 | f3 | f4 | f5 ;

where:
	field_name cmp_op value |
	field_name not IN ( value_list );

not:
	| NOT ;

cmp_op:
	< | > | = | <= | >= | <> | <=> | != ;
