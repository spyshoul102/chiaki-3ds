# Provides: Udev::libudev

set(_prefix Udev)
set(_target "${_prefix}::libudev")

find_package(PkgConfig)
if(PkgConfig_FOUND AND NOT TARGET ${_target})
	pkg_check_modules("${_prefix}" libudev IMPORTED_TARGET)
	if((TARGET PkgConfig::${_prefix}) AND (NOT CMAKE_VERSION VERSION_LESS "3.11.0"))
		set_target_properties(PkgConfig::${_prefix} PROPERTIES IMPORTED_GLOBAL ON)
		add_library(${_target} ALIAS PkgConfig::${_prefix})
	elseif(${_prefix}_FOUND)
		add_library(${_target} INTERFACE IMPORTED)
		set_target_properties(${_target} PROPERTIES
				INTERFACE_INCLUDE_DIRECTORIES "${${_prefix}_INCLUDE_DIRS}")
		set_target_properties(${_target} PROPERTIES
				INTERFACE_LINK_LIBRARIES "${${_prefix}_LIBRARIES}")
	endif()
endif()

include(FindPackageHandleStandardArgs)
find_package_handle_standard_args("${_prefix}"
	REQUIRED_VARS "${_prefix}_LIBRARIES"
	VERSION_VAR "${_prefix}_VERSION")

unset(_prefix)
unset(_target)
