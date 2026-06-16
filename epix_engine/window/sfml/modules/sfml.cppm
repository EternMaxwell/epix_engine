module;
#ifndef EPIX_IMPORT_STD
#include <future>
#include <memory>
#include <optional>
#include <string>
#include <tuple>
#include <unordered_map>
#include <utility>
#include <vector>
#endif
#include <SFML/Window/Clipboard.hpp>
#include <SFML/Window/Cursor.hpp>
#include <SFML/Window/Event.hpp>
#include <SFML/Window/VideoMode.hpp>
#include <SFML/Window/WindowBase.hpp>
#include <SFML/Window/WindowEnums.hpp>
#include <SFML/Window/WindowHandle.hpp>

export module epix.sfml.core;
#ifdef EPIX_IMPORT_STD
import std;
#endif
export import epix.core;
export import epix.input;
export import epix.window;
export import epix.assets;
export import epix.image;
extern "C++" {
#include <epix/sfml/core.hpp>
}
