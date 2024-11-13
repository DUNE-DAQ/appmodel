from ._daq_appmodel_py import *

__all__= ['generate_modules']


__generate_class_map = {
    'ReadoutApplication': readout_application_generate,
    'DFApplication': df_application_generate,
    'DFOApplication': dfo_application_generate,
    'TPStreamWriterApplication': tpwriter_application_generate,
    'TriggerApplication': trigger_application_generate,
    'DFOApplication': dfo_application_generate,
    'FakeHSIApplication': fakehsi_application_generate,
    'HSIEventToTCApplication': hsieventtotc_application_generate,
    'FakeHSIApplication': fakehsi_application_generate,
    'HSIEventToTCApplication': hsieventtotc_application_generate,
    'MLTApplication': mlt_application_generate,
    'WIECApplication': wiec_application_generate,

}

class UnknownGeneratorException(Exception):
    pass

def generate_modules(confdb, app, session):

    generator = __generate_class_map.get(app.className())
    if generator is None:
        raise UnknownGeneratorException(f"Generator for {app.className()} not found")
    
    mods = generator(confdb._obj, confdb.active_database, app.id, session.id)

    return [confdb.get_dal(m.class_name, m.id) for m in mods]

