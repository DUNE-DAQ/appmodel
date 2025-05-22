from ._daq_appmodel_py import *

__all__= ['generate_modules']

class UnknownGeneratorException(Exception):
    pass

def generate_modules(confdb, app, session):

    mods = smart_dap_application_generate_modules(confdb._obj, confdb.active_database, app.id, session.id)

    return [confdb.get_dal(m.class_name, m.id) for m in mods]

