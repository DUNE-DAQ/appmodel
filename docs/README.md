# Readoutdal

 This package extends the schema from the coredal package
to describe readout applications.

  ![schema](schema.png)

The ReadoutApplication has no direct relationships with DaqModules. It
has relationships to DaqModule configurations but the DaqModules
themselves are generated on the fly by ReadoutApplication's
`generate_modules()` method.