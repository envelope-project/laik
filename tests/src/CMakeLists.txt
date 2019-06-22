foreach (unit_test
	"kvs"
       	"location" )
    add_executable("${unit_test}test" "${CMAKE_CURRENT_SOURCE_DIR}/${unit_test}test.c")
    target_link_libraries ("${unit_test}test" PRIVATE "laik")
endforeach ()
