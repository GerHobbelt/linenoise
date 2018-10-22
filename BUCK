cxx_library(
  name = 'linenoise', 
  header_namespace = '', 
  exported_headers = [
    'linenoise.h', 
  ], 
  srcs = [
    'linenoise.c', 
  ], 
  visibility = [
    'PUBLIC', 
  ],
)

cxx_binary(
  name = 'example', 
  srcs = [
    'example.c', 
  ], 
  deps = [
    ':linenoise', 
  ], 
)
