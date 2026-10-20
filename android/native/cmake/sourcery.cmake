# A CMake-only replacement for ares' `sourcery` tool.
#
# Upstream builds tools/sourcery for the host and runs it to turn resource.bml into
# resource.cpp / resource.hpp. That does not work when cross-compiling (Android NDK),
# so this does the same job at configure time with nothing but CMake.
#
# resource.bml uses two-space indentation:
#   namespace name=Resource
#     namespace name=MegaDrive
#       binary name=TMSS file="../Firmware/Mega Drive/tmss.rom"

function(ares_sourcery manifest out_cpp out_hpp)
  get_filename_component(base "${manifest}" DIRECTORY)
  file(STRINGS "${manifest}" lines)

  set(source "#include \"resource.hpp\"\n\n")
  set(header "")
  set(depth 0)

  foreach(line IN LISTS lines)
    if(line MATCHES "^[ \t]*$")
      continue()
    endif()
    string(REGEX REPLACE "^ +" "" unindented "${line}")
    string(LENGTH "${line}" line_length)
    string(LENGTH "${unindented}" unindented_length)
    math(EXPR indent_length "${line_length} - ${unindented_length}")
    math(EXPR level "${indent_length} / 2")

    # close namespaces we have left
    while(depth GREATER level)
      string(APPEND header "}\n")
      string(APPEND source "}\n")
      math(EXPR depth "${depth} - 1")
    endwhile()

    string(STRIP "${line}" line)
    if(line MATCHES "^namespace name=([A-Za-z0-9_]+)")
      set(name "${CMAKE_MATCH_1}")
      string(APPEND header "namespace ${name} {\n")
      string(APPEND source "namespace ${name} {\n")
      math(EXPR depth "${depth} + 1")
    elseif(line MATCHES "^binary name=([A-Za-z0-9_]+) file=(.*)$")
      set(name "${CMAKE_MATCH_1}")
      set(file "${CMAKE_MATCH_2}")
      string(REGEX REPLACE "^\"(.*)\"$" "\\1" file "${file}")
      set(path "${base}/${file}")
      if(NOT EXISTS "${path}")
        message(WARNING "sourcery: binary file ${file} not found")
        continue()
      endif()
      file(SIZE "${path}" size)
      file(READ "${path}" hex HEX)
      string(REGEX REPLACE "([0-9a-f][0-9a-f])" "0x\\1," bytes "${hex}")
      # wrap long lines so compilers and editors stay happy
      string(REGEX REPLACE "((0x..,){32})" "\\1\n" bytes "${bytes}")
      string(APPEND header "extern const unsigned char ${name}[${size}];\n")
      string(APPEND source "const unsigned char ${name}[${size}] = {\n${bytes}\n};\n")
    endif()
  endforeach()

  while(depth GREATER 0)
    string(APPEND header "}\n")
    string(APPEND source "}\n")
    math(EXPR depth "${depth} - 1")
  endwhile()

  # only rewrite when the content changed, so builds stay incremental
  foreach(pair "${out_hpp}|header" "${out_cpp}|source")
    string(REPLACE "|" ";" pair "${pair}")
    list(GET pair 0 path)
    list(GET pair 1 var)
    set(content "${${var}}")
    set(old "")
    if(EXISTS "${path}")
      file(READ "${path}" old)
    endif()
    if(NOT old STREQUAL content)
      file(WRITE "${path}" "${content}")
    endif()
  endforeach()
endfunction()
