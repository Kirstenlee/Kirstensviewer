# ll_deploy_sharedlibs_command
# target_exe: the cmake target of the executable for which the shared libs will be deployed.
macro(ll_deploy_sharedlibs_command target_exe) 
  set(TARGET_LOCATION $<TARGET_FILE:${target_exe}>)
  get_filename_component(OUTPUT_PATH ${TARGET_LOCATION} PATH)

  add_custom_command(
    TARGET ${target_exe} POST_BUILD
    COMMAND ${CMAKE_COMMAND} 
    ARGS
      "-DBIN_NAME=\"${TARGET_LOCATION}\""
      "-DSEARCH_DIRS=\"${SEARCH_DIRS}\""
      "-DDST_PATH=\"${OUTPUT_PATH}\""
      "-P"
      "${CMAKE_SOURCE_DIR}/cmake/DeploySharedLibs.cmake"
  )
endmacro()

# ll_stage_sharedlib
# Performs config and adds a copy command for a sharedlib target.
macro(ll_stage_sharedlib DSO_TARGET)
  # target gets written to the DLL staging directory.
  set_target_properties(${DSO_TARGET} PROPERTIES RUNTIME_OUTPUT_DIRECTORY ${SHARED_LIB_STAGING_DIR})
endmacro()