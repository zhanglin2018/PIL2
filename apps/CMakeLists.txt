file(GLOB APPS2BUILD "*")

set(MAKE_TYPE "bin")
foreach(APPNAME ${APPS2BUILD})
	if(IS_DIRECTORY ${APPNAME} AND EXISTS ${APPNAME}/CMakeLists.txt)
		add_subdirectory(${APPNAME})
	endif()
endforeach()
