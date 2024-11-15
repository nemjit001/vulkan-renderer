
find_program(GLSL_COMPILER "glslc" REQUIRED)

function(target_enable_warnings_as_errors TARGET_NAME)
	message(STATUS "${TARGET_NAME}: Compiling warnings as errors")
	set_target_properties(${TARGET_NAME} PROPERTIES COMPILE_WARNING_AS_ERROR ON)
	if (MSVC)
		target_compile_options(${TARGET_NAME} PRIVATE /W4)
	else()
		target_compile_options(${TARGET_NAME} PRIVATE -Wall -Wextra -Wpedantic)
	endif()
endfunction()


function(target_compile_shaders TARGET_NAME TARGET_SHADER_SOURCES)
	foreach(SHADER_SOURCE IN LISTS TARGET_SHADER_SOURCES)
		message(STATUS "${TARGET_NAME}: found shader file ${SHADER_SOURCE}")
		get_filename_component(SHADER_NAME ${SHADER_SOURCE} NAME_WLE)
		set(SHADER_OUTPUT_FILE "${SHADER_NAME}.spv")

		add_custom_command(
			# TARGET ${TARGET_NAME}
			OUTPUT ${SHADER_OUTPUT_FILE}
			COMMAND ${GLSL_COMPILER} ${SHADER_SOURCE} -o "$<TARGET_FILE_DIR:${TARGET_NAME}>/${SHADER_OUTPUT_FILE}"
			DEPENDS ${SHADER_SOURCE}
			COMMENT "Compiling ${SHADER_SOURCE} -> $<TARGET_FILE_DIR:${TARGET_NAME}>/${SHADER_OUTPUT_FILE}"
		)

		list(APPEND ${TARGET_NAME}_SHADER_SOURCES ${SHADER_OUTPUT_FILE})
	endforeach()

	add_custom_target(${TARGET_NAME}_CompileShaders ALL
		DEPENDS "${${TARGET_NAME}_SHADER_SOURCES}"
		COMMENT "${TARGET_NAME}: Compiling shaders"
	)
	# add_dependencies(${TARGET_NAME} ${TARGET_NAME}_CompileShaders)
endfunction()
