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
import epix.core;
import epix.input;
import epix.window;
import epix.assets;
import epix.image;
extern "C++" {
#include <epix/sfml/core.hpp>
}
