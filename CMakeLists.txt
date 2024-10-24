cmake_minimum_required(VERSION 3.12)
project(appmodel VERSION 1.2.1)

find_package(daq-cmake REQUIRED)

daq_setup_environment()

find_package(okssystem REQUIRED)
find_package(oks REQUIRED)
find_package(oksdalgen REQUIRED)
find_package(ers REQUIRED)
find_package(logging REQUIRED)
find_package(conffwk REQUIRED)
find_package(confmodel REQUIRED)
find_package(fmt REQUIRED)

find_package(Boost COMPONENTS unit_test_framework program_options REQUIRED)

daq_oks_codegen(application.schema.xml fdmodules.schema.xml trigger.schema.xml wiec.schema.xml
  NAMESPACE dunedaq::appmodel DEP_PKGS confmodel)

daq_add_library(ReadoutApplication.cpp SmartDaqApplication.cpp
	DFApplication.cpp DFOApplication.cpp TPWriterApplication.cpp FakeDataApplication.cpp FakeHSIApplication.cpp DTSHSIApplication.cpp TriggerApplication.cpp MLTApplication.cpp HSIEventToTCApplication.cpp WIECApplication.cpp 
 LINK_LIBRARIES conffwk::conffwk fmt::fmt
  logging::logging confmodel::confmodel oks::oks ers::ers)

daq_add_application(getAppsArguments get_apps_arguments.cxx
  LINK_LIBRARIES confmodel::confmodel appmodel conffwk::conffwk)

daq_add_python_bindings(*.cpp LINK_LIBRARIES appmodel confmodel::confmodel)

daq_add_application(generate_modules_test generate_modules_test.cxx
 TEST LINK_LIBRARIES appmodel confmodel::confmodel conffwk::conffwk
 logging::logging)

daq_add_application(print_detailed_config_info print_detailed_config_info.cxx
 TEST LINK_LIBRARIES appmodel confmodel::confmodel conffwk::conffwk
 logging::logging)

daq_install()
