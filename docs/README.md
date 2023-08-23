# Readoutdal

 This package extends the schema from the coredal package
to describe readout applications.

  ![schema](schema.png)

## ReadoutMap

 The ReadoutMap included here is currently a direct translation from
the jsonnet schema in the `daqconf` package with the addition of a
grouping class ReadoutGroup. 

 The DROStreamConf class inherits from ResourceBase allowing
individual streams to be disabled.  DROStreamConfs are grouped into
ReadoutGroups which inherit from ResourceSetAND so if all streams in a
group are disabled the group itselg is disabled.

The EthStreamParameters and the FlxStreamParameters classes both
contain host addresses. It is not clear how these relate to the
VirtualHost/PhysicalHost classes from the core schema.

## ReadoutApplication

 The ReadoutApplication is a SmartDaqApplication which has no direct
relationships with DaqModules. The DaqModules themselves are generated
on the fly by ReadoutApplication's `generate_modules()` method
according to DaqModule configurations given by the link_handler,
tp_handler and data_reader relationships.

ReadoutApplication is a ResourceSetAND. This means it has a
contains relationship that can contain any class inheriting from
RsourceBase but should only contain ReadoutGroups. The
`generate_modules` method is used to generate DataReaders and
DataLinkHandlers on the fly. To know how to generate a DataReader
a DataReaderConf must be provided.

### NICReader

 The NICReader, which is generated on the fly by the
 ReadoutApplication's `generate_modules()`, has a relationship to a
 NICReceiverConf which will be the same for all NICReceivers of the
 ReadoutApplication and maybe for all the ReadoutApplications in the
 Session. Its only distinguishing configuration item is the
 relationship it has to a DROStreamConf.

## DataFlow applications

  ![DFApplication](DFApplication.png)

The Datflow applications, which are also SmartDaqApplication which generate
DaqModules on the fly, are also included here.