if(NOT DEFINED SRC OR NOT DEFINED DST)
    message(FATAL_ERROR "CopyResourcesIfExists.cmake requires SRC and DST")
endif()

if(EXISTS "${SRC}")
    file(COPY "${SRC}/" DESTINATION "${DST}")
endif()
