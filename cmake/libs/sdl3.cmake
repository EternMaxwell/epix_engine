block()
  set(CMAKE_POLICY_DEFAULT_CMP0077 NEW)
  # A static SDL3 keeps the windowing library inside the engine binary, the way
  # the other windowing backends are built here.
  set(SDL_SHARED OFF CACHE BOOL "" FORCE)
  set(SDL_STATIC ON CACHE BOOL "" FORCE)
  set(SDL_TEST_LIBRARY OFF CACHE BOOL "" FORCE)
  set(SDL_TESTS OFF CACHE BOOL "" FORCE)
  set(SDL_EXAMPLES OFF CACHE BOOL "" FORCE)
  set(SDL_INSTALL OFF CACHE BOOL "" FORCE)
  set(SDL_INSTALL_CPACK OFF CACHE BOOL "" FORCE)
  set(SDL_UNINSTALL OFF CACHE BOOL "" FORCE)
  add_subdirectory(libs/sdl3)
endblock()
