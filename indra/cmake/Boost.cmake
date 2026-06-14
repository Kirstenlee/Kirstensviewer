# -*- cmake -*-
include(Prebuilt)

include_guard()

add_library( ll::boost INTERFACE IMPORTED )


  set(BOOST_CONTEXT_LIBRARY boost_context-mt)
  set(BOOST_FIBER_LIBRARY boost_fiber-mt)
  set(BOOST_FILESYSTEM_LIBRARY boost_filesystem-mt)
  set(BOOST_PROGRAM_OPTIONS_LIBRARY boost_program_options-mt)
  set(BOOST_REGEX_LIBRARY boost_regex-mt)
  set(BOOST_SIGNALS_LIBRARY boost_signals-mt)
  set(BOOST_SYSTEM_LIBRARY boost_system-mt)
  set(BOOST_THREAD_LIBRARY boost_thread-mt)
  set(BOOST_URL_LIBRARY boost_url-mt)

  use_prebuilt_binary(boost)
  set(Boost_INCLUDE_DIRS ${LIBS_PREBUILT_DIR}/include)

  # As of sometime between Boost 1.67 and 1.72, Boost libraries are suffixed
  # with the address size.
  set(addrsfx "-x${ADDRESS_SIZE}")
      
      set(BOOST_CONTEXT_LIBRARY 
          libboost_context-mt${addrsfx})
     
     set(BOOST_FIBER_LIBRARY 
          libboost_fiber-mt${addrsfx})
          
      set(BOOST_FILESYSTEM_LIBRARY 
          libboost_filesystem-mt${addrsfx})
          
      set(BOOST_PROGRAM_OPTIONS_LIBRARY 
          libboost_program_options-mt${addrsfx})
         
      set(BOOST_REGEX_LIBRARY
          libboost_regex-mt${addrsfx})
         
      set(BOOST_SIGNALS_LIBRARY 
          libboost_signals-mt${addrsfx})
          
      set(BOOST_SYSTEM_LIBRARY 
          libboost_system-mt${addrsfx})
          
      set(BOOST_THREAD_LIBRARY 
          libboost_thread-mt${addrsfx})
          
      set(BOOST_URL_LIBRARY
          libboost_url-mt${addrsfx})




