# Readoutdal

 This package extends the schema from the coredal package
to describe readout applications.

  ![schema](schema.png)

The ReadoutApplication is a SmartDaqApplication which has no direct
relationships with DaqModules. The DaqModules themselves are generated
on the fly by ReadoutApplication's `generate_modules()` method
according to DaqModule configurations given by the link_handler,
tp_handler and data_reader relationships.

'ReadoutApplication' is a 'ResourceSetAND'. This means it has a
'contains' relationship that can contain any class inheriting from
'RsourceBase' but should only contain 'ReadoutGroups'. The
'generate_modules' method is used to generate 'DataReaders' and
'DataLinkHandlers' on the fly. To know how to generate a 'DataReader'
a 'DataReaderConf' should be provided.

## DataFlow applications

  ![DFApplication](DFApplication.png)

The Datflow applications, which are also SmartDaqApplication which generate
DaqModules on the fly, are also included here.