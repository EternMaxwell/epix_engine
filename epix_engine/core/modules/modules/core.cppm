module;

export module epix.core;

export import :utils;
export import :entities;
export import :type_registry;
export import :component;
export import :tick;
export import :ticks;
export import :label;
export import :labels;
export import :archetype;
export import :hierarchy;
export import :query;
export import :bundle;
export import :storage;
export import :world;
export import :system;
export import :schedule;
export import :app;

#ifdef EPIX_ENABLE_TEST
export import :query.access.test;
#endif