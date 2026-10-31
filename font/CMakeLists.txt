idf_component_register(SRCS "stb_impl.cpp" "font.cpp"
                       INCLUDE_DIRS "."
                       EMBED_FILES "CrimsonText-Regular.ttf")

# stb_truetype is third-party code; don't let its warnings fail the build.
set_source_files_properties(stb_impl.cpp PROPERTIES COMPILE_OPTIONS "-w")
