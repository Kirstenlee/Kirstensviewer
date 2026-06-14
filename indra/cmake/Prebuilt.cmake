include_guard()

macro(use_prebuilt_binary _binary)
	get_property(PREBUILT_PACKAGES TARGET prepare PROPERTY PREBUILT)
	list(FIND PREBUILT_PACKAGES ${_binary} _index)
	if(_index LESS 0)
		set_property(TARGET prepare APPEND PROPERTY PREBUILT ${_binary})
	endif()
endmacro()
